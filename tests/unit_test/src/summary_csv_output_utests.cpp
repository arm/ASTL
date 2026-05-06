// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../../test_includes.hpp"  // must come first before any Catch2 usage
#include "../../test_utilities.hpp"
#include "astl_utils.hpp"
#include "common/capabilities.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "metric/i_metric.hpp"
#include "output/output_manager.hpp"
#include "output/summarizer.hpp"
#include "output/summary_csv_output.hpp"
#include "target.hpp"

namespace {

// Minimal mock target + metric to populate ProcessedSamplesMap without depending on full mocks (lighter weight)
struct SummaryTestTarget : public astl::ITarget {
  std::string         name{"T0"};
  astl::CollectorType collector_type{astl::CollectorType::SCMI};
  SummaryTestTarget() = default;
  explicit SummaryTestTarget(std::string target_name) : name(std::move(target_name)) {}
  auto GetCollectorType() const -> astl::CollectorType override { return collector_type; }
  auto Name() const -> std::string const& override { return name; }
  auto GetProperties(astl_target_props_t* props) const -> astl_status_code override {
    if (!props) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    props->handle = this;
    return ASTL_STATUS_SUCCESS;
  }
};

struct SummaryTestMetric : public astl::IMetric {
  std::string        name{"M0"};
  std::string        description{"Test metric description"};
  astl_units_t       units{ASTL_UNITS_UNKNOWN};
  astl_value_type_t  value_type{ASTL_VALUE_FLOAT64};
  astl_metric_type_t metric_type{ASTL_METRIC_VALUE};
  SummaryTestMetric() = default;
  SummaryTestMetric(std::string metric_name, std::string metric_description, astl_units_t metric_units,
                    astl_value_type_t  metric_value_type  = ASTL_VALUE_FLOAT64,
                    astl_metric_type_t metric_metric_type = ASTL_METRIC_VALUE)
      : name(std::move(metric_name)),
        description(std::move(metric_description)),
        units(metric_units),
        value_type(metric_value_type),
        metric_type(metric_metric_type) {}
  bool CheckCapabilities(const astl::Capabilities& caps) const override {
    (void)caps;
    return true;
  }
  std::expected<astl::OperationSequence, astl_status_code> GetOperations() override {
    return astl::OperationSequence{};
  }
  astl_status_code ReceiveRawSample(const astl::NormalizedSampledData& sample) override {
    (void)sample;
    return ASTL_STATUS_SUCCESS;
  }
  void             SetProcessedSampleSink(astl::IProcessedSampleSink* sink) override { (void)sink; }
  void             Reset() override {}
  astl_status_code Summarize() override { return ASTL_STATUS_SUCCESS; }
  astl_status_code GetProperties(astl_metric_props_t* props) const override {
    if (!props) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    props->name        = name.c_str();
    props->description = description.c_str();
    props->units       = units;
    props->value_type  = value_type;
    props->metric_type = metric_type;
    return ASTL_STATUS_SUCCESS;
  }
  auto             Name() const -> std::string const& override { return name; }
  astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
    (void)processed_sample;
    return ASTL_STATUS_SUCCESS;
  }
  astl_status_code ProcessPauseSample(astl::ProcessedSampleTimestamp pause_timestamp) override {
    (void)pause_timestamp;
    return ASTL_STATUS_SUCCESS;
  }
};

// Helper to create test samples with specific values for summary testing
std::vector<astl::ProcessedSampledData> MakeSamplesWithValues(const std::vector<double>& values) {
  std::vector<astl::ProcessedSampledData> samples_vec;
  samples_vec.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    samples_vec.emplace_back(
        astl::AstlValue{values[i]},
        astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100 + static_cast<int>(i)}});
  }
  return samples_vec;
}

std::vector<astl::ProcessedSampledData> MakeSamplesWithBoolValues(const std::vector<bool>& values) {
  std::vector<astl::ProcessedSampledData> samples_vec;
  samples_vec.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    samples_vec.emplace_back(
        astl::AstlValue{values[i]},
        astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100 + static_cast<int>(i)}});
  }
  return samples_vec;
}

