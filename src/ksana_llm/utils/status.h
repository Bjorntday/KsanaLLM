/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/

#pragma once

#include <memory>
#include <string>

#include "ksana_llm/utils/ret_code.h"

namespace ksana_llm {

// Denote success or failure of a call in ksana_llm.
class Status {
 public:
  // Create a success status.
  Status() {}

  // Do nothing if code is RET_SUCCESS.
  explicit Status(RetCode code, const std::string &message = "");

  Status(const Status &status);

  // Return true if the status indicates success.
  bool OK() const { return (state_ == nullptr); }

  const std::string &GetMessage() const { return OK() ? GetEmptyString() : state_->message; }

  RetCode GetCode() const { return OK() ? RetCode::RET_SUCCESS : state_->code; }

  // Return a string representation of this status, return `OK` for success.
  std::string ToString() const;

 private:
  // Return this static object for better performance.
  static const std::string &GetEmptyString();

  struct State {
    RetCode code;
    std::string message;
  };

  // OK status has a `NULL` state_, Otherwise points to
  // a `State` structure containing the error code and messages.
  std::shared_ptr<State> state_ = nullptr;
};

#define STATUS_CHECK_RETURN(status)           \
  do {                                        \
    auto &&_status = (status);                \
    if (!_status.OK()) {                      \
      KLLM_LOG_ERROR << _status.GetMessage(); \
      return _status;                         \
    }                                         \
  } while (0)

#define STATUS_CHECK_FAILURE(status)          \
  do {                                        \
    auto &&_status = (status);                \
    if (!_status.OK()) {                      \
      KLLM_THROW(_status.GetMessage());       \
    }                                         \
  } while (0)

}  // namespace ksana_llm
