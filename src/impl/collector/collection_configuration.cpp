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

#include "collector/collection_configuration.hpp"

#include "common/operation.hpp"
#include "counter.hpp"
#include "target.hpp"

namespace astl {

CollectionConfiguration::CollectionConfiguration(ITarget *target, ICollector *collector,
                                                 CollectionOperations                collectionOperations,
                                                 astl_collection_parameters_t const &collection_params)
    : _target{target},
      _collector{collector},
      _operations{std::move(collectionOperations)},
      _collection_params{collection_params} {}

}  // namespace astl
