/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/
#pragma once

#include <memory>
#include "ksana_llm/models/common/common_weight.h"

namespace ksana_llm {

template <typename T>
class LlamaWeight : public BaseWeight {
 public:
  LlamaWeight() {}
  explicit LlamaWeight(const ModelConfig& model_config, int rank, std::shared_ptr<Context> context);

  Tensor GetModelWeights(const std::string& weight_name);

  Status LoadWeightsFromFile(std::shared_ptr<BaseFileTensorLoader>& weights_loader,
                             std::vector<std::string>& weight_name_list, std::vector<std::string>& custom_name_list);
  void PermuteQKWeight(std::shared_ptr<BaseFileTensorLoader>& weights_loader,
                       std::vector<std::string>& weight_name_list, std::vector<std::string>& custom_name_list);

  void ProcessWeights();

  std::shared_ptr<CommonWeight<T>> Getcommonweight() { return common_weight_; }

  void SetEmbeddingsConfig();

 private:
  // the common weight instance.
  std::shared_ptr<CommonWeight<T>> common_weight_ = nullptr;
};

}  // namespace ksana_llm