// Helper to create processed samples map with multiple targets and metrics
astl::ProcessedSamplesMap CreateTestProcessedSamplesMap() {
  // Create targets in a vector to ensure predictable order
  static std::vector<SummaryTestTarget> targets{SummaryTestTarget{"Target1"}, SummaryTestTarget{"Target2"}};
  static std::vector<SummaryTestMetric> metrics{
      {"Temperature", "Board temperature", ASTL_UNITS_CELSIUS},
      {"Voltage",     "Rail voltage",      ASTL_UNITS_VOLTS  },
  };

  astl::ProcessedSamplesMap processed_samples;

  // Target1 - Temperature: values 10.0, 20.0, 30.0
  processed_samples[targets.data()][metrics.data()] = MakeSamplesWithValues({10.0, 20.0, 30.0});

  // Target1 - Voltage: values 3.3, 3.4, 3.5
  processed_samples[targets.data()][&metrics[1]] = MakeSamplesWithValues({3.3, 3.4, 3.5});

  // Target2 - Temperature: values 15.0, 25.0, 35.0
  processed_samples[&targets[1]][metrics.data()] = MakeSamplesWithValues({15.0, 25.0, 35.0});

  // Target2 - Voltage: values 5.0, 5.1, 5.2
  processed_samples[&targets[1]][&metrics[1]] = MakeSamplesWithValues({5.0, 5.1, 5.2});

  return processed_samples;
}

}  // namespace

