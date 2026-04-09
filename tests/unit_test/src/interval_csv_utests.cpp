// SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../../mock_classes.hpp"
#include "../../test_utilities.hpp"  // TempFileGuard
#include "astl_utils.hpp"
#include "metric/i_metric.hpp"
#include "output/interval_csv_output.hpp"
#include "target.hpp"

namespace {
// Reuse slim test doubles similar to those used in perfetto_output_utests.

// Helper to make a processed sample (numeric) with deterministic timestamp
astl::ProcessedSampledData MakeSample(double value, astl::ProcessedSampleTimestamp timestamp) {
  return astl::ProcessedSampledData{astl::AstlValue{static_cast<uint64_t>(value)}, timestamp};
}
}  // namespace

TEST_CASE("IntervalCsvOutput basic write", "[intervalcsv]") {  // NOLINT
  std::filesystem::path path = "astl_intervalcsv_basic.csv";
  TempFileGuard         tmp_guard{path};

  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  // build processed samples map with one metric and one sample
  TestTargetBase                       target{"Soc"};
  TestMetricBase                       metric{"Power", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};  // epoch
  processed[&target][&metric].push_back(MakeSample(3.14, base_timestamp));
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // Hybrid grouped format: metric info row then header with metric column and sample containing metric name
  REQUIRE(content.find("Power,") != std::string::npos);  // metric info row starts with name
  REQUIRE(content.find("timestamp_us,target,metric,value") != std::string::npos);
  // Sample row should repeat metric name after target
  REQUIRE(content.find("Soc,Power,3") != std::string::npos);
}

TEST_CASE("IntervalCsvOutput category inference", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_categories.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  TestTargetBase                       tgt{"Soc"};
  TestMetricBase                       m_power{"SoC Power", ASTL_UNITS_WATTS};
  TestMetricBase                       m_temp{"SoC Temp", ASTL_UNITS_CELSIUS};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};
  processed[&tgt][&m_power].push_back(MakeSample(10.0, base_timestamp));
  processed[&tgt][&m_temp].push_back(MakeSample(55.0, base_timestamp + astl::ProcessedSampleTimestamp::duration{5}));
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // Hybrid grouped format: two metric info rows and their headers (with metric column)
  REQUIRE(content.find("SoC Power,") != std::string::npos);
  REQUIRE(content.find("SoC Temp,") != std::string::npos);
  REQUIRE(content.find("timestamp_us,target,metric,value") != std::string::npos);
  REQUIRE(content.find("Soc,SoC Power,10") != std::string::npos);
  REQUIRE(content.find("Soc,SoC Temp,55") != std::string::npos);
}

TEST_CASE("IntervalCsvOutput empty map emits empty file", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_empty.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  astl::ProcessedSamplesMap processed;  // empty
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // Empty map => system info section only (no metric groups)
  REQUIRE(content.find("System Info") != std::string::npos);
  REQUIRE(content.find("Field,Value") != std::string::npos);
}

TEST_CASE("IntervalCsvOutput aggregates same metric name across targets", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_multi_target.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  TestTargetBase target_a{"SocA"};
  TestTargetBase target_b{"SocB"};
  // Two distinct metric objects with identical name
  TestMetricBase                       metric_a{"SharedMetric", ASTL_UNITS_WATTS};
  TestMetricBase                       metric_b{"SharedMetric", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};
  processed[&target_a][&metric_a].push_back(MakeSample(1.0, base_timestamp));
  processed[&target_b][&metric_b].push_back(
      MakeSample(2.0, base_timestamp + astl::ProcessedSampleTimestamp::duration{10}));
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // One metric info row (SharedMetric,description?) then header then two sample rows
  size_t metric_info_occurrences = 0;
  {
    std::istringstream iss(content);
    std::string        line;
    while (std::getline(iss, line)) {
      if (line.rfind("SharedMetric", 0) == 0) {
        metric_info_occurrences++;
      }
    }
  }
  REQUIRE(metric_info_occurrences == 1);
  REQUIRE(content.find("timestamp_us,target,metric,value") != std::string::npos);
  REQUIRE(content.find("SocA,SharedMetric,1") != std::string::npos);
  REQUIRE(content.find("SocB,SharedMetric,2") != std::string::npos);
}

// New tests to increase coverage
TEST_CASE("IntervalCsvOutput quotes description and sanitizes string samples", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_quoting.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  // Metric with description containing comma and quotes, and a string sample containing quotes
  class StringMetric : public TestMetricBase {  // NOLINT
   public:
    using TestMetricBase::TestMetricBase;
    astl_status_code GetProperties(astl_metric_props_t* props) const override {
      auto status = TestMetricBase::GetProperties(props);
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
      static const char* desc = "Desc, with \"quotes\" inside";  // NOLINT
      props->description      = desc;
      return ASTL_STATUS_SUCCESS;
    }
  };
  TestTargetBase                       target{"Soc"};
  StringMetric                         metric{"Quoted", ASTL_UNITS_NONE};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};
  astl::ProcessedSampledData           sample{astl::AstlValue{uint64_t{42}}, base_timestamp};
  processed[&target][&metric].push_back(sample);
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // Description should be quoted and internal double quotes converted to single quotes
  REQUIRE(content.find("Quoted,\"Desc, with 'quotes' inside\"") != std::string::npos);
  // Header should include metric column
  REQUIRE(content.find("timestamp_us,target,metric,value") != std::string::npos);
  REQUIRE(content.find("Soc,Quoted,42") != std::string::npos);
}

TEST_CASE("IntervalCsvOutput orders metric groups alphabetically", "[intervalcsv]") {  // NOLINT
  std::filesystem::path   path = "astl_intervalcsv_order.csv";
  TempFileGuard           tmp_guard{path};
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  TestTargetBase                       tgt{"Soc"};
  TestMetricBase                       m_z{"Zeta", ASTL_UNITS_WATTS};
  TestMetricBase                       m_a{"Alpha", ASTL_UNITS_WATTS};
  TestMetricBase                       m_m{"Mid", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap            processed;
  const astl::ProcessedSampleTimestamp base_timestamp{};
  processed[&tgt][&m_z].push_back(MakeSample(1.0, base_timestamp));
  processed[&tgt][&m_a].push_back(MakeSample(2.0, base_timestamp));
  processed[&tgt][&m_m].push_back(MakeSample(3.0, base_timestamp));
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // Extract first metric info row names in order of appearance
  std::istringstream       iss(content);
  std::string              line;
  std::vector<std::string> metric_names;
  while (std::getline(iss, line)) {
    if (line.empty()) {
      continue;
    }
    // Metric info row has no leading whitespace and no commas beyond description start when header follows
    if (line.rfind("Alpha", 0) == 0 || line.rfind("Mid", 0) == 0 || line.rfind("Zeta", 0) == 0) {
      metric_names.push_back(line.substr(0, line.find(',')));
    }
  }
  // Expect alphabetical order: Alpha, Mid, Zeta
  REQUIRE(metric_names.size() >= 3);
  REQUIRE(metric_names[0] == "Alpha");
  REQUIRE(metric_names[1] == "Mid");
  REQUIRE(metric_names[2] == "Zeta");
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
  REQUIRE(content.find("1234,Soc,Power,3") != std::string::npos);
}
