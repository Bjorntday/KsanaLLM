/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/
#pragma once

#include "ksana_llm/layers/attention_layer.h"

namespace ksana_llm {

template <typename SCALAR_T, typename CACHE_T, bool FP8_E5M2>
class PagedAttentionLayer : public AttentionLayer<SCALAR_T> {
 public:
  virtual Status Init(const std::vector<std::any>& parameters, std::shared_ptr<Context> context, int rank) override;
  virtual Status Forward(const std::vector<Tensor>& input_tensors, std::vector<Tensor>& output_tensors) override;

#ifdef ENABLE_ACL_ATB
 private:
  // ATB attention implement for ascend device, for ATB run with graph, each layer need's one kernel implement instance
  std::shared_ptr<llm_kernels::ascend::ATBPagedAttention<SCALAR_T>> atb_paged_attn_;
#endif
};

}  // namespace ksana_llm
