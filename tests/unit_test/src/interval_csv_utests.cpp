// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../mock_classes.hpp"
#include "../../test_utilities.hpp"
#include "common/system_info.hpp"
#include "output/interval_csv_output.hpp"

namespace {

auto MakeSample(double value, astl::ProcessedSampleTimestamp timestamp) -> astl::ProcessedSampledData {
  return astl::ProcessedSampledData{astl::AstlValue{value}, timestamp};
}

}  // namespace

TEST_CASE("IntervalCsvOutput writes ATX-shaped collection info and interval sections",
          "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_basic.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());

  TestTargetBase                       target{"Soc"};
  TestMetricBase                       metric{"Power", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};
  processed[&target][&metric].push_back(MakeSample(3.14, base_timestamp));

  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);

  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("ASTL Build Version,") != std::string::npos);
  REQUIRE(content.find("Collection Date/Time,") != std::string::npos);
  REQUIRE(content.find("Command Line,\"<not captured>\"") != std::string::npos);
  REQUIRE(content.find("Power on Soc") != std::string::npos);
  REQUIRE(content.find("timestamp_us,value") != std::string::npos);
  REQUIRE(content.find("0,3.14") != std::string::npos);
}

TEST_CASE("IntervalCsvOutput writes one section per metric-target pair", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_multi_section.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());

  TestTargetBase                       target_a{"SocA"};
  TestTargetBase                       target_b{"SocB"};
  TestMetricBase                       metric_a{"SharedMetric", ASTL_UNITS_WATTS};
  TestMetricBase                       metric_b{"SharedMetric", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};
  processed[&target_a][&metric_a].push_back(MakeSample(1.0, base_timestamp));
  processed[&target_b][&metric_b].push_back(
      MakeSample(2.0, base_timestamp + astl::ProcessedSampleTimestamp::duration{10'000}));

  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);

  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("SharedMetric on SocA") != std::string::npos);
  REQUIRE(content.find("SharedMetric on SocB") != std::string::npos);
  REQUIRE(content.find("0,1") != std::string::npos);
  REQUIRE(content.find("10,2") != std::string::npos);
}

TEST_CASE("IntervalCsvOutput orders sections by metric then target", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_order.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());

  TestTargetBase                       target_b{"SocB"};
  TestTargetBase                       target_a{"SocA"};
  TestMetricBase                       metric_z{"Zeta", ASTL_UNITS_WATTS};
  TestMetricBase                       metric_a{"Alpha", ASTL_UNITS_WATTS};
  TestMetricBase                       metric_a_2{"Alpha", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};
  processed[&target_b][&metric_z].push_back(MakeSample(1.0, base_timestamp));
  processed[&target_b][&metric_a].push_back(MakeSample(2.0, base_timestamp));
  processed[&target_a][&metric_a_2].push_back(MakeSample(3.0, base_timestamp));

  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);

  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  const auto alpha_a = content.find("Alpha on SocA");
  const auto alpha_b = content.find("Alpha on SocB");
  const auto zeta_b  = content.find("Zeta on SocB");
  REQUIRE(alpha_a != std::string::npos);
  REQUIRE(alpha_b != std::string::npos);
  REQUIRE(zeta_b != std::string::npos);
  REQUIRE(alpha_a < alpha_b);
  REQUIRE(alpha_b < zeta_b);
}

TEST_CASE("IntervalCsvOutput empty map emits collection info only", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_empty.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());

  astl::ProcessedSamplesMap processed;
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);

  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("ASTL Build Version,") != std::string::npos);
  REQUIRE(content.find("Collection Date/Time,") != std::string::npos);
  REQUIRE(content.find("timestamp_us,value") == std::string::npos);
  REQUIRE_FALSE(writer.Ready());
}

TEST_CASE("IntervalCsvOutput converts monotonic nanoseconds to timestamp_us", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_timestamp_units.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());

  TestTargetBase            target{"Soc"};
  TestMetricBase            metric{"Power", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap processed;
  const auto                ns_timestamp =
      astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{1'234'567}};  // 1234 us after truncation
  processed[&target][&metric].push_back(MakeSample(3.0, ns_timestamp));

  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("1234,3") != std::string::npos);
}