TEST_CASE("OutputManager::OutputProcessedSamples SUMMARY_CSV mode error paths",
          "[output_manager][csv_summary]") {  // NOLINT
  astl::OutputManager       mgr;
  SummaryTestTarget         target;
  SummaryTestMetric         metric;
  astl::ProcessedSamplesMap processed_samples;

  SECTION("SUMMARY_CSV mode without ASTL_OUTPUT_SUMMARY_CSV environment variable") {
    REQUIRE(mgr.OutputProcessedSamples(processed_samples, astl::OutputType::SUMMARY_CSV, &target, &metric) ==
            ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE(  // NOLINT(readability-function-cognitive-complexity)
    "OutputManager::OutputProcessedSamples SUMMARY_CSV mode success", "[output_manager][csv_summary]") {
  astl::OutputManager mgr;

  // Create a temporary file for testing
  std::filesystem::path temp_file = std::filesystem::temp_directory_path() / "astl_test_summary.csv";
  TempFileGuard         tmp_guard(temp_file);

  // Set environment variable
  EnvVarGuard csv_env_var_guard(astl::EnvVar::ASTL_OUTPUT_SUMMARY_CSV, temp_file.string());

  SECTION("SUMMARY_CSV mode with valid data creates CSV file") {
    auto processed_samples = CreateTestProcessedSamplesMap();

    // Call OutputProcessedSamples - it should process ALL metrics/targets
    REQUIRE(mgr.OutputProcessedSamples(processed_samples, astl::OutputType::SUMMARY_CSV, nullptr, nullptr) ==
            ASTL_STATUS_SUCCESS);

    // Verify file was created
    REQUIRE(std::filesystem::exists(temp_file));

    // Read and verify file contents
    std::ifstream file(temp_file);
    REQUIRE(file.is_open());

    std::string line;

    std::getline(file, line);
    REQUIRE(line.rfind("ASTL Build Version,", 0) == 0);
    std::getline(file, line);
    REQUIRE(line.rfind("Collection Date/Time,", 0) == 0);
    std::getline(file, line);
    REQUIRE(line == "Command Line,\"<not captured>\"");
    while (std::getline(file, line) && !line.empty()) {
    }

    std::getline(file, line);
    REQUIRE(line == "Metric: Temperature - Board temperature (Celsius)");
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Summary Statistics");
    std::getline(file, line);
    REQUIRE(line == "Target,Min,Max,Average,Time Weighted Average,Count");
    std::getline(file, line);
    REQUIRE(line.find("Target1,10.00,30.00,20.00,15.00,3") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.find("Target2,15.00,35.00,25.00,20.00,3") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Histogram Summary");
    std::getline(file, line);
    REQUIRE(line == "Target,Type,10.00,20.00,30.00,15.00,25.00,35.00");
    std::getline(file, line);
    REQUIRE(line == "Target1,Discrete,1,1,1,0,0,0");
    std::getline(file, line);
    REQUIRE(line == "Target2,Discrete,0,0,0,1,1,1");
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Metric: Voltage - Rail voltage (Volts)");
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Summary Statistics");
    std::getline(file, line);
    REQUIRE(line == "Target,Min,Max,Average,Time Weighted Average,Count");
    std::getline(file, line);
    REQUIRE(line.find("Target1,3.30,3.50,3.40,3.35,3") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.find("Target2,5.00,5.20,5.10,5.05") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Histogram Summary");
    std::getline(file, line);
    REQUIRE(line == "Target,Type,3.30,3.40,3.50,5.00,5.10,5.20");
    std::getline(file, line);
    REQUIRE(line == "Target1,Discrete,1,1,1,0,0,0");
    std::getline(file, line);
    REQUIRE(line == "Target2,Discrete,0,0,0,1,1,1");
    std::getline(file, line);
    REQUIRE(line.empty());
  }

  SECTION("SUMMARY_CSV mode with empty data writes collection info") {
    astl::ProcessedSamplesMap empty_samples;

    REQUIRE(mgr.OutputProcessedSamples(empty_samples, astl::OutputType::SUMMARY_CSV, nullptr, nullptr) ==
            ASTL_STATUS_SUCCESS);

    // Verify file was created
    REQUIRE(std::filesystem::exists(temp_file));

    // Read and verify file contents
    std::ifstream file(temp_file);
    REQUIRE(file.is_open());

    std::string line;

    std::getline(file, line);
    REQUIRE(line.rfind("ASTL Build Version,", 0) == 0);
    std::getline(file, line);
    REQUIRE(line.rfind("Collection Date/Time,", 0) == 0);
    std::getline(file, line);
    REQUIRE(line == "Command Line,\"<not captured>\"");
  }
}

TEST_CASE("SummaryCsvOutput direct testing", "[csv_summary]") {  // NOLINT
  // Create a temporary file for testing
  std::filesystem::path temp_file = std::filesystem::temp_directory_path() / "astl_direct_csv_test.csv";
  TempFileGuard         tmp_guard2(temp_file);

  SECTION("SummaryCsvOutput with valid data") {
    astl::SummaryCsvOutput csv_output(temp_file.string());
    REQUIRE(csv_output.Ready());

    auto processed_samples = CreateTestProcessedSamplesMap();

    REQUIRE(csv_output.WriteProcessedSamples(processed_samples) == ASTL_STATUS_SUCCESS);

    // Verify file was created and has correct content
    REQUIRE(std::filesystem::exists(temp_file));

    std::ifstream file(temp_file);
    REQUIRE(file.is_open());

    std::string line;

    std::getline(file, line);
    REQUIRE(line.rfind("ASTL Build Version,", 0) == 0);
    std::getline(file, line);
    REQUIRE(line.rfind("Collection Date/Time,", 0) == 0);
    std::getline(file, line);
    REQUIRE(line == "Command Line,\"<not captured>\"");
    while (std::getline(file, line) && !line.empty()) {
    }

    std::getline(file, line);
    REQUIRE(line == "Metric: Temperature - Board temperature (Celsius)");
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Summary Statistics");
    std::getline(file, line);
    REQUIRE(line == "Target,Min,Max,Average,Time Weighted Average,Count");
    std::getline(file, line);
    REQUIRE(line.find("Target1,10.00,30.00,20.00,15.00,3") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.find("Target2,15.00,35.00,25.00,20.00,3") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Histogram Summary");
    std::getline(file, line);
    REQUIRE(line == "Target,Type,10.00,20.00,30.00,15.00,25.00,35.00");
    std::getline(file, line);
    REQUIRE(line == "Target1,Discrete,1,1,1,0,0,0");
    std::getline(file, line);
    REQUIRE(line == "Target2,Discrete,0,0,0,1,1,1");
    std::getline(file, line);
    REQUIRE(line.empty());

    std::getline(file, line);
    REQUIRE(line == "Metric: Voltage - Rail voltage (Volts)");
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Summary Statistics");
    std::getline(file, line);
    REQUIRE(line == "Target,Min,Max,Average,Time Weighted Average,Count");
    std::getline(file, line);
    REQUIRE(line.find("Target1,3.30,3.50,3.40,3.35") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.find("Target2,5.00,5.20,5.10,5.05") != std::string::npos);
    std::getline(file, line);
    REQUIRE(line.empty());
    std::getline(file, line);
    REQUIRE(line == "Histogram Summary");
    std::getline(file, line);
    REQUIRE(line == "Target,Type,3.30,3.40,3.50,5.00,5.10,5.20");
    std::getline(file, line);
    REQUIRE(line == "Target1,Discrete,1,1,1,0,0,0");
    std::getline(file, line);
    REQUIRE(line == "Target2,Discrete,0,0,0,1,1,1");
    std::getline(file, line);
    REQUIRE(line.empty());
  }

  SECTION("SummaryCsvOutput includes histogram-only metrics") {
    astl::SummaryCsvOutput csv_output(temp_file.string());
    REQUIRE(csv_output.Ready());

    SummaryTestTarget         target{"EventTarget"};
    SummaryTestMetric         metric{"PowerState", "Power state transitions", ASTL_UNITS_UNKNOWN, ASTL_VALUE_BOOL8,
                             ASTL_METRIC_EVENT};
    astl::ProcessedSamplesMap processed_samples;
    processed_samples[&target][&metric] = MakeSamplesWithBoolValues({true, false, true});

    REQUIRE(csv_output.WriteProcessedSamples(processed_samples) == ASTL_STATUS_SUCCESS);

    std::ifstream file(temp_file);
    REQUIRE(file.is_open());
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    REQUIRE(content.find("Metric: PowerState - Power state transitions (Unknown)") != std::string::npos);
    REQUIRE(
        content.find(
            "Summary Statistics\nTarget,Min,Max,Average,Time Weighted Average,Count\nEventTarget,N/A,N/A,N/A,N/A,3") !=
        std::string::npos);
    REQUIRE(content.find("Histogram Summary\nTarget,Type,0,1\nEventTarget,Discrete,1,2") != std::string::npos);
  }

  SECTION("SummaryCsvOutput with empty path") {
    astl::SummaryCsvOutput csv_output("");
    REQUIRE_FALSE(csv_output.Ready());

    auto processed_samples = CreateTestProcessedSamplesMap();
    REQUIRE(csv_output.WriteProcessedSamples(processed_samples) == ASTL_STATUS_INTERNAL_ERROR);
  }
}

TEST_CASE("MinMaxAvgSummarizer direct testing", "[csv_summary][summarizer]") {  // NOLINT
  astl::MinMaxAvgSummarizer summarizer;

  SECTION("Empty samples returns zero summary") {
    std::vector<astl::ProcessedSampledData> empty_samples;
    auto                                    result = summarizer.Summarize(empty_samples);

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::MinMaxAvgSummary>(result.value()));

    auto summary = std::get<astl::MinMaxAvgSummary>(result.value());
    REQUIRE_FALSE(summary.min.has_value());
    REQUIRE_FALSE(summary.max.has_value());
    REQUIRE_FALSE(summary.avg.has_value());
    REQUIRE(summary.count == 0);
  }

  SECTION("Single sample") {
    auto samples = MakeSamplesWithValues({42.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::MinMaxAvgSummary>(result.value()));

    auto summary = std::get<astl::MinMaxAvgSummary>(result.value());
    REQUIRE(summary.min.has_value());
    REQUIRE(summary.max.has_value());
    REQUIRE(summary.avg.has_value());
    REQUIRE(summary.count == 1);

    // All values should be 42.0
    REQUIRE(*summary.min == astl::AstlValue{42.0});
    REQUIRE(*summary.max == astl::AstlValue{42.0});
    REQUIRE(*summary.avg == astl::AstlValue{42.0});
  }

  SECTION("Multiple samples with known values") {
    auto samples = MakeSamplesWithValues({1.0, 2.0, 3.0, 4.0, 5.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::MinMaxAvgSummary>(result.value()));

    auto summary = std::get<astl::MinMaxAvgSummary>(result.value());
    REQUIRE(summary.min.has_value());
    REQUIRE(summary.max.has_value());
    REQUIRE(summary.avg.has_value());
    REQUIRE(summary.count == 5);

    REQUIRE(*summary.min == astl::AstlValue{1.0});
    REQUIRE(*summary.max == astl::AstlValue{5.0});
    REQUIRE(*summary.avg == astl::AstlValue{3.0});  // (1+2+3+4+5)/5 = 15/5 = 3
  }

  SECTION("Samples with negative values") {
    auto samples = MakeSamplesWithValues({-10.0, -5.0, 0.0, 5.0, 10.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::MinMaxAvgSummary>(result.value());

    REQUIRE(*summary.min == astl::AstlValue{-10.0});
    REQUIRE(*summary.max == astl::AstlValue{10.0});
    REQUIRE(*summary.avg == astl::AstlValue{0.0});  // (-10-5+0+5+10)/5 = 0/5 = 0
    REQUIRE(summary.count == 5);
  }
}

TEST_CASE("TimeWeightedAvgSummarizer direct testing", "[csv_summary][summarizer][time_weighted_average]") {  // NOLINT
  astl::TimeWeightedAvgSummarizer summarizer;

  SECTION("Empty samples returns empty summary") {
    std::vector<astl::ProcessedSampledData> empty_samples;
    auto                                    result = summarizer.Summarize(empty_samples);

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::TimeWeightedAvgSummary>(result.value()));

    auto summary = std::get<astl::TimeWeightedAvgSummary>(result.value());
    REQUIRE_FALSE(summary.time_weighted_avg.has_value());
    REQUIRE(summary.count == 0);
  }

  SECTION("Single sample falls back to arithmetic mean") {
    auto samples = MakeSamplesWithValues({42.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::TimeWeightedAvgSummary>(result.value()));

    auto summary = std::get<astl::TimeWeightedAvgSummary>(result.value());
    REQUIRE(summary.time_weighted_avg.has_value());
    REQUIRE(summary.count == 1);
    REQUIRE(*summary.time_weighted_avg == astl::AstlValue{42.0});
  }

  SECTION("Multiple samples with equal intervals") {
    // MakeSamplesWithValues uses 1μs equal intervals: TWA uses first N-1 samples
    // {10, 20, 30} at {100, 101, 102}μs → TWA = (10*1 + 20*1) / 2 = 15.0
    auto samples = MakeSamplesWithValues({10.0, 20.0, 30.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::TimeWeightedAvgSummary>(result.value());
    REQUIRE(summary.time_weighted_avg.has_value());
    REQUIRE(summary.count == 3);
    REQUIRE(*summary.time_weighted_avg == astl::AstlValue{15.0});
  }

  SECTION("Uneven intervals produce correctly weighted result") {
    // Timestamps: 0, 100, 400 → intervals: 100μs, 300μs
    // Values: 10, 20, 30 → TWA = (10*100 + 20*300) / 400 = 7000/400 = 17.5
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{10.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{0}}  },
        {astl::AstlValue{20.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}}},
        {astl::AstlValue{30.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{400}}},
    };
    auto result = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::TimeWeightedAvgSummary>(result.value());
    REQUIRE(summary.time_weighted_avg.has_value());
    REQUIRE(summary.count == 3);
    REQUIRE(*summary.time_weighted_avg == astl::AstlValue{17.5});
  }

  SECTION("IsSupported returns true for arithmetic value/delta/rate metrics") {
    REQUIRE(summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_VALUE));
    REQUIRE(summarizer.IsSupported(ASTL_VALUE_FLOAT32, ASTL_METRIC_DELTA));
    REQUIRE(summarizer.IsSupported(ASTL_VALUE_UINT32, ASTL_METRIC_RATE));
    REQUIRE(summarizer.IsSupported(ASTL_VALUE_UINT64, ASTL_METRIC_VALUE));
  }

  SECTION("IsSupported returns false for non-arithmetic or unsupported metric types") {
    REQUIRE_FALSE(summarizer.IsSupported(ASTL_VALUE_BOOL8, ASTL_METRIC_VALUE));
    REQUIRE_FALSE(summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_RESIDENCY));
    REQUIRE_FALSE(summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_EVENT));
  }
}

TEST_CASE("ComputeTimeWeightedAverage direct testing", "[csv_summary][summarizer][time_weighted_average]") {
  SECTION("Left-hold weighting across uneven intervals") {
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{10.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{0}}  },
        {astl::AstlValue{20.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}}},
        {astl::AstlValue{30.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{400}}},
    };

    auto result = astl::ComputeTimeWeightedAverage(samples, {});
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{17.5});
  }

  SECTION("Falls back to arithmetic mean when no positive intervals exist") {
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{10.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}}},
        {astl::AstlValue{20.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}}},
        {astl::AstlValue{30.0}, astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}}},
    };

    auto result = astl::ComputeTimeWeightedAverage(samples, {});
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{20.0});
  }
}

