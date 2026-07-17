// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_OPERATION_HELPERS_HPP_
#define SCMI_OPERATION_HELPERS_HPP_

#include <unordered_set>
#include <vector>

#include "collector/collection_operations.hpp"
#include "collector/scmi_data_event.hpp"

namespace astl::scmi_operation_helpers {

/**
 * @brief Extracts the unique SCMI data event identifiers referenced by configured operations.
 *
 * @param operations Collection operation sets to inspect.
 * @return Set of SCMI data event identifiers required for the collection.
 */
auto GetUniqueDataEventIds(CollectionOperations const& operations) -> std::unordered_set<ScmiDataEventId>;

/**
 * @brief Propagates discovered SCMI timestamp rates into configured read operations.
 *
 * @param data_events Enabled data events containing timestamp metadata.
 * @param operations Collection operation sets to update in place.
 */
auto UpdateReadOperationTimestampRates(std::vector<ScmiDataEvent> const& data_events,
                                       CollectionOperations const&       operations) -> void;

}  // namespace astl::scmi_operation_helpers

#endif  // SCMI_OPERATION_HELPERS_HPP_
