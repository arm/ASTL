// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef I_RAW_SAMPLE_SINK_HPP_
#define I_RAW_SAMPLE_SINK_HPP_

#include <chrono>
#include <span>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "common/monotonic_raw_clock.hpp"
#include "operation/operation.hpp"
#include "target.hpp"

namespace astl {

/**
 * @brief Represents a single raw (unprocessed) metric sample produced by a collector operation.
 *
 * Raw samples are produced directly from hardware/firmware (e.g. SCMI reads) before any metric-level
 * interpretation such as delta, rate, or aggregation is applied. Each raw sample is tagged with the
 * `OperationId` that initiated its collection so the metric layer can dispatch it to the correct
 * `IMetric` instance.  The `raw_tick` field usually holds the collector-native hardware clock tick
 * count; MetricManager converts it to `CLOCK_MONOTONIC_RAW` nanoseconds before forwarding to metrics.
 * Reserved pause markers are the exception: they store an already-normalized `CLOCK_MONOTONIC_RAW`
 * timestamp directly in both `raw_tick` and the raw value payload.
 *
 * Thread-safety: Once constructed the object is immutable and can be shared safely between threads
 * if the underlying `AstlValue` variant alternative is trivially copyable (the typical case).
 */
struct RawSampledData {
  static constexpr bool kSerializable{true};
  RawSampledData() = delete;

  /**
   * @brief Construct a raw sample, capturing the current time as a microsecond tick fallback.
   * @param operation_id Identifier of the originating `Operation`.
   * @param value The captured raw value.
   */
  explicit RawSampledData(OperationId operation_id, AstlValue value) : operation_id{operation_id}, value{value} {}

  /**
   * @brief Construct a raw sample with an explicit collector-native hardware tick.
   * @param operation_id Identifier of the originating `Operation`.
   * @param value The captured raw value.
   * @param raw_tick Collector-native hardware clock tick count.
   */
  RawSampledData(OperationId operation_id, AstlValue value, HwClockTicks raw_tick)
      : operation_id{operation_id}, value{value}, raw_tick{raw_tick} {}

  /**
   * @brief Construct a reserved pause-marker sample.
   *
   * The pause timestamp is stored in both `raw_tick` and the raw value payload so downstream
   * consumers inspecting serialized raw samples can identify the pause boundary without any
   * collector-specific clock correlation.
   */
  static auto PauseMarker(ProcessedSampleTimestamp pause_timestamp) -> RawSampledData {
    const auto pause_tick = static_cast<uint64_t>(pause_timestamp.time_since_epoch().count());
    return RawSampledData{kPauseOperationId, AstlValue{pause_tick}, pause_tick};
  }

  static constexpr auto IsPauseMarkerOperationId(OperationId operation_id) -> bool {
    return operation_id == kPauseOperationId;
  }

  [[nodiscard]] auto IsPauseMarker() const -> bool { return IsPauseMarkerOperationId(operation_id); }

  /** @brief Identifier of the operation that produced this sample. */
  OperationId operation_id{kOperationIdInvalid};
  /** @brief Raw value captured (pre-metric processing). */
  AstlValue value;
  /** @brief Collector-native hardware clock tick count; converted to CLOCK_MONOTONIC_RAW ns by MetricManager. */
  HwClockTicks raw_tick{0};

  /**
   * @brief Retrieve the stored raw value as a concrete type.
   * @tparam T Exact variant alternative expected.
   * @return Reference to stored value.
   */
  template <typename T>
  const auto &get() const {
    return std::get<T>(value.value);
  }
};

/**
 * @brief A raw sample after MetricManager has converted the collector-native tick to
 *        a `CLOCK_MONOTONIC_RAW` timestamp.  This is the type received by all `IMetric`
 *        implementations via `ReceiveRawSample`.
 */
struct NormalizedSampledData {
  NormalizedSampledData() = delete;

  /**
   * @brief Construct a normalized sample with the current CLOCK_MONOTONIC_RAW timestamp.
   * @param operation_id Identifier of the originating `Operation`.
   * @param value The metric value.
   */
  explicit NormalizedSampledData(OperationId operation_id, AstlValue value)
      : operation_id{operation_id}, value{value}, timestamp{ClockMonotonicRaw::now()} {}

  /**
   * @brief Construct a normalized sample with an explicit CLOCK_MONOTONIC_RAW timestamp.
   * @param operation_id Identifier of the originating `Operation`.
   * @param value The metric value.
   * @param timestamp CLOCK_MONOTONIC_RAW time_point (nanosecond resolution).
   */
  NormalizedSampledData(OperationId operation_id, AstlValue value, ProcessedSampleTimestamp timestamp)
      : operation_id{operation_id}, value{value}, timestamp{timestamp} {}

  static constexpr auto IsPauseMarkerOperationId(OperationId operation_id) -> bool {
    return operation_id == kPauseOperationId;
  }

  [[nodiscard]] auto IsPauseMarker() const -> bool { return IsPauseMarkerOperationId(operation_id); }

  /** @brief Identifier of the operation that produced this sample. */
  OperationId operation_id{kOperationIdInvalid};
  /** @brief Metric value. */
  AstlValue value;
  /** @brief CLOCK_MONOTONIC_RAW timestamp (nanosecond resolution). */
  ProcessedSampleTimestamp timestamp;

  /**
   * @brief Retrieve the stored value as a concrete type.
   * @tparam T Exact variant alternative expected.
   * @return Reference to stored value.
   */
  template <typename T>
  const auto &get() const {
    return std::get<T>(value.value);
  }
};

/* IRawSampleSink is an interface for anything that can receive raw sampled data.
 * This might include the Orchestrator, CollectorManager, or output writers, as well as test components.
 */
struct IRawSampleSink {
  virtual ~IRawSampleSink() = default;

  IRawSampleSink()                                  = default;
  IRawSampleSink(const IRawSampleSink &)            = default;
  IRawSampleSink &operator=(IRawSampleSink const &) = default;
  IRawSampleSink(IRawSampleSink &&)                 = default;
  IRawSampleSink &operator=(IRawSampleSink &&)      = default;

  /**
   * @brief Deliver one or more raw samples collected for a target.
   * @param target Target on which samples were collected (may be nullptr for global metrics).
   * @param raw_samples Span of raw samples; valid only for the duration of the call.
   * @return ASTL_STATUS_SUCCESS on success or an error status if the sink cannot consume the data.
   */
  virtual auto SinkRawSamples(const ITarget *target, std::span<RawSampledData> raw_samples) -> astl_status_code = 0;
};

}  // namespace astl

#endif  // I_RAW_SAMPLE_SINK_HPP_
