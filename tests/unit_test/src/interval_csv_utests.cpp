/*
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 */
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "astl_utils.hpp"
#include "metric/i_metric.hpp"
#include "output/interval_csv_output.hpp"
#include "target.hpp"

namespace {
// Reuse slim test doubles similar to those used in perfetto_output_utests.
class TestTargetBase : public astl::ITarget {  // NOLINT
 public:
  explicit TestTargetBase(std::string n, astl::CollectorType type = astl::CollectorType::SCMI)
      : name_(std::move(n)), collector_type_(type) {}
  auto GetCollectorType() const -> astl::CollectorType override { return collector_type_; }
  auto Name() const -> std::string const& override { return name_; }
  auto GetProperties(astl_target_properties_t* props) const -> astl_status_code override {
    if (!props) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    props->_handle = this;
    return ASTL_STATUS_SUCCESS;
  }
  size_t GetCounterCount() const override { return 0; }
  auto   GetCounters() const -> std::vector<std::unique_ptr<astl::ICounter>> const& override { return counters_; }

 private:
  std::string                                  name_;
  astl::CollectorType                          collector_type_{astl::CollectorType::SCMI};
  std::vector<std::unique_ptr<astl::ICounter>> counters_;
};

class TestMetricBase : public astl::IMetric {  // NOLINT
 public:
  explicit TestMetricBase(std::string n, astl_units_t units = ASTL_UNITS_NONE) : name_(std::move(n)), units_(units) {}
  bool CheckCapabilities(const astl::Capabilities& caps) const override {
    (void)caps;
    return true;
  }
  std::expected<astl::OperationSequence, astl_status_code> GetOperations() override {
    return astl::OperationSequence{};
  }
  astl_status_code ReceiveRawSample(const astl::RawSampledData& raw) override {
    (void)raw;
    return ASTL_STATUS_SUCCESS;
  }
  void             SetProcessedSampleSink(astl::IProcessedSampleSink* sink) override { (void)sink; }
  void             Reset() override {}
  astl_status_code Summarize() override { return ASTL_STATUS_SUCCESS; }
  astl_status_code GetProperties(astl_metric_properties_t* props) const override {
    if (!props) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    props->_handle = this;
    props->_name   = name_.c_str();
    props->_units  = units_;
    return ASTL_STATUS_SUCCESS;
  }
  auto             Name() const -> std::string const& override { return name_; }
  astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed) override {
    (void)processed;
    return ASTL_STATUS_SUCCESS;
  }

 private:
  std::string  name_;
  astl_units_t units_;
};

// Helper to make a processed sample (numeric) with deterministic timestamp
astl::ProcessedSampledData MakeSample(double value, astl::SampleTimestamp timestamp) {
  return astl::ProcessedSampledData{astl::AstlValue{static_cast<uint64_t>(value)}, timestamp};
}
}  // namespace