TEST_CASE("ComputeTimeWeightedAverage pause marker clipping",
          "[csv_summary][summarizer][time_weighted_average][pause]") {
  using TS = astl::ProcessedSampleTimestamp;
  using D  = astl::ProcessedSampleTimestamp::duration;

  SECTION("Pause mid-interval clips weight to pause point") {
    // Samples: t=0 v=100, t=1000 v=200, t=2000 v=300.  Pause at t=500.
    // Interval [0,1000]: clipped to [0,500] → weight 500, contribution 50000
    // Interval [1000,2000]: no pause after t=1000, weight 1000, contribution 200000
    // TWA = 250000/1500 = 166.67
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{100.0}, TS{D{0}}   },
        {astl::AstlValue{200.0}, TS{D{1000}}},
        {astl::AstlValue{300.0}, TS{D{2000}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{500}}};

    auto result = astl::ComputeTimeWeightedAverage(samples, pauses);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{166.67});
  }

  SECTION("Pause clips only the interval it falls in; later intervals unaffected") {
    // Samples: t=0 v=0, t=200 v=100, t=600 v=200.  Pause at t=100.
    // Interval [0,200]:   pause at 100 clips it → weight 100, contribution 0
    // Interval [200,600]: no pause after t=200, weight 400, contribution 40000
    // TWA = 40000/500 = 80.0
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{0.0},   TS{D{0}}  },
        {astl::AstlValue{100.0}, TS{D{200}}},
        {astl::AstlValue{200.0}, TS{D{600}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{100}}};

    auto result = astl::ComputeTimeWeightedAverage(samples, pauses);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{80.0});
  }

  SECTION("Multiple pauses in one interval - only the first clips") {
    // Samples: t=0 v=100, t=1000 v=200.  Pauses at t=300 and t=600.
    // upper_bound(0) finds t=300 first; 300 < 1000 → interval_end=300, weight=300
    // TWA = 100.0 (single active interval)
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{100.0}, TS{D{0}}   },
        {astl::AstlValue{200.0}, TS{D{1000}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{300}}, TS{D{600}}};

    auto result = astl::ComputeTimeWeightedAverage(samples, pauses);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{100.0});
  }

  SECTION("Pause before first sample has no effect") {
    // Pause at t=50, first sample at t=100. upper_bound(100) on [50] → end; no clipping.
    // Interval [100,200]: weight 100, contribution 10000. TWA = 100.0
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{100.0}, TS{D{100}}},
        {astl::AstlValue{200.0}, TS{D{200}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{50}}};

    auto result = astl::ComputeTimeWeightedAverage(samples, pauses);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{100.0});
  }

  SECTION("Pause after last sample has no effect") {
    // Samples: t=0 v=100, t=1000 v=200.  Pause at t=1500 (after all samples).
    // Interval [0,1000]: pause at 1500 is not < 1000; no clipping. weight=1000.
    // TWA = 100.0
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{100.0}, TS{D{0}}   },
        {astl::AstlValue{200.0}, TS{D{1000}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{1500}}};

    auto result = astl::ComputeTimeWeightedAverage(samples, pauses);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{100.0});
  }

  SECTION("Pause exactly at current timestamp is not treated as clipping") {
    // Pause at t=0 == current.timestamp; upper_bound(0) on [0] → end; no clipping.
    // Interval [0,1000]: weight=1000. TWA = 100.0
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{100.0}, TS{D{0}}   },
        {astl::AstlValue{200.0}, TS{D{1000}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{0}}};

    auto result = astl::ComputeTimeWeightedAverage(samples, pauses);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value() == astl::AstlValue{100.0});
  }

  SECTION("Unsorted pause markers are sorted internally before processing") {
    // Same as the mid-interval clip test, but pauses supplied in reverse order.
    // Pause at t=500 should still clip interval [0,1000] to weight=500.
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{100.0}, TS{D{0}}   },
        {astl::AstlValue{200.0}, TS{D{1000}}},
        {astl::AstlValue{300.0}, TS{D{2000}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{1500}}, TS{D{500}}};  // unsorted

    auto result = astl::ComputeTimeWeightedAverage(samples, pauses);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    // Interval [0,1000]: clips at 500 → weight=500, contrib=50000
    // Interval [1000,2000]: clips at 1500 → weight=500, contrib=100000
    // TWA = 150000/1000 = 150.0
    REQUIRE(result->value() == astl::AstlValue{150.0});
  }
}

