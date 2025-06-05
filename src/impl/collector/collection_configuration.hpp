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

#ifndef COLLECTION_CONFIGURATION_HPP_
#define COLLECTION_CONFIGURATION_HPP_

#include <span>

#include "astl/astl.h"
#include "collection_operations.hpp"
#include "target.hpp"

namespace astl {

class ICollector;
class ITarget;

/*
 * @brief Current state of collection for a given set of metrics on a target.
 *
 * It can either be a collection of counters or metrics.
 * This also tracks the current state of the collection - paused, unconfigured, started, etc.
 */
class CollectionConfiguration {
 public:
  /**
   * @brief Construct a configuration from Counters
   *
   * @param[in] target                ITarget where this collection will be configured
   *
   * @param[in] counters              collection of Counters to be sampled
   *
   * @param[in] collection_params     Collection parameters structure
   **/
  CollectionConfiguration(ITarget *target, ICollector *collector, CollectionOperations collectionOperations,
                          astl_collection_parameters_t const &collection_params)
      : _target{target},
        _collector{collector},
        _operations{std::move(collectionOperations)},
        _collection_params{collection_params} {}

 private:
  ITarget    *_target    = nullptr;
  ICollector *_collector = nullptr;

  CollectionOperations _operations;

  // input from the astl API on how to collect (interval, optimization strategy, etc)
  astl_collection_parameters_t _collection_params = {0};
};

}  // namespace astl

#endif  // COLLECTION_CONFIGURATION_HPP_