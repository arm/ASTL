// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../mock_classes.hpp"
#include "../../test_utilities.hpp"
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
