/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#ifndef PERIODIC_SAMPLER_HPP_
#define PERIODIC_SAMPLER_HPP_

#include <atomic>
#include <chrono>
#include <thread>

// Enable native extensions for OS-level thread priority
#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include "BS_thread_pool.hpp"  // BS::thread_pool, BS::this_thread, BS::os_thread_priority
#undef BS_THREAD_POOL_NATIVE_EXTENSIONS
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

  // Pause/Unpause the periodic sampling (skips _collector.Sample())
  void Pause() { _paused.store(true); }
  void Unpause() { _paused.store(false); }

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

}  // namespace astl

#endif  // PERIODIC_SAMPLER_HPP_