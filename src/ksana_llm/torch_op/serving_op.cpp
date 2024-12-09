/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/

#include "ksana_llm/torch_op/serving_op.h"

#include <iostream>
#include <memory>
#include <string>

#include "ksana_llm/endpoints/endpoint_factory.h"
#include "ksana_llm/endpoints/streaming/streaming_iterator.h"
#include "ksana_llm/profiler/profiler.h"
#include "ksana_llm/profiler/reporter.h"
#include "ksana_llm/utils/logger.h"
#include "ksana_llm/utils/request.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include "pybind11/stl_bind.h"

PYBIND11_MAKE_OPAQUE(std::map<std::string, ksana_llm::TargetDescribe>);
PYBIND11_MAKE_OPAQUE(std::map<std::string, ksana_llm::PythonTensor>);
PYBIND11_MAKE_OPAQUE(std::vector<std::pair<int, int>>);

namespace ksana_llm {

ServingOp::ServingOp() {}

ServingOp::~ServingOp() {
  Singleton<RequestPacker>::GetInstance()->DestroyTokenizer();
  inference_server_->Stop();
}

void ServingOp::InitServing(const std::string &config_file) {
  inference_server_ = std::make_shared<InferenceServer>(config_file, endpoint_config_);
  STATUS_CHECK_FAILURE(inference_server_->Start());

  ModelConfig model_config;
  STATUS_CHECK_FAILURE(Singleton<Environment>::GetInstance()->GetModelConfig("", model_config));
  try {
    Singleton<RequestPacker>::GetInstance()->InitTokenizer(model_config.path);
  } catch (const py::error_already_set &e) {
    PyErr_Clear();
    KLLM_THROW(fmt::format("Failed to init the tokenizer from {}.", model_config.path));
  }
}

Status ServingOp::Generate(const std::shared_ptr<KsanaPythonInput> &ksana_python_input,
                           const std::shared_ptr<std::unordered_map<std::string, std::string>> &req_ctx,
                           ksana_llm::KsanaPythonOutput &ksana_python_output) {
  KLLM_LOG_DEBUG << "ServingOp::Generate invoked.";
  const HttpTextMapCarrier<const std::unordered_map<std::string, std::string>> carrier(*req_ctx);
  auto span = REPORT_TRACE(serving_op_generate_span, carrier);
  opentelemetry::trace::Scope scope(span);
  STATUS_CHECK_AND_REPORT(inference_server_->Handle(ksana_python_input, req_ctx, ksana_python_output), span);
}

Status ServingOp::GenerateStreaming(const std::shared_ptr<KsanaPythonInput> &ksana_python_input,
                                    const std::shared_ptr<std::unordered_map<std::string, std::string>> &req_ctx,
                                    std::shared_ptr<StreamingIterator> &streaming_iterator) {
  KLLM_LOG_DEBUG << "ServingOp::GenerateStreaming invoked.";
  const HttpTextMapCarrier<const std::unordered_map<std::string, std::string>> carrier(*req_ctx);
  auto span = REPORT_TRACE(serving_op_generate_streaming_span, carrier);
  opentelemetry::trace::Scope scope(span);
  STATUS_CHECK_AND_REPORT(inference_server_->HandleStreaming(ksana_python_input, req_ctx, streaming_iterator), span);
}

Status ServingOp::Forward(const std::string &request_bytes,
                          const std::shared_ptr<std::unordered_map<std::string, std::string>> &req_ctx,
                          std::string &response_bytes) {
  KLLM_LOG_DEBUG << "ServingOp::Forward invoked.";
  const HttpTextMapCarrier<const std::unordered_map<std::string, std::string>> carrier(*req_ctx);
  auto span = REPORT_TRACE(serving_op_forward_span, carrier);
  opentelemetry::trace::Scope scope(span);
  STATUS_CHECK_AND_REPORT(inference_server_->HandleForward(request_bytes, req_ctx, response_bytes), span);
}

}  // namespace ksana_llm

