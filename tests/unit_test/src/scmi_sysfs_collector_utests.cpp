#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "collector/scmi_sysfs_collector.hpp"
#include "operation/scmi_read_operation.hpp"

using namespace std::chrono_literals;

using trompeloeil::_;

// extend Catch2's to-string capabilities, so assert failures mention error codes by name rather than value
namespace Catch {

std::ostream& operator<<(std::ostream& output_stream, astl_status_code error) {
  output_stream << astlStatusString(error);
  return output_stream;
}

}  // namespace Catch

TEST_CASE("ScmiSysfsCollector::GetCapabilities", "[scmi_sysfs_collector]") {
  MockFileInterface                           mock_file_interface;
  astl::ScmiSysfsCollector<MockFileInterface> collector(std::move(mock_file_interface));
  auto                                        collector_capabilities = collector.GetCapabilities();
  REQUIRE(collector_capabilities.collector_type == astl::CollectorType::SCMI);
}

TEST_CASE("ScmiSysfsCollector::ConfigureCollection - empty", "[scmi_sysfs_collector]") {
  // ensure that configuring an empty set of operations doesn't touch the file system
  MockFileInterface mock_file_interface;

  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path("de_implementation_version"), _))
      .SIDE_EFFECT(_2 = "0.0.0")
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path("version"), _))
      .SIDE_EFFECT(_2 = "0.0.1")
      .RETURN(ASTL_STATUS_SUCCESS);

  // expect collector may initialize telemetry subsystem
  ALLOW_CALL(mock_file_interface, Write(std::filesystem::path("tlm_enable"), "1")).RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiSysfsCollector<MockFileInterface> collector(std::move(mock_file_interface));
  astl::CollectionOperations    operations{{}, {}, {}, {}, {}, astl::CollectorCapability{astl::CollectorType::SCMI}};
  astl_collection_parameters_t  collection_params{};
  astl::CollectionConfiguration configuration{nullptr, std::move(operations), collection_params};
  REQUIRE(ASTL_STATUS_SUCCESS == collector.ConfigureCollection(std::move(configuration)));
}

/* In this test we'll be enabling one SCMI data event, reading it, and then stopping the collection.
 * We expect the collector to read the "enable" file for the data event, write "1" to it, and then read the
 * "tstamp_enable" file to determine how to parse timestamps.
 * Then it'll read the "value" file once before writing a "0" back to the enable file.
 */
TEST_CASE("ScmiSysfsCollector::ConfigureAndStart - one", "[scmi_sysfs_collector]") {
  // ensure that configuring an empty set of operations doesn't touch the file system
  MockFileInterface mock_file_interface;
  // assume very friendly file interface
  ALLOW_CALL(mock_file_interface, IsValid(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, HasWritePermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, HasReadPermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"de_implementation_version"}, _))
      .SIDE_EFFECT(_2 = "0.0.0")
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"version"}, _))
      .SIDE_EFFECT(_2 = "0.0.1")
      .RETURN(ASTL_STATUS_SUCCESS);

  trompeloeil::sequence seq;
  // expect collector to initialize telemetry subsystem
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"tlm_enable"}, "1"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  // initially, data event 0x1234 is disabled.
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/enable"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "0")
      .RETURN(ASTL_STATUS_SUCCESS);
  // collector should enable data event 1234
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x1234/enable"}, "1"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  // collector should enable timestamps on data event 1234
  REQUIRE_CALL(mock_file_interface, IsValid(std::filesystem::path{"des/0x1234/tstamp_enable"}))
      .IN_SEQUENCE(seq)
      .RETURN(true);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/tstamp_enable"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "0")
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x1234/tstamp_enable"}, "1"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  // collector should read the value
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/value"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "1234567890 42")  // example value with timestamp
      .RETURN(ASTL_STATUS_SUCCESS);
  // finally, collector should disable timestamps and data for event 1234
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x1234/tstamp_enable"}, "0"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x1234/enable"}, "0"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);

  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/tstamp_rate"}, _))
      .SIDE_EFFECT(_2 = "1")
      .RETURN(ASTL_STATUS_SUCCESS);

  MockRawSampleSink     mock_raw_sample_sink;
  const astl::AstlValue expected_value{uint64_t{0x42}};
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      .WITH(_2.size() == 1)
      .WITH(_2[0].value == expected_value)
      .RETURN(ASTL_STATUS_SUCCESS);

  // create the collector and its operations
  astl::ScmiSysfsCollector<MockFileInterface> collector(std::move(mock_file_interface));
  collector.SetRawSampleSink(&mock_raw_sample_sink);

  constexpr uint32_t      raw_id = 0x1234;
  astl::ScmiDataEventId   data_event_id{raw_id};
  astl::OperationSequence operations_on_sample;
  auto                    read_operation = std::make_unique<astl::ScmiReadOperation>(data_event_id, astl::kilohertz{1});
  operations_on_sample.push_back(std::move(read_operation));

  astl::CollectionOperations   operations{.operationsBeforeStart{},
                                          .operationsAtStart{},
                                          .operationsOnSample{std::move(operations_on_sample)},
                                          .operationsAtStop{},
                                          .samplingInterval{},
                                          .requirements{astl::CollectorCapability{astl::CollectorType::SCMI}}};
  astl_collection_parameters_t collection_params{
      ._size              = sizeof(astl_collection_parameters_t),
      ._sampling_interval = 0,
      ._collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
      ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD,
  };
  astl::CollectionConfiguration configuration{nullptr, std::move(operations), collection_params};
  // configure the collector, and perform the collection
  REQUIRE(ASTL_STATUS_SUCCESS == collector.ConfigureCollection(std::move(configuration)));
  REQUIRE(ASTL_STATUS_SUCCESS == collector.StartCollection());
  REQUIRE(ASTL_STATUS_SUCCESS == collector.ReadImmediate());
  REQUIRE(ASTL_STATUS_SUCCESS == collector.StopCollection());
}