TEST_CASE("IntervalCsvOutput basic write", "[intervalcsv]") {  // NOLINT
  auto            path = std::filesystem::temp_directory_path() / "astl_intervalcsv_basic.csv";
  std::error_code error_code;
  std::filesystem::remove(path, error_code);
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  // build processed samples map with one metric and one sample
  TestTargetBase              target{"Soc"};
  TestMetricBase              metric{"Power", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap   processed;
  const astl::SampleTimestamp base_timestamp{};  // epoch
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
  auto            path = std::filesystem::temp_directory_path() / "astl_intervalcsv_categories.csv";
  std::error_code error_code;
  std::filesystem::remove(path, error_code);
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  TestTargetBase              tgt{"Soc"};
  TestMetricBase              m_power{"SoC Power", ASTL_UNITS_WATTS};
  TestMetricBase              m_temp{"SoC Temp", ASTL_UNITS_CELSIUS};
  astl::ProcessedSamplesMap   processed;
  const astl::SampleTimestamp base_timestamp{};
  processed[&tgt][&m_power].push_back(MakeSample(10.0, base_timestamp));
  processed[&tgt][&m_temp].push_back(MakeSample(55.0, base_timestamp + astl::SampleTimestamp::duration{5}));
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
  auto            path = std::filesystem::temp_directory_path() / "astl_intervalcsv_empty.csv";
  std::error_code error_code;
  std::filesystem::remove(path, error_code);
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  astl::ProcessedSamplesMap processed;  // empty
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // Empty map => no metric groups written
  REQUIRE(content.empty());
}

TEST_CASE("IntervalCsvOutput aggregates same metric name across targets", "[intervalcsv]") {  // NOLINT
  auto            path = std::filesystem::temp_directory_path() / "astl_intervalcsv_multi_target.csv";
  std::error_code error_code;
  std::filesystem::remove(path, error_code);
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  TestTargetBase target_a{"SocA"};
  TestTargetBase target_b{"SocB"};
  // Two distinct metric objects with identical name
  TestMetricBase              metric_a{"SharedMetric", ASTL_UNITS_WATTS};
  TestMetricBase              metric_b{"SharedMetric", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap   processed;
  const astl::SampleTimestamp base_timestamp{};
  processed[&target_a][&metric_a].push_back(MakeSample(1.0, base_timestamp));
  processed[&target_b][&metric_b].push_back(MakeSample(2.0, base_timestamp + astl::SampleTimestamp::duration{10}));
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
  auto            path = std::filesystem::temp_directory_path() / "astl_intervalcsv_quoting.csv";
  std::error_code error_code;
  std::filesystem::remove(path, error_code);
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  // Metric with description containing comma and quotes, and a string sample containing quotes
  class StringMetric : public TestMetricBase {  // NOLINT
   public:
    using TestMetricBase::TestMetricBase;
    astl_status_code GetProperties(astl_metric_properties_t* props) const override {
      auto status = TestMetricBase::GetProperties(props);
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
      static const char* desc = "Desc, with \"quotes\" inside";  // NOLINT
      props->_description     = desc;
      return ASTL_STATUS_SUCCESS;
    }
  };
  TestTargetBase              target{"Soc"};
  StringMetric                metric{"Quoted", ASTL_UNITS_NONE};
  astl::ProcessedSamplesMap   processed;
  const astl::SampleTimestamp base_timestamp{};
  // Create a string AstlValue sample with embedded quotes
  astl::ProcessedSampledData string_sample{astl::AstlValue{std::string{"value \"with\" quotes"}}, base_timestamp};
  processed[&target][&metric].push_back(string_sample);
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  // Description should be quoted and internal double quotes converted to single quotes
  REQUIRE(content.find("Quoted,\"Desc, with 'quotes' inside\"") != std::string::npos);
  // Header should include metric column
  REQUIRE(content.find("timestamp_us,target,metric,value") != std::string::npos);
  // Sample row should show sanitized string (outer quotes preserved, inner double quotes replaced)
  REQUIRE(content.find("Soc,Quoted,\"value 'with' quotes\"") != std::string::npos);
}

TEST_CASE("IntervalCsvOutput orders metric groups alphabetically", "[intervalcsv]") {  // NOLINT
  auto            path = std::filesystem::temp_directory_path() / "astl_intervalcsv_order.csv";
  std::error_code error_code;
  std::filesystem::remove(path, error_code);
  astl::IntervalCsvOutput writer(path);
  REQUIRE(writer.Ready());
  TestTargetBase              tgt{"Soc"};
  TestMetricBase              m_z{"Zeta", ASTL_UNITS_WATTS};
  TestMetricBase              m_a{"Alpha", ASTL_UNITS_WATTS};
  TestMetricBase              m_m{"Mid", ASTL_UNITS_WATTS};
  astl::ProcessedSamplesMap   processed;
  const astl::SampleTimestamp base_timestamp{};
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
