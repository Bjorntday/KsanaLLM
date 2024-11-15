/* Copyright 2024 Tencent Inc.  All rights reserved.
==============================================================================*/

#include "ksana_llm/utils/stop_checker.h"

namespace ksana_llm {

bool StopChecker::CheckIncrementalStopStrings(const std::shared_ptr<InferRequest> req,
    const std::shared_ptr<Tokenizer> tokenizer) {
  if (!req->sampling_config.stop_strings.empty() && tokenizer != nullptr
    && req->output_tokens.size() > req->input_tokens.size()) {
    std::string decoded_output_tokens;
    std::vector<int> truncated_output_tokens;
    std::vector<int> generated_output = {req->output_tokens.back()};
    tokenizer->Decode(generated_output, decoded_output_tokens);
    const std::string suffix = "\ufffd";
    if (decoded_output_tokens.size() >= suffix.size() &&
        decoded_output_tokens.compare(decoded_output_tokens.size() - suffix.size(), suffix.size(), suffix) == 0) {
        decoded_output_tokens.erase(decoded_output_tokens.size() - suffix.size());
    }
    req->incremental_decoded_str.append(decoded_output_tokens);
    for (const auto& str : req->sampling_config.stop_strings) {
      if (req->incremental_decoded_str.find(str) != std::string::npos) {
        req->incremental_decoded_str.erase(req->incremental_decoded_str.find(str));
        tokenizer->Encode(req->incremental_decoded_str, truncated_output_tokens);
        req->output_tokens.erase(req->output_tokens.begin() + req->input_tokens.size(), req->output_tokens.end());
        req->output_tokens.insert(req->output_tokens.begin() + req->input_tokens.size(),
                        truncated_output_tokens.begin(), truncated_output_tokens.end());
        return true;
      }
    }
  }
  return false;
}

void StopChecker::CheckCompleteStopStrings(const std::shared_ptr<InferRequest> req,
    const std::shared_ptr<Tokenizer> tokenizer) {
  if (!req->sampling_config.stop_strings.empty() && tokenizer != nullptr) {
    std::string decoded_output_tokens;
    std::vector<int> truncated_output_tokens;
    std::vector<int> generated_output{req->output_tokens.begin() + req->input_tokens.size(), req->output_tokens.end()};
    tokenizer->Decode(generated_output, decoded_output_tokens);
    for (const auto& str : req->sampling_config.stop_strings) {
    if (decoded_output_tokens.find(str) != std::string::npos) {
        decoded_output_tokens.erase(decoded_output_tokens.find(str));
        tokenizer->Encode(decoded_output_tokens, truncated_output_tokens);
        req->output_tokens.erase(req->output_tokens.begin() + req->input_tokens.size(), req->output_tokens.end());
        req->output_tokens.insert(req->output_tokens.end(),
            truncated_output_tokens.begin(), truncated_output_tokens.end());
      }
    }
  }
}


}  // namespace ksana_llm
