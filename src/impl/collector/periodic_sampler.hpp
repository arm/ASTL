// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PERIODIC_SAMPLER_HPP_
#define PERIODIC_SAMPLER_HPP_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

// Enable native extensions for OS-level thread priority
#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include "BS_thread_pool.hpp"  // BS::thread_pool, BS::this_thread, BS::os_thread_priority
#undef BS_THREAD_POOL_NATIVE_EXTENSIONS
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"

namespace astl {

struct ICollector;

/** @brief manages a background thread to periodically sample from the given collector.
 *         Helper class to be used by Collectors or CollectorManager
 */
class PeriodicSampler {
 public:
  PeriodicSampler(ICollector* collector, std::chrono::milliseconds interval)
      : _collector{collector},
        _interval{interval},
        _cancel{false},
        _paused{false},
        _pool{1, [](std::size_t /*idx*/) {
                bool result = BS::this_thread::set_os_thread_priority(BS::os_thread_priority::highest);
                ASTL_LOG_DEBUG("Setting thread priority for periodic sampler {}", +result ? "succeeded" : "failed");
              }} {
    // Kick off the long-running loop as a detached task:
    _pool.detach_task([this] { Loop(); });
  }
  // forbid copy
  PeriodicSampler(PeriodicSampler const&)            = delete;
  PeriodicSampler& operator=(PeriodicSampler const&) = delete;
  PeriodicSampler(PeriodicSampler&&)                 = delete;
  PeriodicSampler& operator=(PeriodicSampler&&)      = delete;

  ~PeriodicSampler() { Cancel(); }

  // Signal cancellation; loop() will exit shortly thereafter.
  void Cancel() { _cancel.store(true); }

  // Pause/Resume the periodic sampling (skips _collector.Sample())
  void Pause() { _paused.store(true); }
  void Resume() { _paused.store(false); }

 private:
  void Loop() {
    auto now               = std::chrono::steady_clock::now();
    auto next_sample_point = now + _interval;
    while (!_cancel.load()) {
      if (!_paused.load()) {
        _collector->ReadImmediate();
      }
      std::this_thread::sleep_until(next_sample_point);
      next_sample_point += _interval;
    }
  }

  ICollector*                     _collector;
  const std::chrono::milliseconds _interval;
  std::atomic<bool>               _cancel, _paused;
  BS::thread_pool<BS::tp::none>   _pool;
};

template <typename CollectorT>
auto StartPeriodicSamplerForCollector(CollectorT* collector, CollectionConfiguration const& configuration,
                                      std::unique_ptr<PeriodicSampler>& periodic_sampler, std::string_view log_prefix)
    -> astl_status_code {
  if (periodic_sampler) {
    ASTL_LOG_ERROR("{} started _periodic_sampler is already initialized", log_prefix);
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto interval = std::chrono::milliseconds{configuration.CollectionParams().sampling_interval};
  periodic_sampler    = std::make_unique<PeriodicSampler>(collector, interval);
  return ASTL_STATUS_SUCCESS;
}

template <typename CollectionStateT>
auto CheckPeriodicSamplerStart(CollectionStateT                              collection_state,
                               std::optional<CollectionConfiguration> const& configuration,
                               std::initializer_list<CollectionStateT> startable_states, std::string_view log_prefix)
    -> astl_status_code {
  if (std::ranges::find(startable_states, collection_state) == startable_states.end()) {
    ASTL_LOG_ERROR("{} started when collection state is not configured, stopped, or paused", log_prefix);
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!configuration.has_value()) {
    ASTL_LOG_ERROR("{} start attempted with no configuration!", log_prefix);
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl

#endif  // PERIODIC_SAMPLER_HPP_
