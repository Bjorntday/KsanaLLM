/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ksana_llm/runtime/infer_stage.h"
#include "ksana_llm/utils/request.h"

namespace ksana_llm {

// The information used for forward.
struct ForwardRequest {
  // The request id.
  size_t req_id;

  // The infer stage, context decode or decode.
  InferStage infer_stage = InferStage::STAGE_CONTEXT;

  // The decode step, 0 for context decode, and then 1, 2, 3...
  int step = 0;

  // The custom length for the logits output, allowing for a specific size of logits to be generated.
  size_t logits_custom_length = 0;

  // Multimodal rotary position embedding offset, this points to the corresponding member in infer_request.
  int64_t *mrotary_embedding_pos_offset = nullptr;

  // The input tokens.
  std::vector<int>* output_tokens;

  // Embedding slice used to refit input embedding
  EmbeddingSlice* input_refit_embedding;

  // The key is the request target, which can only be a predefined set of requestable targets {embedding_lookup,
  // layernorm, transformer, logits}.
  const std::map<std::string, TargetDescribe>* request_target = nullptr;

  // The result of request_target.
  std::map<std::string, PythonTensor>* response = nullptr;

  // The output logits buf and offset.
  std::vector<float*> logits_buf;
  size_t logits_offset;

  // The kv cache addresses, for every device.
  std::vector<std::vector<void*>> kv_cache_ptrs;

  // The length of the flexible cache, indicating the number of elements stored in the flexible cache for potential
  // reuse in subsequent computations.
  int flexible_cache_len = 0;

  // A vector containing pointers to FlexibleCachedCopyTask objects, which represent tasks that involve copying data
  // flexibly between different memory regions.
  std::vector<FlexibleCachedCopyTask>* flexible_cached_copy_tasks = nullptr;

  // The flag for tagging request prefix cache usage
  bool is_use_prefix_cache = false;

  // The prefix cache tokens number
  int prefix_cache_len = 0;

  // The prefix cache blocks number
  int prefix_cache_blocks_number = 0;

  // is cudagraph capture call
  bool is_cudagraph_capture_request;

  // Opentelemetry SpanContext
  opentelemetry::trace::SpanContext span_context = opentelemetry::trace::SpanContext::GetInvalid();

  // The arrive time.
  uint64_t timestamp_in_ms;

  std::shared_ptr<std::unordered_map<std::string, std::string>> req_ctx;

#if defined(ENABLE_ACL) || defined(ENABLE_FLASH_ATTN_WITH_CACHE)
  // NOTE(karlluo): for ATB, all device blocks locate on a flatten plane memory space.
  // The Ksana kv cache consists of blocks, each of which is an independent storage space. The blocks are not
  // guaranteed to be contiguous in memory. Each block has a shape of [2, layer_num, block_token_num, head_num,
  // head_dim], where 2 represents key and value. The Ascend ATB kv cache consists of kcache and vcache, which are
  // independent contiguous storage spaces. The shapes of kcache and vcache are [num_blocks * layer_num,
  // block_token_num, head_num, head_dim]. Each block has a size of [block_token_num, head_num, head_dim]. To
  // interface with the NPU, Ascend ATB (hereinafter referred to as ATB) needs to be used. In order for the NPU's
  // self/paged attention to utilize Ksana's kv cache and share the underlying memory/GPU memory management
  // capabilities, the Ksana kv cache needs to be converted to the Ascend ATB kv cache format.
  // 1. Change the block allocation method so that the blocks are contiguous in physical memory, while the upper-level
  // pointers point to different storage spaces. Originally, each block in the Ksana kv cache called malloc once. This
  // should be changed to pre-allocate a contiguous storage space of size [num_blocks, 2, layer_num, block_token_num,
  // head_num, head_dim]. The pointers of each block should then point to cache_base_ptr + (block index * 2 *
  // layer_num * block_token_num * head_num * head_dim * sizeof(DTYPE)).
  // 2. During each inference process, each prompt will carry an array of block IDs, which can be used to obtain the
  // pointers to the storage space. For ATB, conversion is required to use these pointers. The conversion process is
  // as follows:
  //    - Given a block ID array [b0, b1, b2, b3, b4] and the base address pointer of the Ksana kv cache after the
  //    modification in step 1, cache_base_ptr.
  //    - For ATB: The Ksana kv cache has a total of num_blocks * 2 * layer_num blocks.
  //    - Therefore, the block ID array for ATB is [b0 * layer_num * 2, b1 * layer_num * 2, b2 * layer_num * 2, b3 *
  //    layer_num * 2, b4 * layer_num * 2].
  //    - Ksana's kv cache swaps memory/GPU memory at the block level, so to reuse Ksana's kv cache's underlying
  //    memory/GPU memory management capabilities, ATB's kcache and vcache share the same Ksana kv cache.
  //    - Since each block in Ksana is divided into K and V parts, each part having a size of [layer_num,
  //    block_token_num, head_num, head_dim].
  //    - To allow ATB's kcache and vcache to share the same block ID array, the kcache pointer is cache_base_ptr, and
  //    the vcache pointer is cache_base_ptr + (layer_num * block_token_num * head_num * head_dim * sizeof(DTYPE)).
  //    - Therefore, the block ID array for kcache/vcache is [b0 * layer_num * 2 + layer_idx, b1 * layer_num * 2 +
  //    layer_idx, b2 * layer_num * 2 + layer_idx, b3 * layer_num * 2 + layer_idx, b4 * layer_num * 2 + layer_idx].
  std::vector<std::vector<int32_t>> atb_kv_cache_base_blk_ids;
#endif
};

}  // namespace ksana_llm
