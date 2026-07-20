// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/scmi_operation_helpers.hpp"

#include <algorithm>

#include "operation/scmi_read_operation.hpp"

namespace astl::scmi_operation_helpers {

/**
 * @brief Extracts the unique SCMI data event identifiers referenced by configured operations.
 *
 * @param operations Collection operation sets to inspect.
 * @return Set of SCMI data event identifiers required for the collection.
 */
auto GetUniqueDataEventIds(CollectionOperations const& operations) -> std::unordered_set<ScmiDataEventId> {
  std::unordered_set<ScmiDataEventId> all_data_events;
  auto                                insert_unique_event_ids = [&all_data_events](const auto& operations_list) {
    for (const auto& operation : operations_list) {
      if (const auto* scmi_operation = dynamic_cast<ScmiReadOperation const*>(operation.get())) {
        all_data_events.insert(scmi_operation->scmi_data_event_id);
      }
    }
  };
  insert_unique_event_ids(operations.operationsBeforeStart);
  insert_unique_event_ids(operations.operationsAtStart);
  insert_unique_event_ids(operations.operationsOnSample);
  insert_unique_event_ids(operations.operationsAtStop);
  return all_data_events;
}

/**
 * @brief Propagates discovered SCMI timestamp rates into configured read operations.
 *
 * @param data_events Enabled data events containing timestamp metadata.
 * @param operations Collection operation sets to update in place.
 */
auto UpdateReadOperationTimestampRates(std::vector<ScmiDataEvent> const& data_events,
                                       CollectionOperations const&       operations) -> void {
  auto update_list = [&data_events](const auto& operations_list) {
    for (const auto& operation : operations_list) {
      if (auto* scmi_operation = dynamic_cast<ScmiReadOperation*>(operation.get())) {
        auto data_event_it =
            std::find_if(data_events.begin(), data_events.end(), [&scmi_operation](const ScmiDataEvent& data_event) {
              return data_event.id == scmi_operation->scmi_data_event_id;
            });
        if (data_event_it != data_events.end()) {
          scmi_operation->tstamp_rate = data_event_it->timestamp_rate.value_or(kilohertz{1});
        }
      }
    }
  };

  update_list(operations.operationsBeforeStart);
  update_list(operations.operationsAtStart);
  update_list(operations.operationsOnSample);
  update_list(operations.operationsAtStop);
}

}  // namespace astl::scmi_operation_helpers
