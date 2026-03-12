// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef COLLECTION_CONFIGURATION_HPP_
#define COLLECTION_CONFIGURATION_HPP_

#include "astl/astl.h"
#include "collection_operations.hpp"
#include "target.hpp"

namespace astl {

/*
 * @brief Current state of collection for a given set of metrics on a target.
 *
 * It can either be a collection of counters or metrics.
 * This also tracks the current state of the collection - paused, unconfigured, started, etc.
 */
class CollectionConfiguration {
 public:
  /**
   * @brief Construct a configuration of operations to collect and how to collect them
   *
   * @param[in] target                Target where this collection will be configured
   *
   * @param[in] collectionOperations  Groups of operations to apply at different phases of collection
   *
   * @param[in] collection_params     Collection parameters (interval, strategy, etc)
   **/
  CollectionConfiguration(const ITarget* target, CollectionOperations collectionOperations,
                          astl_collection_params_t const& collection_params)
      : _target{target}, _operations{std::move(collectionOperations)}, _collection_params{collection_params} {}

  const ITarget* Target() const { return _target; }

  CollectionOperations const& Operations() const { return _operations; }

  astl_collection_params_t CollectionParams() const { return _collection_params; }

 private:
  const ITarget* _target = nullptr;

  CollectionOperations _operations;

  // input from the astl API on how to collect (interval, optimization strategy, etc)
  astl_collection_params_t _collection_params = {0, 0, 0, ASTL_COLLECTION_MODE_IMMEDIATE};
};

}  // namespace astl

#endif  // COLLECTION_CONFIGURATION_HPP_