/* In this test we enable periodic sampling, exercising the 'happy path'.
 * We expect the collector to read the "enable" file for the data event, write "1" to it, and then read the
 * "tstamp_enable" file to determine how to parse timestamps.
 * Then it'll read the "value" file once before writing a "0" back to the enable file.
 */
TEST_CASE("ScmiSysfsCollector::ConfigureAndStart - Sampling", "[scmi_sysfs_collector][time_sensitive]") {
  // ensure that configuring an empty set of operations doesn't touch the file system
  MockFileInterface mock_file_interface;
  // assume very friendly file interface
  ALLOW_CALL(mock_file_interface, IsValid(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, HasWritePermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, HasReadPermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"de_implementation_version"}, _))
      .SIDE_EFFECT(_2 = "0.0.0")
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"version"}, _))
      .SIDE_EFFECT(_2 = "0.0.1")
      .RETURN(ASTL_STATUS_SUCCESS);

  trompeloeil::sequence seq;
  // expect collector to initialize telemetry subsystem
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"tlm_enable"}, "1"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  // initially, data event 0x1234 is disabled.
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/enable"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "0")
      .RETURN(ASTL_STATUS_SUCCESS);
  // collector should enable data event 1234
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x1234/enable"}, "1"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  // initially, data event 0x1234 has timestamp enabled for this test
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/tstamp_enable"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "1")
      .RETURN(ASTL_STATUS_SUCCESS);

  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/tstamp_rate"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "1")
      .RETURN(ASTL_STATUS_SUCCESS);

  // collector should read the value
  size_t                         read_value_call_count{0};
  const std::vector<std::string> expected_data{"1234567890 10", "1234567891 11", "1234567892 12", "1234567893 13",
                                               "1234567894 14", "1234567895 15", "1234567896 16", "1234567897 17",
                                               "1234567898 18", "1234567899 19"};
  // we expect some number of calls to this value read function, depending on how long
  // we leave the collection enabled. return some of the expected data, and don't overrun that list.
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/value"}, _))
      .IN_SEQUENCE(seq)
      .TIMES(10)
      .LR_SIDE_EFFECT(_2 = expected_data[std::min(read_value_call_count, expected_data.size() - 1)],
                      ++read_value_call_count)
      .RETURN(ASTL_STATUS_SUCCESS);

  // if over-sampling, return an error and increase the count
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/value"}, _))
      .IN_SEQUENCE(seq)
      .LR_SIDE_EFFECT(++read_value_call_count)
      .RETURN(ASTL_STATUS_FILE_ERROR);

  // finally, collector should disable timestamps and data for event 1234
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x1234/enable"}, "0"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);

  MockRawSampleSink            mock_raw_sample_sink;
  const std::vector<uint64_t>  expected_raw_data_samples{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19};
  std::vector<astl::AstlValue> expected_samples;
  expected_samples.reserve(expected_raw_data_samples.size());
  std::transform(expected_raw_data_samples.begin(), expected_raw_data_samples.end(),
                 std::back_inserter(expected_samples),
                 [](const auto& raw_value) { return astl::AstlValue{uint64_t{raw_value}}; });
  std::vector<astl::AstlValue> samples;
  // each time SinkRawSamples is called, we push all of the values of the samples to our local `samples` vector
  // in the end, `samples` should match expected_samples, regardless of how many samples come in each call to
  // SinkRawSamples
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      // extra parens needed for proper macro parse, letting us mutate `samples`
      .TIMES(10)
      .LR_SIDE_EFFECT((std::for_each(std::begin(_2), std::end(_2),
                                     [&samples](auto const& sample) { samples.push_back(sample.value); })))
      .RETURN(ASTL_STATUS_SUCCESS);

  // ALLOW_CALL(mock_raw_sample_sink, SinkRawSamples(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  // create the collector and its operations
  astl::ScmiSysfsCollector<MockFileInterface> collector(std::move(mock_file_interface));
  collector.SetRawSampleSink(&mock_raw_sample_sink);

  constexpr uint32_t      raw_id = 0x1234;
  astl::ScmiDataEventId   data_event_id{raw_id};
  astl::OperationSequence operations_on_sample;
  auto                    read_operation = std::make_unique<astl::ScmiReadOperation>(data_event_id, astl::kilohertz{1});
  operations_on_sample.push_back(std::move(read_operation));

  astl::CollectionOperations operations{.operationsBeforeStart{},
                                        .operationsAtStart{},
                                        .operationsOnSample{std::move(operations_on_sample)},
                                        .operationsAtStop{},
                                        .samplingInterval{},
                                        .requirements{astl::CollectorCapability{astl::CollectorType::SCMI}}};

  constexpr uint32_t sampling_interval_ms = 50;
  constexpr auto     test_duration        = 500ms;
  constexpr size_t   expected_call_count  = test_duration.count() / sampling_interval_ms;

  astl_collection_parameters_t collection_params{
      ._size              = sizeof(astl_collection_parameters_t),
      ._sampling_interval = sampling_interval_ms,  // sample every 50 ms
      ._collection_mode   = ASTL_COLLECTION_MODE_SAMPLING,
      ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD,
  };
  astl::CollectionConfiguration configuration{nullptr, std::move(operations), collection_params};
  // configure the collector, and perform the collection
  REQUIRE(ASTL_STATUS_SUCCESS == collector.ConfigureCollection(std::move(configuration)));
  REQUIRE(ASTL_STATUS_SUCCESS == collector.StartCollection());
  std::this_thread::sleep_for(test_duration);  // 10x the sample interval
  REQUIRE(ASTL_STATUS_SUCCESS == collector.StopCollection());
  REQUIRE(read_value_call_count >= expected_call_count);
  // may get an some extra samples due to OS scheduling jitters
  constexpr auto allowed_extra_samples{3};
  REQUIRE(read_value_call_count <= expected_call_count + allowed_extra_samples);
  REQUIRE_THAT(samples, Catch::Matchers::Equals(expected_samples));
}

