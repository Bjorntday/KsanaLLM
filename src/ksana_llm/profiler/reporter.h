/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/
#pragma once

#include <map>
#include <string>
#include "ksana_llm/profiler/profiler.h"
#include "ksana_llm/profiler/timer.h"
#include "ksana_llm/utils/logger.h"
#include "ksana_llm/utils/singleton.h"

namespace ksana_llm {

// Report time.
struct TimeReporter {
  // The time unit.
  enum TimeUnit { TIME_MS, TIME_US, TIME_NS };

  TimeReporter(const std::string &name, TimeUnit time_unit);

  ~TimeReporter();

 private:
  inline time_t GetCurrentByUnit(TimeUnit time_unit);

 private:
  // The profile name.
  std::string name_;

  TimeUnit time_unit_;
  time_t start_;
};

// Report trace.
opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> ReportTrace(
    const std::string &span_name, const opentelemetry::trace::StartSpanOptions &carrier);

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> ReportTrace(
    const std::string &span_name, const HttpTextMapCarrier<const std::unordered_map<std::string, std::string>> carrier);

#define REPORT_TIME_MS(name) TimeReporter time_reporter_##name(#name, TimeReporter::TimeUnit::TIME_MS)

#define REPORT_TIME_US(name) TimeReporter time_reporter_##name(#name, TimeReporter::TimeUnit::TIME_US)

#define REPORT_TIME_NS(name) TimeReporter time_reporter_##name(#name, TimeReporter::TimeUnit::TIME_NS)

#define REPORT_METRIC(metric_name, value, ...)                                                  \
  Singleton<Profiler>::GetInstance()->monitor_.call.metric_name->Record((value), ##__VA_ARGS__, \
                                                                        opentelemetry::context::Context{})

#define REPORT_COUNTER(counter_name, value, ...)                                              \
  Singleton<Profiler>::GetInstance()->monitor_.call.counter_name->Add((value), ##__VA_ARGS__, \
                                                                      opentelemetry::context::Context{})
#define REPORT_TRACE(span_name, carrier) ReportTrace(std::string(#span_name), carrier)

}  // namespace ksana_llm
