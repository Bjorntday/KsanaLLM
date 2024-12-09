
#include <cuda_bf16.h>
#include <cuda_fp16.h>
#include <stdio.h>
#include "cache_copy.h"
#include "quant_utils.cuh"
namespace llm_kernels {
namespace nvidia {

#define MAX_THREADS_PER_BLOCK 1024
#define MAX_BLOCKS_PER_GRID_Y 65535

__device__ int k_chunk_size = 16;
/*
  block_size:     Number of tokens stored in each block.
  block_offsets:  Records the number of blocks for each batch size   [bs + 1,]
  prefix_offsets: Records the prefix length for each batch size (bs)
                  (accumulated from 0 to the current batch).         [bs + 1,]
*/
template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
__global__ void CacheCopyKernel(SCALAR_T* k_src, SCALAR_T* v_src, void** k_list, void** v_list, size_t* input_offsets,
                                size_t* prefix_offsets, int* block_offsets, int block_size, int bs, int total_len,
                                int num_heads, int head_size, int stride_size, float k_scale, float v_scale) {
  /*
    x:           In PagedAttention storage, KV-Blocks are divided into chunks to store head_size.
                 The variable x represents the size of each chunk.
    head_size_i: Indicates which chunk the head_size to be processed belongs to.
    j:           Represents the offset of the head_size to be processed within a single chunk.
  */
  int x = k_chunk_size / sizeof(SCALAR_T);
  int idx = blockIdx.y + blockIdx.z * gridDim.y;
  if (idx < total_len) {
    int batch_idx = 0;
    for (batch_idx = 0; batch_idx < bs; batch_idx++) {
      if (idx < input_offsets[batch_idx + 1]) {
        break;
      }
    }
    size_t prefix_limit = prefix_offsets[batch_idx + 1] - prefix_offsets[batch_idx] + input_offsets[batch_idx];
    if (idx < prefix_limit) {
      return;
    }
    int cur_block_offset = (idx - input_offsets[batch_idx]) / block_size;
    int cur_batch_offset = (idx - input_offsets[batch_idx]) % block_size;
    CACHE_T* k_dst_base = reinterpret_cast<CACHE_T*>(k_list[block_offsets[batch_idx] + cur_block_offset]);
    CACHE_T* v_dst_base = reinterpret_cast<CACHE_T*>(v_list[block_offsets[batch_idx] + cur_block_offset]);
    SCALAR_T* k_src_ptr = k_src + idx * stride_size;
    SCALAR_T* v_src_ptr = v_src + idx * stride_size;
    for (int hs_i = threadIdx.x; hs_i < head_size; hs_i += blockDim.x) {
      int head_size_i = hs_i / x;
      int j = hs_i % x;
      for (int num_head_i = blockIdx.x; num_head_i < num_heads; num_head_i += gridDim.x) {
        int k_src_index = num_head_i * head_size + head_size_i * x + j;
        int k_dst_index =
            num_head_i * (head_size * block_size) + head_size_i * (block_size * x) + cur_batch_offset * x + j;
        int i = head_size_i * x + j;
        int v_src_index = num_head_i * head_size + i;
        int v_dst_index = num_head_i * (head_size * block_size) + i * block_size + cur_batch_offset;
        // Assignment operation
        if constexpr (KV_DTYPE == llm_kernels::utils::KVCacheType::kAuto) {
          k_dst_base[k_dst_index] = k_src_ptr[k_src_index];
          v_dst_base[v_dst_index] = v_src_ptr[v_src_index];
        } else {
          k_dst_base[k_dst_index] = fp8::scaled_convert<CACHE_T, SCALAR_T, KV_DTYPE>(k_src_ptr[k_src_index], k_scale);
          v_dst_base[v_dst_index] = fp8::scaled_convert<CACHE_T, SCALAR_T, KV_DTYPE>(v_src_ptr[v_src_index], v_scale);
        }
      }
    }
  }
}

/*
  block_size:     Number of tokens stored in each block.
  block_offsets:  Records the number of blocks for each batch size   [bs + 1,]
  prefix_offsets: Records the prefix length for each batch size (bs)
                  (accumulated from 0 to the current batch).         [bs + 1,]
*/
template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
__global__ void ReverseCacheCopyKernel(SCALAR_T* k_src, SCALAR_T* v_src, void** k_list, void** v_list,
                                       size_t* input_offsets, size_t* prefix_offsets, int* block_offsets,
                                       int block_size, int bs, int total_len, int num_heads, int head_size,
                                       int stride_size, float k_scale, float v_scale) {
  /*
    x:           In PagedAttention storage, KV-Blocks are divided into chunks to store head_size.
                 The variable x represents the size of each chunk.
    head_size_i: Indicates which chunk the head_size to be processed belongs to.
    j:           Represents the offset of the head_size to be processed within a single chunk.
  */
  int x = k_chunk_size / sizeof(SCALAR_T);
  int idx = blockIdx.y + blockIdx.z * gridDim.y;
  if (idx < total_len) {
    int batch_idx = 0;
    for (batch_idx = 0; batch_idx < bs; batch_idx++) {
      if (idx < input_offsets[batch_idx + 1]) {
        break;
      }
    }
    size_t prefix_limit = prefix_offsets[batch_idx + 1] - prefix_offsets[batch_idx] + input_offsets[batch_idx];
    if (idx >= prefix_limit) {
      return;
    }
    int cur_block_offset = (idx - input_offsets[batch_idx]) / block_size;
    int cur_batch_offset = (idx - input_offsets[batch_idx]) % block_size;
    CACHE_T* k_dst_base = reinterpret_cast<CACHE_T*>(k_list[block_offsets[batch_idx] + cur_block_offset]);
    CACHE_T* v_dst_base = reinterpret_cast<CACHE_T*>(v_list[block_offsets[batch_idx] + cur_block_offset]);
    SCALAR_T* k_src_ptr = k_src + idx * stride_size;
    SCALAR_T* v_src_ptr = v_src + idx * stride_size;

    for (int hs_i = threadIdx.x; hs_i < head_size; hs_i += blockDim.x) {
      int head_size_i = hs_i / x;
      int j = hs_i % x;
      for (int num_head_i = blockIdx.x; num_head_i < num_heads; num_head_i += gridDim.x) {
        int k_src_index = num_head_i * head_size + head_size_i * x + j;
        int k_dst_index =
            num_head_i * (head_size * block_size) + head_size_i * (block_size * x) + cur_batch_offset * x + j;
        int i = head_size_i * x + j;
        int v_src_index = num_head_i * head_size + i;
        int v_dst_index = num_head_i * (head_size * block_size) + i * block_size + cur_batch_offset;
        // Reverse assignment operation
        if constexpr (KV_DTYPE == llm_kernels::utils::KVCacheType::kAuto) {
          k_src_ptr[k_src_index] = k_dst_base[k_dst_index];
          v_src_ptr[v_src_index] = v_dst_base[v_dst_index];
        } else {
          k_src_ptr[k_src_index] = fp8::scaled_convert<SCALAR_T, CACHE_T, KV_DTYPE>(k_dst_base[k_dst_index], k_scale);
          v_src_ptr[v_src_index] = fp8::scaled_convert<SCALAR_T, CACHE_T, KV_DTYPE>(v_dst_base[v_dst_index], v_scale);
        }
      }
    }
  }
}

template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
__global__ void FlexibleReverseCacheCopyKernel(CACHE_T** kv_src, CACHE_T** kv_dst, int* kv_list_src, int* kv_list_dst,
                                               int block_size, int layer_idx, int total_len, int num_heads,
                                               int head_size, int stride_size) {
  /*
    x:           In PagedAttention storage, KV-Blocks are divided into chunks to store head_size.
                 The variable x represents the size of each chunk.
    head_size_i: Indicates which chunk the head_size to be processed belongs to.
    j:           Represents the offset of the head_size to be processed within a single chunk.
  */
  int x = k_chunk_size / sizeof(SCALAR_T);
  int input_idx = blockIdx.y;
  if (input_idx < total_len) {
    int64_t layer_size = block_size * num_heads * head_size;
    CACHE_T* k_src = kv_src[input_idx] + layer_size * 2 * layer_idx;
    CACHE_T* k_dst = kv_dst[input_idx] + layer_size * 2 * layer_idx;
    CACHE_T* v_src = k_src + layer_size;
    CACHE_T* v_dst = k_dst + layer_size;
    int idx_src = kv_list_src[input_idx] % block_size;
    int idx_dst = kv_list_dst[input_idx] % block_size;
    for (int hs_i = threadIdx.x; hs_i < head_size; hs_i += blockDim.x) {
      int head_size_i = hs_i / x;
      int j = hs_i % x;
      for (int num_head_i = blockIdx.x; num_head_i < num_heads; num_head_i += gridDim.x) {
        int k_dst_index = num_head_i * (head_size * block_size) + head_size_i * (block_size * x) + idx_dst * x + j;
        int k_src_index = num_head_i * (head_size * block_size) + head_size_i * (block_size * x) + idx_src * x + j;
        int i = head_size_i * x + j;
        int v_dst_index = num_head_i * (head_size * block_size) + i * block_size + idx_dst;
        int v_src_index = num_head_i * (head_size * block_size) + i * block_size + idx_src;
        k_dst[k_dst_index] = k_src[k_src_index];
        v_dst[v_dst_index] = v_src[v_src_index];
      }
    }
  }
}

/*
  block_size:    Number of tokens stored in each block.
  block_offsets: Records the number of blocks for each batch size   [bs + 1,]
*/
template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
__global__ void CachePosCopyKernel(SCALAR_T* k_src, SCALAR_T* v_src, void** k_list, void** v_list, void* pos,
                                   size_t* input_offsets, int* block_offsets, int block_size, int bs, int total_len,
                                   int num_heads, int head_size, int stride_size, float k_scale, float v_scale) {
  /*
    x:           In PagedAttention storage, KV-Blocks are divided into chunks to store head_size.
                 The variable x represents the size of each chunk.
    head_size_i: Indicates which chunk the head_size to be processed belongs to.
    j:           Represents the offset of the head_size to be processed within a single chunk.
  */
  int x = k_chunk_size / sizeof(SCALAR_T);
  int idx = blockIdx.y;
  if (idx < total_len) {
    int input_len = reinterpret_cast<int64_t*>(pos)[idx];
    int cur_block_offset = input_len / block_size;
    int cur_batch_offset = input_len % block_size;
    CACHE_T* k_dst_base = reinterpret_cast<CACHE_T*>(k_list[block_offsets[idx] + cur_block_offset]);
    CACHE_T* v_dst_base = reinterpret_cast<CACHE_T*>(v_list[block_offsets[idx] + cur_block_offset]);
    SCALAR_T* k_src_ptr = k_src + idx * stride_size;
    SCALAR_T* v_src_ptr = v_src + idx * stride_size;

    for (int hs_i = threadIdx.x; hs_i < head_size; hs_i += blockDim.x) {
      int head_size_i = hs_i / x;
      int j = hs_i % x;
      for (int num_head_i = blockIdx.x; num_head_i < num_heads; num_head_i += gridDim.x) {
        int k_src_index = num_head_i * head_size + head_size_i * x + j;
        int k_dst_index =
            num_head_i * (head_size * block_size) + head_size_i * (block_size * x) + cur_batch_offset * x + j;
        int i = head_size_i * x + j;
        int v_src_index = num_head_i * head_size + i;
        int v_dst_index = num_head_i * (head_size * block_size) + i * block_size + cur_batch_offset;
        //  赋值操作
        if constexpr (KV_DTYPE == llm_kernels::utils::KVCacheType::kAuto) {
          k_dst_base[k_dst_index] = k_src_ptr[k_src_index];
          v_dst_base[v_dst_index] = v_src_ptr[v_src_index];
        } else {
          k_dst_base[k_dst_index] = fp8::scaled_convert<CACHE_T, SCALAR_T, KV_DTYPE>(k_src_ptr[k_src_index], k_scale);
          v_dst_base[v_dst_index] = fp8::scaled_convert<CACHE_T, SCALAR_T, KV_DTYPE>(v_src_ptr[v_src_index], v_scale);
        }
      }
    }
  }
}

template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
void CacheCopy(SCALAR_T* k_src, SCALAR_T* v_src, void** k_list, void** v_list, size_t* input_offsets,
               size_t* prefix_offsets, int* block_offsets, int block_size, int bs, int total_len, int num_heads,
               int head_size, int stride_size, float k_scale, float v_scale, cudaStream_t stream) {
  int grid_y = std::min(total_len, MAX_BLOCKS_PER_GRID_Y);
  int grid_z = (total_len + MAX_BLOCKS_PER_GRID_Y - 1) / MAX_BLOCKS_PER_GRID_Y;
  dim3 grid_shape(num_heads, grid_y, grid_z);

  dim3 block_shape(std::min(head_size, MAX_THREADS_PER_BLOCK));
  CacheCopyKernel<SCALAR_T, CACHE_T, KV_DTYPE><<<grid_shape, block_shape, 0, stream>>>(
      k_src, v_src, k_list, v_list, input_offsets, prefix_offsets, block_offsets, block_size, bs, total_len, num_heads,
      head_size, stride_size, k_scale, v_scale);
}

template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
void ReverseCacheCopy(SCALAR_T* k_src, SCALAR_T* v_src, void** k_list, void** v_list, size_t* input_offsets,
                      size_t* prefix_offsets, int* block_offsets, int block_size, int bs, int total_len, int num_heads,
                      int head_size, int stride_size, float k_scale, float v_scale, cudaStream_t stream) {
  int grid_y = std::min(total_len, MAX_BLOCKS_PER_GRID_Y);
  int grid_z = (total_len + MAX_BLOCKS_PER_GRID_Y - 1) / MAX_BLOCKS_PER_GRID_Y;
  dim3 grid_shape(num_heads, grid_y, grid_z);

  dim3 block_shape(std::min(head_size, MAX_THREADS_PER_BLOCK));
  ReverseCacheCopyKernel<SCALAR_T, CACHE_T, KV_DTYPE><<<grid_shape, block_shape, 0, stream>>>(
      k_src, v_src, k_list, v_list, input_offsets, prefix_offsets, block_offsets, block_size, bs, total_len, num_heads,
      head_size, stride_size, k_scale, v_scale);
}

template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
void CachePosCopy(SCALAR_T* k_src, SCALAR_T* v_src, void** k_list, void** v_list, void* pos, size_t* input_offsets,
                  int* block_offsets, int block_size, int bs, int total_len, int num_heads, int head_size,
                  int stride_size, float k_scale, float v_scale, cudaStream_t stream) {
  dim3 grid_shape(num_heads, total_len);
  dim3 block_shape(std::min(head_size, MAX_THREADS_PER_BLOCK));
  CachePosCopyKernel<SCALAR_T, CACHE_T, KV_DTYPE><<<grid_shape, block_shape, 0, stream>>>(
      k_src, v_src, k_list, v_list, pos, input_offsets, block_offsets, block_size, bs, total_len, num_heads, head_size,
      stride_size, k_scale, v_scale);
}

template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
__global__ void ConvertFP8AndBackKernel(SCALAR_T* data, size_t dim0, size_t dim1, int stride_size, float scale) {
  if constexpr (KV_DTYPE == llm_kernels::utils::KVCacheType::kAuto) {
    return;
  }
  size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx < dim0 * dim1) {
    // FP16 to FP8
    auto data_idx = idx / dim1 * stride_size + idx % dim1;
    CACHE_T temp = fp8::scaled_convert<CACHE_T, SCALAR_T, KV_DTYPE>(data[data_idx], scale);
    // FP8 back to FP16
    data[data_idx] = fp8::scaled_convert<SCALAR_T, CACHE_T, KV_DTYPE>(temp, scale);
  }
}

template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
void ConvertFP8AndBack(SCALAR_T* data, size_t dim0, size_t dim1, int stride_size, float scale, cudaStream_t stream) {
  int threads_per_block = 256;
  int blocks_per_grid = (dim0 * dim1 + threads_per_block - 1) / threads_per_block;
  ConvertFP8AndBackKernel<SCALAR_T, CACHE_T, KV_DTYPE>
      <<<blocks_per_grid, threads_per_block, 0, stream>>>(data, dim0, dim1, stride_size, scale);
}

template <typename SCALAR_T, typename CACHE_T, llm_kernels::utils::KVCacheType KV_DTYPE>
void FlexibleReverseCacheCopy(CACHE_T** kv_src, CACHE_T** kv_dst, int* kv_list_src, int* kv_list_dst, int block_size,
                              int layer_idx, int total_len, int num_heads, int head_size, int stride_size,
                              cudaStream_t stream) {
  dim3 grid_shape(num_heads, total_len);
  dim3 block_shape(std::min(head_size, MAX_THREADS_PER_BLOCK));
  FlexibleReverseCacheCopyKernel<SCALAR_T, CACHE_T, KV_DTYPE><<<grid_shape, block_shape, 0, stream>>>(
      kv_src, kv_dst, kv_list_src, kv_list_dst, block_size, layer_idx, total_len, num_heads, head_size, stride_size);
}

#define CACHE_COPY_FUNCTION_DECLARATION(SCALAR_T, CACHE_T, KV_DTYPE)                                                   \
  template void CacheCopy<SCALAR_T, CACHE_T, KV_DTYPE>(                                                                \
      SCALAR_T * k_src, SCALAR_T * v_src, void** k_list, void** v_list, size_t* input_offsets, size_t* prefix_offsets, \
      int* block_offsets, int block_size, int bs, int total_len, int num_heads, int head_size, int stride_size,        \
      float k_scale, float v_scale, cudaStream_t stream);                                                              \
  template void ReverseCacheCopy<SCALAR_T, CACHE_T, KV_DTYPE>(                                                         \
      SCALAR_T * k_src, SCALAR_T * v_src, void** k_list, void** v_list, size_t* input_offsets, size_t* prefix_offsets, \
      int* block_offsets, int block_size, int bs, int total_len, int num_heads, int head_size, int stride_size,        \
      float k_scale, float v_scale, cudaStream_t stream);                                                              \
  template void CachePosCopy<SCALAR_T, CACHE_T, KV_DTYPE>(                                                             \
      SCALAR_T * k_src, SCALAR_T * v_src, void** k_list, void** v_list, void* pos, size_t* input_offsets,              \
      int* block_offsets, int block_size, int bs, int total_len, int num_heads, int head_size, int stride_size,        \
      float k_scale, float v_scale, cudaStream_t stream);                                                              \
  template void FlexibleReverseCacheCopy<SCALAR_T, CACHE_T, KV_DTYPE>(                                                 \
      CACHE_T * *kv_src, CACHE_T * *kv_dst, int* kv_list_src, int* kv_list_dst, int block_size, int layer_idx,         \
      int total_len, int num_heads, int head_size, int stride_size, cudaStream_t stream);                              \
  template void ConvertFP8AndBack<SCALAR_T, CACHE_T, KV_DTYPE>(SCALAR_T * data, size_t dim0, size_t dim1,              \
                                                               int stride_size, float scale, cudaStream_t stream);

CACHE_COPY_FUNCTION_DECLARATION(float, float, llm_kernels::utils::KVCacheType::kAuto);
CACHE_COPY_FUNCTION_DECLARATION(float, uint8_t, llm_kernels::utils::KVCacheType::kFp8E4M3);
CACHE_COPY_FUNCTION_DECLARATION(float, uint8_t, llm_kernels::utils::KVCacheType::kFp8E5M2);
CACHE_COPY_FUNCTION_DECLARATION(half, half, llm_kernels::utils::KVCacheType::kAuto);
CACHE_COPY_FUNCTION_DECLARATION(half, uint8_t, llm_kernels::utils::KVCacheType::kFp8E4M3);
CACHE_COPY_FUNCTION_DECLARATION(half, uint8_t, llm_kernels::utils::KVCacheType::kFp8E5M2);
CACHE_COPY_FUNCTION_DECLARATION(__nv_bfloat16, __nv_bfloat16, llm_kernels::utils::KVCacheType::kAuto);
CACHE_COPY_FUNCTION_DECLARATION(__nv_bfloat16, uint8_t, llm_kernels::utils::KVCacheType::kFp8E4M3);
CACHE_COPY_FUNCTION_DECLARATION(__nv_bfloat16, uint8_t, llm_kernels::utils::KVCacheType::kFp8E5M2);
#undef CACHE_COPY_FUNCTION_DECLARATION

}  // namespace nvidia
}  // namespace llm_kernels