TEST_CASE("ScmiSysfsCollector::TstampRateScaling", "[scmi_sysfs_collector]") {
  // test that the collector scales timestamps for us based on tstamp_rate, which indicates the rate in KHz that the
  // tstamps are updated
  MockFileInterface mock_file_interface;

  // Allow basic file interface operations
  ALLOW_CALL(mock_file_interface, HasWritePermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, HasReadPermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"de_implementation_version"}, _))
      .SIDE_EFFECT(_2 = "0.0.0")
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"version"}, _))
      .SIDE_EFFECT(_2 = "0.0.1")
      .RETURN(ASTL_STATUS_SUCCESS);

  trompeloeil::sequence seq;

  // Expect collector to initialize telemetry subsystem
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"tlm_enable"}, "1"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);

  // Data event 0x5678 setup
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x5678/enable"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "0")
      .RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x5678/enable"}, "1"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, IsValid(std::filesystem::path{"des/0x5678/tstamp_enable"}))
      .IN_SEQUENCE(seq)
      .RETURN(false);  // for this test, assume no timestamp enable file exists

  // Test data with duplicate timestamps
  size_t read_value_call_count{0};
  // this test_data increments the timestamps by 1000x. The collector should properly scale these timestamps based on
  // the tstamp_rate
  const std::vector<std::string> test_data{
      "1000 100",  // First sample: timestamp=1000, value=100, tstamp_rate=4, so timestamp should be scaled to 250ms
      "2000 200",  // Different timestamp: should be accepted. timestamp=2000, value=200, scaled to 500ms
      "3000 300", "4000 400", "5000 500"};

  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x5678/value"}, _))
      .IN_SEQUENCE(seq)
      .TIMES(5)
      .LR_SIDE_EFFECT(_2 = test_data[std::min(read_value_call_count, test_data.size() - 1)], ++read_value_call_count)
      .RETURN(ASTL_STATUS_SUCCESS);

  // Cleanup calls
  // tstamp_enable should _not_ be written
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x5678/enable"}, "0"))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);

  // timestamps count up at 4KHz
  ALLOW_CALL(mock_file_interface, IsValid(std::filesystem::path{"des/0x5678/tstamp_rate"})).RETURN(true);
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x5678/tstamp_rate"}, _))
      .SIDE_EFFECT(_2 = "4")
      .RETURN(ASTL_STATUS_SUCCESS);

  // Track samples received by the sink
  MockRawSampleSink                  mock_raw_sample_sink;
  std::vector<astl::AstlValue>       received_samples;
  std::vector<astl::SampleTimestamp> received_timestamps;

  // We expect only 3 samples to be received (duplicate timestamps should be discarded)
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      .TIMES(5)
      .LR_SIDE_EFFECT((std::for_each(std::begin(_2), std::end(_2),
                                     [&received_samples, &received_timestamps](auto const& sample) {
                                       received_samples.push_back(sample.value);
                                       received_timestamps.push_back(sample.timestamp);
                                     })))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Create the collector and configure it
  astl::ScmiSysfsCollector<MockFileInterface> collector(std::move(mock_file_interface));
  collector.SetRawSampleSink(&mock_raw_sample_sink);

  constexpr uint32_t      raw_id = 0x5678;
  astl::ScmiDataEventId   data_event_id{raw_id};
  astl::OperationSequence operations_on_sample;
  auto                    read_operation = std::make_unique<astl::ScmiReadOperation>(data_event_id, astl::kilohertz{1});
  operations_on_sample.push_back(std::move(read_operation));

  astl::CollectionOperations operations{.operationsBeforeStart{},
                                        .operationsAtStart{},
                                        .operationsOnSample{std::move(operations_on_sample)},
                                        .operationsAtStop{},
                                        .samplingInterval{},
                                        .requirements{astl::CollectorCapability{astl::CollectorType::SCMI}}};

  astl_collection_parameters_t collection_params{
      ._size              = sizeof(astl_collection_parameters_t),
      ._sampling_interval = 1,
      ._collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
      ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD,
  };

  astl::CollectionConfiguration configuration{nullptr, std::move(operations), collection_params};

  // Configure and start collection
  REQUIRE(ASTL_STATUS_SUCCESS == collector.ConfigureCollection(std::move(configuration)));
  REQUIRE(ASTL_STATUS_SUCCESS == collector.StartCollection());

  // Perform 5 immediate reads
  for (int i = 0; i < 5; ++i) {
    REQUIRE(ASTL_STATUS_SUCCESS == collector.ReadImmediate());
  }

  REQUIRE(ASTL_STATUS_SUCCESS == collector.StopCollection());

  // Verify that samples were received with scaled timestamps
  // Expected: 100 (first), 300 (after first duplicate), 500 (after second duplicate)
  const std::vector<astl::AstlValue> expected_samples{
      astl::AstlValue{uint64_t{0x100}}, astl::AstlValue{uint64_t{0x200}}, astl::AstlValue{uint64_t{0x300}},
      astl::AstlValue{uint64_t{0x400}}, astl::AstlValue{uint64_t{0x500}}};
  REQUIRE_THAT(received_samples, Catch::Matchers::Equals(expected_samples));

  const std::vector<astl::SampleTimestamp> expected_timestamps{
      astl::SampleTimestamp{250ms}, astl::SampleTimestamp{500ms}, astl::SampleTimestamp{750ms},
      astl::SampleTimestamp{1000ms}, astl::SampleTimestamp{1250ms}};

  REQUIRE_THAT(received_timestamps, Catch::Matchers::Equals(expected_timestamps));
}
