// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef OPERATION_HPP_
#define OPERATION_HPP_

#include <astl_logger.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "astl_internal_status.hpp"

namespace astl {

using SamplingInterval = std::chrono::duration<uint32_t, std::milli>;
// unsigned representation for timestamps - not suitable for differences
using SampleMicroseconds = std::chrono::duration<uint64_t, std::micro>;
using SampleTimestamp    = std::chrono::time_point<std::chrono::steady_clock, SampleMicroseconds>;

using OperationId = uint16_t;
/// Operation ID shared by both pause and resume marker samples; value payload distinguishes them
/// (0 = pause, 1 = resume).  All real metric operation IDs begin at kFirstAssignableOperationId.
constexpr OperationId kPauseResumeOperationId{0};
constexpr OperationId kFirstAssignableOperationId{kPauseResumeOperationId + 1};
constexpr size_t      kOperationIdInvalid = std::numeric_limits<OperationId>::max();

class OperationIdExhausted : public std::runtime_error {
 public:
  OperationIdExhausted()
      : std::runtime_error{
            "ASTL has run out of unique OperationIds. Configure from a clean stopped state to reset the allocator"} {}

  [[nodiscard]] static auto Status() noexcept -> astl_status_code { return kInternalOperationIdExhausted; }
};

/**
 * @brief Base class for concrete operations executed by collectors to enable or sample metrics.
 *
 * Each operation instance receives a monotonically increasing 16-bit identifier used to
 * correlate raw samples with higher-level metric processing. Operation ID 0 is reserved for
 * collector-emitted pause markers, so assignable operation IDs begin at 1. If the ID space is
 * exhausted (wraps to the sentinel invalid value) an exception is thrown because continued
 * correlation would no longer be reliable.
 */
class Operation {
 public:
  virtual ~Operation() = default;

  Operation() : operation_id{GetNextOperationId()} {}
  Operation(const Operation&)            = default;
  Operation& operator=(const Operation&) = default;
  Operation(Operation&&)                 = default;
  Operation& operator=(Operation&&)      = default;

  /**
   * @brief Get the unique identifier for this operation instance.
   * Useful for when a Collector needs to send SampledData back to Orchestrator and tie it to Metrics
   *
   * @return OperationId value (never equal to kOperationIdInvalid during a valid lifetime).
   */
  OperationId GetId() const { return operation_id; }

  /**
   * @brief Reset the process-local OperationId allocator back to the first assignable id.
   *
   * This must only be called at a global collection boundary where no configured or active
   * collection can still emit, process, or route samples using older OperationIds.
   */
  static auto ResetOperationIdAllocator() -> void {
    NextOperationId().store(kFirstAssignableOperationId, std::memory_order_relaxed);
  }

 private:
  OperationId operation_id{kOperationIdInvalid};

  static auto NextOperationId() -> std::atomic<uint32_t>& {
    static std::atomic<uint32_t> next_operation_id{kFirstAssignableOperationId};
    return next_operation_id;
  }

  /**
   * @brief Atomically fetch-and-increment the global operation ID counter.
   *
   * Since this is used in the constructor, this raises an exception if it would allocate the
   * invalid kOperationIdInvalid value. The counter saturates at kOperationIdInvalid instead of wrapping.
   *
   * @throws OperationIdExhausted if the counter reaches kOperationIdInvalid.
   */
  static OperationId GetNextOperationId() {
    auto& next_operation_id = NextOperationId();
    auto  this_id           = next_operation_id.load(std::memory_order_relaxed);
    while (true) {
      if (this_id >= kOperationIdInvalid) {
        ASTL_LOG_CRITICAL(
            "ASTL has run out of unique OperationIds. Configure from a clean stopped state to reset the allocator");
        throw OperationIdExhausted{};
      }
      if (next_operation_id.compare_exchange_weak(this_id, this_id + 1U, std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
        return static_cast<OperationId>(this_id);
      }
    }
  }
};

using OperationSequence = std::vector<std::unique_ptr<Operation>>;

struct ITarget;  // forward declaration so we can get OperationBuilder concept declared

/**
 * @brief OperationBuilder concept - interface for building operations for a given target
 */
template <typename OperationBuilderType>
concept OperationBuilder = requires(const OperationBuilderType& operation_builder, const ITarget* target) {
  { operation_builder.BuildOperations(target) } -> std::same_as<std::expected<OperationSequence, astl_status_code>>;
};

}  // namespace astl

#endif  // OPERATION_HPP_