TEST_CASE("Archived clock reference survives replay and supplies UTC output", "[intervalcsv][wallclock]") {
  const std::filesystem::path directory = "astl_clock_reference_test";
  std::filesystem::create_directories(directory);
  struct Cleanup {
    explicit Cleanup(const std::filesystem::path& directory) : path(directory) {}
    Cleanup(const Cleanup&)                                = delete;
    auto operator=(const Cleanup&) -> Cleanup&             = delete;
    Cleanup(Cleanup&&)                                     = delete;
    auto                  operator=(Cleanup&&) -> Cleanup& = delete;
    std::filesystem::path path;
    ~Cleanup() {
      astl::ClearLoadedPlatformInfo();
      std::filesystem::remove_all(path);
    }
  } cleanup{directory};
  {
    std::ofstream metadata(directory / astl::kPlatformInfoFileName);
    metadata << R"({"wall_clock_correlation":{"raw_ns":1000,"realtime_ns":0,"uncertainty_ns":3}})";
  }
  REQUIRE(astl::LoadPlatformInfoFromCacheDir(directory) == ASTL_STATUS_SUCCESS);
  REQUIRE(astl::SavePlatformInfoToCacheDir(directory / "saved") == ASTL_STATUS_SUCCESS);
  astl::ClearLoadedPlatformInfo();
  astl::CaptureSessionWallClockCorrelation();
  REQUIRE(astl::LoadPlatformInfoFromCacheDir(directory / "saved") == ASTL_STATUS_SUCCESS);
  REQUIRE(astl::GetActiveWallClockCorrelation() == astl::WallClockCorrelation{1000, 0, 3});
  TestTargetBase            target{"Soc"};
  TestMetricBase            metric{"Power", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap processed;
  processed[&target][&metric].push_back(
      MakeSample(3.14, astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1001}}));
  {
    astl::IntervalCsvOutput writer(directory / "output.csv");
    REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  }
  std::ifstream     output(directory / "output.csv");
  const std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("timestamp_us,value,timestamp_utc\n1,3.14,1970-01-01T00:00:00.000000001Z") != std::string::npos);
  REQUIRE(content.find("Collection Date/Time,\"1970-01-01T00:00:00.000000000Z\"") != std::string::npos);
  SECTION("Legacy metadata does not inherit the analysis host reference") {
    std::ofstream(directory / astl::kPlatformInfoFileName) << "{}";
    REQUIRE(astl::LoadPlatformInfoFromCacheDir(directory) == ASTL_STATUS_SUCCESS);
    REQUIRE_FALSE(astl::GetActiveWallClockCorrelation());
  }
  SECTION("Missing metadata does not inherit the analysis host reference") {
    REQUIRE(astl::LoadPlatformInfoFromCacheDir(directory / "missing") == ASTL_STATUS_SUCCESS);
    REQUIRE_FALSE(astl::GetActiveWallClockCorrelation());
    REQUIRE_FALSE(astl::GetLoadedPlatformInfo());
  }
  {
    astl::IntervalCsvOutput legacy_writer(directory / "legacy.csv");
    REQUIRE(legacy_writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  }
  std::ifstream     legacy_output(directory / "legacy.csv");
  const std::string legacy((std::istreambuf_iterator<char>(legacy_output)), std::istreambuf_iterator<char>());
  REQUIRE(legacy.find("1,3.14,\n") != std::string::npos);
  REQUIRE(legacy.find("Collection Date/Time,\"<unknown>\"") != std::string::npos);
}

TEST_CASE("Clock metadata rejects lossy numeric types and invalid uncertainty", "[wallclock]") {
  const auto    directory = std::filesystem::temp_directory_path() / "astl_invalid_clock_metadata";
  TempFileGuard guard{directory};
  std::filesystem::create_directories(directory);
  struct RestoreHost {
    RestoreHost()                                      = default;
    RestoreHost(const RestoreHost&)                    = delete;
    auto operator=(const RestoreHost&) -> RestoreHost& = delete;
    RestoreHost(RestoreHost&&)                         = delete;
    auto operator=(RestoreHost&&) -> RestoreHost&      = delete;
    ~RestoreHost() { astl::ClearLoadedPlatformInfo(); }
  } restore;
  const auto file = directory / astl::kPlatformInfoFileName;
  std::ofstream(file) << R"({"wall_clock_correlation":{"raw_ns":1000,"realtime_ns":2000,"uncertainty_ns":3}})";
  REQUIRE(astl::LoadPlatformInfoFromCacheDir(directory) == ASTL_STATUS_SUCCESS);
  const auto original = astl::GetActiveWallClockCorrelation();
  for (const auto* invalid :
       {R"({"raw_ns":1.5,"realtime_ns":0,"uncertainty_ns":0})",
        R"({"raw_ns":0,"realtime_ns":18446744073709551615,"uncertainty_ns":0})",
        R"({"raw_ns":false,"realtime_ns":0,"uncertainty_ns":0})", R"({"raw_ns":0,"realtime_ns":0,"uncertainty_ns":-1})",
        R"({"raw_ns":0,"realtime_ns":0})", R"({"raw_ns":0,"realtime_ns":0,"uncertainty_ns":"1"})", "null", "[]"}) {
    DYNAMIC_SECTION("invalid correlation: " << invalid) {
      std::ofstream(file) << "{\"wall_clock_correlation\":" << invalid << '}';
      REQUIRE(astl::LoadPlatformInfoFromCacheDir(directory) == ASTL_STATUS_INTERNAL_ERROR);
      REQUIRE(astl::GetActiveWallClockCorrelation() == original);
    }
  }
}