TEST_CASE("TimeWeightedAvgSummarizer pause marker integration",
          "[csv_summary][summarizer][time_weighted_average][pause]") {
  using TS = astl::ProcessedSampleTimestamp;
  using D  = astl::ProcessedSampleTimestamp::duration;

  SECTION("Empty samples with pause markers returns empty result") {
    std::vector<astl::ProcessedSampledData>     samples;
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{500}}};

    auto result = astl::TimeWeightedAvgSummarizer::Summarize(samples, pauses);
    REQUIRE(result.has_value());
    auto summary = std::get<astl::TimeWeightedAvgSummary>(result.value());
    REQUIRE(!summary.time_weighted_avg.has_value());
    REQUIRE(summary.count == 0);
  }

  SECTION("Single pause clip propagates through summarizer") {
    // Mirrors the ComputeTimeWeightedAverage mid-interval clip test.
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{100.0}, TS{D{0}}   },
        {astl::AstlValue{200.0}, TS{D{1000}}},
        {astl::AstlValue{300.0}, TS{D{2000}}},
    };
    std::vector<astl::ProcessedSampleTimestamp> pauses{TS{D{500}}};

    auto result = astl::TimeWeightedAvgSummarizer::Summarize(samples, pauses);
    REQUIRE(result.has_value());
    auto summary = std::get<astl::TimeWeightedAvgSummary>(result.value());
    REQUIRE(summary.time_weighted_avg.has_value());
    REQUIRE(summary.time_weighted_avg.value() == astl::AstlValue{166.67});
    REQUIRE(summary.count == 3);
  }

  SECTION("No pause markers yields same result as empty span") {
    std::vector<astl::ProcessedSampledData> samples{
        {astl::AstlValue{10.0}, TS{D{0}}  },
        {astl::AstlValue{20.0}, TS{D{100}}},
        {astl::AstlValue{30.0}, TS{D{400}}},
    };

    auto result_no_pause = astl::TimeWeightedAvgSummarizer::Summarize(samples, {});
    auto result_pauses =
        astl::TimeWeightedAvgSummarizer::Summarize(samples, std::vector<astl::ProcessedSampleTimestamp>{});
    REQUIRE(result_no_pause.has_value());
    REQUIRE(result_pauses.has_value());
    REQUIRE(std::get<astl::TimeWeightedAvgSummary>(result_no_pause.value()).time_weighted_avg ==
            std::get<astl::TimeWeightedAvgSummary>(result_pauses.value()).time_weighted_avg);
  }
}

