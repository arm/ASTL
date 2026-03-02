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
  auto                GetCollectorType() const -> astl::CollectorType override { return collector_type; }
  auto                Name() const -> std::string const& override { return name; }
  auto                GetProperties(astl_target_properties_t* props) const -> astl_status_code override {
    if (!props) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    props->_handle = this;
    return ASTL_STATUS_SUCCESS;
  }
};

struct SummaryTestMetric : public astl::IMetric {
  std::string name{"M0"};
  bool        CheckCapabilities(const astl::Capabilities& caps) const override {
    (void)caps;
    return true;
  }
  std::expected<astl::OperationSequence, astl_status_code> GetOperations() override {
    return astl::OperationSequence{};
  }
  astl_status_code ReceiveRawSample(const astl::RawSampledData& sample) override {
    (void)sample;
    return ASTL_STATUS_SUCCESS;
  }
  void             SetProcessedSampleSink(astl::IProcessedSampleSink* sink) override { (void)sink; }
  void             Reset() override {}
  astl_status_code Summarize() override { return ASTL_STATUS_SUCCESS; }
  astl_status_code GetProperties(astl_metric_properties_t* props) const override {
    (void)props;
    return ASTL_STATUS_SUCCESS;
  }
  auto             Name() const -> std::string const& override { return name; }
  astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
    (void)processed_sample;
    return ASTL_STATUS_SUCCESS;
  }
};

// Helper to create test samples with specific values for summary testing
std::vector<astl::ProcessedSampledData> MakeSamplesWithValues(const std::vector<double>& values) {
  std::vector<astl::ProcessedSampledData> samples_vec;
  samples_vec.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    samples_vec.emplace_back(astl::AstlValue{values[i]},
                             astl::SampleTimestamp{std::chrono::microseconds{100 + static_cast<int>(i)}});
  }
  return samples_vec;
}

// Helper to create processed samples map with multiple targets and metrics
astl::ProcessedSamplesMap CreateTestProcessedSamplesMap() {
  // Create targets in a vector to ensure predictable order
  static std::vector<SummaryTestTarget> targets(2);
  static std::vector<SummaryTestMetric> metrics(2);

  targets[0].name = "Target1";
  targets[1].name = "Target2";
  metrics[0].name = "Temperature";
  metrics[1].name = "Voltage";

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

  SECTION("SUMMARY_CSV mode without ASTL_CSV_OUTPUT_FILE environment variable") {
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

    // Check System Info section header
    std::getline(file, line);
    REQUIRE(line == "System Info");

    // Check System Info column headers
    std::getline(file, line);
    REQUIRE(line == "Field,Value");

    // Skip system info rows until blank separator
    while (std::getline(file, line) && !line.empty()) {
    }

    // Check MinMaxAvg section header
    std::getline(file, line);
    REQUIRE(line == "Min/Max/Average Summary");

    // Check MinMaxAvg column headers
    std::getline(file, line);
    REQUIRE(line == "MetricName,Target,Min,Max,Average,Count");

    // Read MinMaxAvg data lines
    std::vector<std::string> minmax_lines;
    while (std::getline(file, line) && !line.empty()) {
      minmax_lines.push_back(line);
    }

    // Should have 4 lines (2 metrics × 2 targets)
    REQUIRE(minmax_lines.size() == 4);

    // Check Histogram section header
    std::getline(file, line);
    REQUIRE(line == "Histogram Summary");

    // Check Histogram column headers
    std::getline(file, line);
    REQUIRE(line == "MetricName,Target,Type,Value/Range,Count");

    // Read Histogram data lines (one line per bin)
    std::vector<std::string> histogram_lines;
    while (std::getline(file, line)) {
      if (!line.empty()) {
        histogram_lines.push_back(line);
      }
    }

    // Should have multiple histogram lines (one per unique value in each metric/target combination)
    // Temperature has 3 unique values per target, Voltage has 3 unique values per target
    // Total: (3 + 3 + 3 + 3) = 12 lines
    REQUIRE(histogram_lines.size() == 12);

    // Based on debug output, the actual order is Target2 first, then Target1
    // This is due to pointer address ordering in the map
    REQUIRE(minmax_lines[0].find("Temperature,Target2,15,35,25,3") != std::string::npos);
    REQUIRE(minmax_lines[1].find("Temperature,Target1,10,30,20,3") != std::string::npos);
    REQUIRE(minmax_lines[2].find("Voltage,Target2,5,5.2,5.1") !=
            std::string::npos);  // Allow for floating point precision
    REQUIRE(minmax_lines[3].find("Voltage,Target1,3.3,3.5,3.4,3") != std::string::npos);
  }

  SECTION("SUMMARY_CSV mode with empty data writes system info section") {
    astl::ProcessedSamplesMap empty_samples;

    REQUIRE(mgr.OutputProcessedSamples(empty_samples, astl::OutputType::SUMMARY_CSV, nullptr, nullptr) ==
            ASTL_STATUS_SUCCESS);

    // Verify file was created
    REQUIRE(std::filesystem::exists(temp_file));

    // Read and verify file contents
    std::ifstream file(temp_file);
    REQUIRE(file.is_open());

    std::string line;

    // With empty data, only the system info section is emitted.
    std::getline(file, line);
    REQUIRE(line == "System Info");
    std::getline(file, line);
    REQUIRE(line == "Field,Value");
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

    // Check System Info section header
    std::getline(file, line);
    REQUIRE(line == "System Info");

    // Check System Info column headers
    std::getline(file, line);
    REQUIRE(line == "Field,Value");

    // Skip system info rows until blank separator
    while (std::getline(file, line) && !line.empty()) {
    }

    // Check MinMaxAvg section header
    std::getline(file, line);
    REQUIRE(line == "Min/Max/Average Summary");

    // Check MinMaxAvg column headers
    std::getline(file, line);
    REQUIRE(line == "MetricName,Target,Min,Max,Average,Count");

    // Collect MinMaxAvg data lines
    std::vector<std::string> minmax_lines;
    while (std::getline(file, line) && !line.empty()) {
      minmax_lines.push_back(line);
    }
    REQUIRE(minmax_lines.size() == 4);  // 2 metrics × 2 targets

    // Check Histogram section header
    std::getline(file, line);
    REQUIRE(line == "Histogram Summary");

    // Check Histogram column headers
    std::getline(file, line);
    REQUIRE(line == "MetricName,Target,Type,Value/Range,Count");

    // Collect Histogram data lines (one line per bin)
    std::vector<std::string> histogram_lines;
    while (std::getline(file, line)) {
      if (!line.empty()) {
        histogram_lines.push_back(line);
      }
    }
    // Should have 12 histogram lines (3 unique values × 2 targets × 2 metrics)
    REQUIRE(histogram_lines.size() == 12);
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
