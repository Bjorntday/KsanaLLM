/*
 * Copyright 2024 Tencent Inc.  All rights reserved.
 * Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <vector>

#include "csrc/kernels/nvidia/cutlass_extensions/gemm_configs.h"
#include "csrc/utils/nvidia/cuda_utils.h"
#include "cute/tensor.hpp"

namespace llm_kernels {
namespace nvidia {

template <class TileShape, class ClusterShape, class ActivationType>
struct should_filter_sm90_gemm_problem_shape {
#ifdef FAST_BUILD
  constexpr static int TILE_K = 128 * 8 / cutlass::sizeof_bits<ActivationType>::value;
  using SupportedCtaShape = cute::Shape<cute::_128, cute::_128, cute::Int<TILE_K>>;
  using SupportedCgaShape = cute::Shape<cute::_1, cute::_1, cute::_1>;

  constexpr static bool value =
      !cute::is_same_v<SupportedCtaShape, TileShape> || !cute::is_same_v<SupportedCgaShape, ClusterShape>;
#else
  constexpr static bool value = false;
#endif
};
template <class TileShape, class ClusterShape, class ActivationType>
constexpr static bool should_filter_sm90_gemm_problem_shape_v =
    should_filter_sm90_gemm_problem_shape<TileShape, ClusterShape, ActivationType>::value;

std::vector<llm_kernels::nvidia::cutlass_extensions::CutlassGemmConfig> get_candidate_configs(
    int sm, int const max_split_k,
    llm_kernels::nvidia::cutlass_extensions::CutlassGemmConfig::CandidateConfigTypeParam const);

llm_kernels::nvidia::cutlass_extensions::CutlassGemmConfig estimate_best_config_from_occupancies(
    std::vector<llm_kernels::nvidia::cutlass_extensions::CutlassGemmConfig> const& candidate_configs,
    std::vector<int> const& occupancies, int64_t const m, int64_t const n, int64_t const k, int64_t const num_experts,
    int const split_k_limit, size_t const workspace_bytes, int const multi_processor_count, int const is_weight_only);

}  // namespace nvidia
}  // namespace llm_kernels