TEST_CASE("HistogramSummarizer discrete mode", "[histogram][summarizer][csv_summary]") {
  astl::HistogramSummarizer summarizer;  // Default is discrete mode

  SECTION("Empty samples") {
    std::vector<astl::ProcessedSampledData> samples;
    auto                                    result = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::HistogramSummary>(result.value()));

    auto summary = std::get<astl::HistogramSummary>(result.value());
    REQUIRE(summary.bins.empty());
    REQUIRE(summary.total_count == 0);
    REQUIRE(summary.is_discrete);
  }

  SECTION("Simple discrete values") {
    auto samples = MakeSamplesWithValues({10.0, 20.0, 30.0, 20.0, 10.0, 20.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    REQUIRE(summary.is_discrete);
    REQUIRE(summary.total_count == 6);
    REQUIRE(summary.unique_values == 3);
    REQUIRE(summary.bins.size() == 3);

    // Bins should be sorted by value (map iteration order)
    REQUIRE(summary.bins[0].value == astl::AstlValue{10.0});
    REQUIRE(summary.bins[0].count == 2);
    REQUIRE(summary.bins[0].is_discrete);

    REQUIRE(summary.bins[1].value == astl::AstlValue{20.0});
    REQUIRE(summary.bins[1].count == 3);

    REQUIRE(summary.bins[2].value == astl::AstlValue{30.0});
    REQUIRE(summary.bins[2].count == 1);
  }

  SECTION("All same values") {
    auto samples = MakeSamplesWithValues({42.0, 42.0, 42.0, 42.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    REQUIRE(summary.bins.size() == 1);
    REQUIRE(summary.bins[0].value == astl::AstlValue{42.0});
    REQUIRE(summary.bins[0].count == 4);
    REQUIRE(summary.unique_values == 1);
  }

  SECTION("Integer values") {
    auto samples = MakeSamplesWithValues({1.0, 2.0, 3.0, 2.0, 1.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    REQUIRE(summary.bins.size() == 3);
    REQUIRE(summary.unique_values == 3);
    REQUIRE(summary.total_count == 5);
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("HistogramSummarizer range mode", "[histogram][summarizer][csv_summary]") {
  constexpr std::size_t     num_bins = 5;
  astl::HistogramSummarizer summarizer(num_bins);

  SECTION("Empty samples") {
    std::vector<astl::ProcessedSampledData> samples;
    auto                                    result = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    REQUIRE(summary.bins.empty());
    REQUIRE(summary.total_count == 0);
    REQUIRE_FALSE(summary.is_discrete);
  }

  SECTION("Values distributed across range") {
    // Values from 0 to 100, should create 5 equal-width bins
    auto samples = MakeSamplesWithValues({0.0, 10.0, 25.0, 50.0, 75.0, 90.0, 100.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    REQUIRE_FALSE(summary.is_discrete);
    REQUIRE(summary.total_count == 7);
    REQUIRE(summary.bins.size() == num_bins);
    REQUIRE(summary.out_of_range_count == 0);

    // Each bin should be 20 units wide: [0,20), [20,40), [40,60), [60,80), [80,100]
    double expected_bin_width = 20.0;
    for (std::size_t i = 0; i < num_bins; ++i) {
      REQUIRE_FALSE(summary.bins[i].is_discrete);
      double expected_lower = static_cast<double>(i) * expected_bin_width;
      REQUIRE(summary.bins[i].lower_bound == Catch::Approx(expected_lower).margin(0.01));
    }

    // Verify counts: [0,10], [25], [50], [75], [90,100]
    REQUIRE(summary.bins[0].count == 2);  // 0, 10
    REQUIRE(summary.bins[1].count == 1);  // 25
    REQUIRE(summary.bins[2].count == 1);  // 50
    REQUIRE(summary.bins[3].count == 1);  // 75
    REQUIRE(summary.bins[4].count == 2);  // 90, 100
  }

  SECTION("All same values - creates padded bin") {
    auto samples = MakeSamplesWithValues({50.0, 50.0, 50.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    // When all values are the same, should create one bin with padding
    REQUIRE(summary.bins.size() == 1);
    REQUIRE(summary.bins[0].count == 3);
    REQUIRE(summary.bins[0].lower_bound == Catch::Approx(49.5).margin(0.01));
    REQUIRE(summary.bins[0].upper_bound == Catch::Approx(50.5).margin(0.01));
  }

  SECTION("Negative values") {
    auto samples = MakeSamplesWithValues({-100.0, -50.0, 0.0, 50.0, 100.0});
    auto result  = summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    REQUIRE(summary.bins.size() == num_bins);
    REQUIRE(summary.total_count == 5);
    REQUIRE(summary.out_of_range_count == 0);

    // Range is -100 to 100 = 200, so each bin is 40 units
    REQUIRE(summary.bins[0].lower_bound == Catch::Approx(-100.0).margin(0.01));
    REQUIRE(summary.bins[4].upper_bound >= 100.0);  // Last bin includes upper bound
  }

  SECTION("Single bin mode") {
    astl::HistogramSummarizer single_bin_summarizer(1);
    auto                      samples = MakeSamplesWithValues({1.0, 2.0, 3.0, 4.0, 5.0});
    auto                      result  = single_bin_summarizer.Summarize(samples);

    REQUIRE(result.has_value());
    auto summary = std::get<astl::HistogramSummary>(result.value());

    REQUIRE(summary.bins.size() == 1);
    REQUIRE(summary.bins[0].count == 5);  // All values in one bin
  }
}

TEST_CASE("HistogramSummarizer type support", "[histogram][summarizer][csv_summary]") {
  SECTION("Discrete mode supports all types") {
    astl::HistogramSummarizer discrete_summarizer;  // Default is discrete mode

    // Arithmetic types
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_UINT8, ASTL_METRIC_VALUE));
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_UINT16, ASTL_METRIC_VALUE));
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_UINT32, ASTL_METRIC_VALUE));
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_UINT64, ASTL_METRIC_VALUE));
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_FLOAT32, ASTL_METRIC_VALUE));
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_VALUE));

    // Non-arithmetic types (supported in discrete mode)
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_BOOL8, ASTL_METRIC_VALUE));
  }

  SECTION("Range mode supports only arithmetic types") {
    constexpr std::size_t     num_bins = 5;
    astl::HistogramSummarizer range_summarizer(num_bins);  // Range mode

    // Arithmetic types supported
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_UINT8, ASTL_METRIC_VALUE));
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_UINT16, ASTL_METRIC_VALUE));
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_UINT32, ASTL_METRIC_VALUE));
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_UINT64, ASTL_METRIC_VALUE));
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT32, ASTL_METRIC_VALUE));
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_VALUE));

    // Non-arithmetic types NOT supported in range mode
    REQUIRE_FALSE(range_summarizer.IsSupported(ASTL_VALUE_BOOL8, ASTL_METRIC_VALUE));

    // Range mode only supports VALUE, DELTA, and RATE metric types
    REQUIRE_FALSE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_RATE));
    REQUIRE_FALSE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_RESIDENCY));
  }

  SECTION("Discrete mode supports all metric types except RESIDENCY") {
    astl::HistogramSummarizer discrete_summarizer;  // Default is discrete mode

    // Supported metric types in discrete mode
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_VALUE));
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_FINITE_SET_VALUE));
    REQUIRE(discrete_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_EVENT));

    // RESIDENCY is not supported in any mode
    REQUIRE_FALSE(discrete_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_RESIDENCY));
  }

  SECTION("Range mode supports VALUE, DELTA, and RATE metric types only") {
    constexpr std::size_t     num_bins = 5;
    astl::HistogramSummarizer range_summarizer(num_bins);

    // Supported metric types in range mode
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_VALUE));
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_FINITE_SET_VALUE));
    REQUIRE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_EVENT));

    // Unsupported metric types in range mode
    REQUIRE_FALSE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_RATE));
    REQUIRE_FALSE(range_summarizer.IsSupported(ASTL_VALUE_FLOAT64, ASTL_METRIC_RESIDENCY));
  }
}
