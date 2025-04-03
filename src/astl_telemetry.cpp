#include "astl/astl.h"
#include "astl_impl.h"

/***********************************************************************************
 **********************               TARGETS               ************************
 **********************************************************************************/

astl_error_code astlGetTargetCount(uint32_t* target_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetTargets(astl_target_properties_t* targets, uint32_t target_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/***********************************************************************************
 **********************              COUNTER                   *********************
 **********************************************************************************/

astl_error_code astlGetCounterCount(astl_target_handle_t target_handle, uint32_t* counter_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetCounters(astl_target_handle_t target_handle, astl_counter_properties_t* counters,
                                uint32_t* counter_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/***********************************************************************************
 **********************              METRIC                    *********************
 **********************************************************************************/

astl_error_code astlGetMetricCount(astl_target_handle_t target_handle, uint32_t* metric_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetMetrics(astl_target_handle_t target_handle, astl_metric_properties_t* metrics,
                               uint32_t* metric_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/***********************************************************************************
 **********************              METRIC GROUPS             *********************
 **********************************************************************************/

astl_error_code astlGetMetricGroupCount(astl_target_handle_t target_handle, uint32_t* metric_group_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetMetricGroups(astl_target_handle_t target_handle, astl_metric_group_properties_t* metric_groups,
                                    uint32_t* metric_group_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetMetricGroupMetrics(astl_target_handle_t            target_handle,
                                          astl_metric_group_properties_t* metric_groups,
                                          astl_metric_properties_t*       metrics) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/***********************************************************************************
 **********************              COLLECTION                *********************
 **********************************************************************************/

/*** CONFIGURE COUNTERS ***/
astl_error_code astlConfigureCounterCollectionOnTarget(astl_target_handle_t         target_handle,
                                                       astl_collection_parameters_t collection_params,
                                                       astl_counter_handle_t* counter_handles, uint32_t counter_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlConfigureCounterCollection(astl_collection_parameters_t collection_params,
                                               astl_counter_handle_t* counter_handles, uint32_t counter_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/*** CONFIGURE METRICS ***/
astl_error_code astlConfigureMetricCollectionOnTarget(astl_target_handle_t         target_handle,
                                                      astl_collection_parameters_t collection_params,
                                                      astl_metric_handle_t* metric_handles, uint32_t metric_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlConfigureMetricCollection(astl_collection_parameters_t collection_params,
                                              astl_metric_handle_t* metric_handles, uint32_t metric_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/*** CONFIGURE METRIC GROUPS ***/
astl_error_code astlConfigureMetricGroupCollectionOnTarget(astl_target_handle_t         target_handle,
                                                           astl_collection_parameters_t collection_params,
                                                           astl_metric_group_handle_t*  metric_group_handles,
                                                           uint32_t                     metric_group_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlConfigureMetricGroupCollection(astl_collection_parameters_t collection_params,
                                                   astl_metric_group_handle_t*  metric_group_handles,
                                                   uint32_t                     metric_group_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlReadImmediateOnTarget(astl_target_handle_t target_handle) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlReadImmediate() {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlStartCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlStartCollection() {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlPauseCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlPauseCollection() {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlResumeCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlResumeCollection() {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlStopCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlStopCollection() {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/*** COLLECTED COUNTER SAMPLES ***/
astl_error_code astlGetCounterSampleCountOnTarget(astl_target_handle_t  target_handle,
                                                  astl_counter_handle_t counter_handle, uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle,
                                              astl_counter_sample_t* samples, uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllCounterSampleCountOnTarget(astl_target_handle_t target_handle, uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_sample_t* samples,
                                                 uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllCounterSampleCount(uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllCounterSamples(astl_counter_sample_t* samples, uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/*** COLLECTED METRIC SAMPLES ***/
astl_error_code astlGetMetricSampleCountOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                                 uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetMetricSamplesOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                             astl_metric_sample_t* samples, uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllMetricSampleCountOnTarget(astl_target_handle_t target_handle, uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllMetricSamplesOnTarget(astl_target_handle_t target_handle, astl_metric_sample_t* samples,
                                                uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllMetricSampleCount(uint32_t* metric_sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

astl_error_code astlGetAllMetricSamples(astl_metric_sample_t* metric_samples, uint32_t* sample_count) {
  astl_error_code result;
  // TODO: Implement
  return result;
}

/***********************************************************************************
 **********************              TEST                      *********************
 **********************************************************************************/
// TODO: Delete
astl_error_code astlTest() {
  astl_error_code result = AstlCollectorInstance().Test();
  return result;
}