PYBIND11_MODULE(libtorch_serving, m) {
  // Export `Status` to python.
  pybind11::class_<ksana_llm::Status, std::shared_ptr<ksana_llm::Status>>(m, "Status")
      .def(pybind11::init())
      .def(pybind11::init<ksana_llm::RetCode, const std::string &>())
      .def("OK", &ksana_llm::Status::OK)
      .def("GetMessage", &ksana_llm::Status::GetMessage)
      .def("GetCode", &ksana_llm::Status::GetCode)
      .def("ToString", &ksana_llm::Status::ToString);

  // Export `RetCode` to python, only export the values used in python.
  pybind11::enum_<ksana_llm::RetCode>(m, "RetCode", pybind11::arithmetic())
      .value("RET_SUCCESS", ksana_llm::RetCode::RET_SUCCESS)
      .value("RET_INVALID_ARGUMENT", ksana_llm::RetCode::RET_INVALID_ARGUMENT)
      .value("RET_STOP_ITERATION", ksana_llm::RetCode::RET_STOP_ITERATION)
      .export_values();

  // Export `SamplingConfig` to python.
  pybind11::class_<ksana_llm::SamplingConfig, std::shared_ptr<ksana_llm::SamplingConfig>>(m, "SamplingConfig")
      .def(pybind11::init<>())
      .def_readwrite("topk", &ksana_llm::SamplingConfig::topk)
      .def_readwrite("topp", &ksana_llm::SamplingConfig::topp)
      .def_readwrite("temperature", &ksana_llm::SamplingConfig::temperature)
      .def_readwrite("max_new_tokens", &ksana_llm::SamplingConfig::max_new_tokens)
      .def_readwrite("logprobs_num", &ksana_llm::SamplingConfig::logprobs_num)
      .def_readwrite("repetition_penalty", &ksana_llm::SamplingConfig::repetition_penalty)
      .def_readwrite("no_repeat_ngram_size", &ksana_llm::SamplingConfig::no_repeat_ngram_size)
      .def_readwrite("encoder_no_repeat_ngram_size", &ksana_llm::SamplingConfig::encoder_no_repeat_ngram_size)
      .def_readwrite("num_beams", &ksana_llm::SamplingConfig::num_beams)
      .def_readwrite("num_return_sequences", &ksana_llm::SamplingConfig::num_return_sequences)
      .def_readwrite("length_penalty", &ksana_llm::SamplingConfig::length_penalty)
      .def_readwrite("stop_token_ids", &ksana_llm::SamplingConfig::stop_token_ids)
      .def_readwrite("ignore_eos", &ksana_llm::SamplingConfig::ignore_eos)
      .def_readwrite("stop_strings", &ksana_llm::SamplingConfig::stop_strings);

  // Export `EmbeddingSlice` to python.
  pybind11::class_<ksana_llm::EmbeddingSlice, std::shared_ptr<ksana_llm::EmbeddingSlice>>(m, "EmbeddingSlice")
      .def(pybind11::init<>())
      .def_readwrite("pos", &ksana_llm::EmbeddingSlice::pos)
      .def_readwrite("embeddings", &ksana_llm::EmbeddingSlice::embeddings)
      .def_readwrite("embedding_tensors", &ksana_llm::EmbeddingSlice::embedding_tensors);

  // Export `TokenReduceMode` to python.
  pybind11::enum_<ksana_llm::TokenReduceMode>(m, "TokenReduceMode", pybind11::arithmetic())
      .value("GATHER_ALL", ksana_llm::TokenReduceMode::GATHER_ALL)
      .value("GATHER_TOKEN_ID", ksana_llm::TokenReduceMode::GATHER_TOKEN_ID)
      .export_values();

  // Export `TargetDescribe` to python.
  pybind11::class_<ksana_llm::TargetDescribe, std::shared_ptr<ksana_llm::TargetDescribe>>(m, "TargetDescribe")
      .def(pybind11::init<>())
      .def_readwrite("token_id", &ksana_llm::TargetDescribe::token_id)
      .def_readwrite("slice_pos", &ksana_llm::TargetDescribe::slice_pos)
      .def_readwrite("token_reduce_mode", &ksana_llm::TargetDescribe::token_reduce_mode);

  py::bind_vector<std::vector<std::pair<size_t, size_t>>>(m, "VectorSizeTPair");
  py::bind_map<std::map<std::string, ksana_llm::TargetDescribe>>(m, "TargetDescribeMap");

  // Export `KsanaPythonInput` to python.
  pybind11::class_<ksana_llm::KsanaPythonInput, std::shared_ptr<ksana_llm::KsanaPythonInput>>(m, "KsanaPythonInput")
      .def(pybind11::init<>())
      .def_readwrite("model_name", &ksana_llm::KsanaPythonInput::model_name)
      .def_readwrite("sampling_config", &ksana_llm::KsanaPythonInput::sampling_config)
      .def_readwrite("input_tokens", &ksana_llm::KsanaPythonInput::input_tokens)
      .def_readwrite("request_target", &ksana_llm::KsanaPythonInput::request_target)
      .def_readwrite("input_refit_embedding", &ksana_llm::KsanaPythonInput::input_refit_embedding)
      .def_readwrite("structured_output_regex", &ksana_llm::KsanaPythonInput::structured_output_regex);

  // Export `PythonTensor` to python.
  pybind11::class_<ksana_llm::PythonTensor, std::shared_ptr<ksana_llm::PythonTensor>>(m, "PythonTensor")
      .def(pybind11::init<>())
      .def_readwrite("data", &ksana_llm::PythonTensor::data)
      .def_readwrite("shape", &ksana_llm::PythonTensor::shape)
      .def_readwrite("dtype", &ksana_llm::PythonTensor::dtype);

  py::bind_map<std::map<std::string, ksana_llm::PythonTensor>>(m, "PythonTensorMap");

  // Export `KsanaPythonOutput` to python.
  pybind11::class_<ksana_llm::KsanaPythonOutput, std::shared_ptr<ksana_llm::KsanaPythonOutput>>(m, "KsanaPythonOutput")
      .def(pybind11::init<>())
      .def_readwrite("output_tokens", &ksana_llm::KsanaPythonOutput::output_tokens)
      .def_readwrite("logprobs", &ksana_llm::KsanaPythonOutput::logprobs)
      .def_readwrite("response", &ksana_llm::KsanaPythonOutput::response)
      .def_readwrite("embedding", &ksana_llm::KsanaPythonOutput::embedding);

  // Export `StreamingIterator` to python.
  pybind11::class_<ksana_llm::StreamingIterator, std::shared_ptr<ksana_llm::StreamingIterator>>(m, "StreamingIterator")
      .def(pybind11::init<>())
      .def("GetNext", [](std::shared_ptr<ksana_llm::StreamingIterator> &self) {
        pybind11::gil_scoped_release release;
        ksana_llm::KsanaPythonOutput ksana_python_output;
        ksana_llm::Status status = self->GetNext(ksana_python_output);
        pybind11::gil_scoped_acquire acquire;
        return std::make_tuple(status, std::move(ksana_python_output));
      });

  // Export `EndpointType` to python, only export the values used in python.
  pybind11::enum_<ksana_llm::EndpointType>(m, "EndpointType", pybind11::arithmetic())
      .value("RPC", ksana_llm::EndpointType::RPC)
      .export_values();

  // Export `EndpointConfig` to python.
  pybind11::class_<ksana_llm::EndpointConfig, std::shared_ptr<ksana_llm::EndpointConfig>>(m, "EndpointConfig")
      .def(pybind11::init<>())
      .def_readwrite("type", &ksana_llm::EndpointConfig::type)
      .def_readwrite("rpc_plugin_name", &ksana_llm::EndpointConfig::rpc_plugin_name)
      .def_readwrite("host", &ksana_llm::EndpointConfig::host)
      .def_readwrite("port", &ksana_llm::EndpointConfig::port)
      .def_readwrite("access_log", &ksana_llm::EndpointConfig::access_log);

  // Export `ServingOp` to python.
  pybind11::class_<ksana_llm::ServingOp, std::shared_ptr<ksana_llm::ServingOp>>(m, "Serving")
      .def(pybind11::init<>())
      .def("init_serving", &ksana_llm::ServingOp::InitServing)
      .def_readwrite("endpoint_config", &ksana_llm::ServingOp::endpoint_config_)
      .def("generate",
           [](std::shared_ptr<ksana_llm::ServingOp> &self,
              const std::shared_ptr<ksana_llm::KsanaPythonInput> &ksana_python_input,
              std::unordered_map<std::string, std::string> &req_ctx) {
             pybind11::gil_scoped_release release;
             ksana_llm::KsanaPythonOutput ksana_python_output;
             auto req_ctx_ptr = std::make_shared<std::unordered_map<std::string, std::string>>(req_ctx);
             ksana_llm::Status status = self->Generate(ksana_python_input, req_ctx_ptr, ksana_python_output);
             pybind11::gil_scoped_acquire acquire;
             return std::make_tuple(status, std::move(ksana_python_output));
           })
      .def("generate_streaming",
           [](std::shared_ptr<ksana_llm::ServingOp> &self,
              const std::shared_ptr<ksana_llm::KsanaPythonInput> &ksana_python_input,
              std::unordered_map<std::string, std::string> &req_ctx) {
             pybind11::gil_scoped_release release;
             std::shared_ptr<ksana_llm::StreamingIterator> streaming_iterator;
             auto req_ctx_ptr = std::make_shared<std::unordered_map<std::string, std::string>>(req_ctx);
             ksana_llm::Status status = self->GenerateStreaming(ksana_python_input, req_ctx_ptr, streaming_iterator);
             pybind11::gil_scoped_acquire acquire;
             return std::make_tuple(status, streaming_iterator);
           })
      .def("forward", [](std::shared_ptr<ksana_llm::ServingOp> &self, const std::string &request_bytes,
                         std::unordered_map<std::string, std::string> &req_ctx) {
        pybind11::gil_scoped_release release;
        std::string response_bytes;
        auto req_ctx_ptr = std::make_shared<std::unordered_map<std::string, std::string>>(req_ctx);
        ksana_llm::Status status = self->Forward(request_bytes, req_ctx_ptr, response_bytes);
        pybind11::gil_scoped_acquire acquire;
        return std::make_tuple(status, py::bytes(response_bytes));
      });
}
