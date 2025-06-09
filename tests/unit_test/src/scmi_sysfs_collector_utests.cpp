#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/trompeloeil.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "astl/astl.h"
#include "collector/scmi_sysfs_collector.hpp"
#include "common/scmi/scmi_read_operation.hpp"

using trompeloeil::_;

// extend Catch2's to-string capabilities, so assert failures mention error codes by name rather than value
namespace Catch {

std::ostream& operator<<(std::ostream& output_stream, astl_status_code error) {
  output_stream << astlStatusString(error);
  return output_stream;
}
template <>
struct StringMaker<astl_status_code> {
  // cppcheck-suppress unusedFunction
  static std::string convert(astl_status_code error) { return astlStatusString(error); }
};
}  // namespace Catch

TEST_CASE("ScmiSysfsCollector::GetCapabilities", "[scmi_sysfs_collector]") {
  MockFileInterface                           mock_file_interface;
  astl::ScmiSysfsCollector<MockFileInterface> collector(nullptr, std::move(mock_file_interface));
  auto                                        collector_capabilities = collector.GetCapabilities();
  REQUIRE(collector_capabilities.collector_type == astl::CollectorType::SCMI);
}

TEST_CASE("ScmiSysfsCollector::ConfigureCollection - empty", "[scmi_sysfs_collector]") {
  // ensure that configuring an empty set of operations doesn't touch the file system
  MockFileInterface mock_file_interface;

  ALLOW_CALL(mock_file_interface, Read(_, _))
      .WITH(_1 == std::filesystem::path("info/de_implementation_version"))
      .SIDE_EFFECT(_2 = "0.0.0")
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, Read(_, _))
      .WITH(_1 == std::filesystem::path("info/version"))
      .SIDE_EFFECT(_2 = "0.0.1")
      .RETURN(ASTL_STATUS_SUCCESS);

  // expect collector may initialize telemetry subsystem
  ALLOW_CALL(mock_file_interface, Write(_, _))
      .WITH(_1 == std::filesystem::path("tlm_enable"), _2 == "1")
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiSysfsCollector<MockFileInterface> collector(nullptr, std::move(mock_file_interface));
  astl::CollectionOperations    operations{{}, {}, {}, {}, {}, astl::CollectorCapabilities{astl::CollectorType::SCMI}};
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
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"info/de_implementation_version"}, _))
      .SIDE_EFFECT(_2 = "0.0.0")
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(mock_file_interface, Read(std::filesystem::path{"info/version"}, _))
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
  REQUIRE_CALL(mock_file_interface, Read(std::filesystem::path{"des/0x1234/tstamp_enable"}, _))
      .IN_SEQUENCE(seq)
      .SIDE_EFFECT(_2 = "0")
      .RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(mock_file_interface, Write(std::filesystem::path{"des/0x1234/tstamp_enable"}, "1"))
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

  MockSampleSink     mock_sample_sink;
  const astl_value_t expected_value{.ui64 = 0x42};
  REQUIRE_CALL(mock_sample_sink, SinkSamples(_, _))
      .WITH(_2.size() == 1, _2[0].value.ui64 == expected_value.ui64)
      .RETURN(ASTL_STATUS_SUCCESS);

  // create the collector and its operations
  astl::ScmiSysfsCollector<MockFileInterface> collector(nullptr, std::move(mock_file_interface));
  collector.SetSampleSink(&mock_sample_sink);

  constexpr uint32_t      raw_id = 0x1234;
  astl::ScmiDataEventId   data_event_id{raw_id};
  astl::OperationSequence operations_on_sample;
  auto                    read_operation = std::make_unique<astl::ScmiReadOperation>(data_event_id);
  auto                    read_op_id     = read_operation->GetId();
  operations_on_sample.push_back(std::move(read_operation));

  astl::CollectionOperations   operations{.operationsBeforeStart{},
                                          .operationsAtStart{},
                                          .operationsOnSample{std::move(operations_on_sample)},
                                          .operationsAtStop{},
                                          .samplingInterval{},
                                          .requirements{astl::CollectorCapabilities{astl::CollectorType::SCMI}}};
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