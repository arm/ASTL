#include <chrono>
#include <cstdlib>  // setenv, unsetenv
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../mock_classes.hpp"     // mocks for orchestrator dependencies
#include "../../test_includes.hpp"    // include before catch2
#include "../../test_utilities.hpp"   // TempFileGuard
#include "output/output_manager.hpp"  // concrete OutputManager

// Bring trompeloeil wildcard into scope for ALLOW_CALL expectations used in deferred emission test.
using trompeloeil::_;
#include "common/astl_value.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "orchestrator/orchestrator.hpp"  // Added for astl::Orchestrator direct construction
#include "output/perfetto_output.hpp"
#include "target.hpp"

namespace {
// Deterministic timestamp base (epoch)
astl::ProcessedSampledData MakeSample(uint64_t value, astl::SampleTimestamp timestamp) {
  return astl::ProcessedSampledData{astl::AstlValue{value}, timestamp};
}

}  // namespace

TEST_CASE("PerfettoOutput basic write & JSON structure", "[perfetto_output]") {  // NOLINT
  // Arrange
  auto                 tmp_guard = TempFileGuard{"astl_perfetto_test.json"};
  astl::PerfettoOutput writer(tmp_guard.path);

  REQUIRE(writer.Ready());

  TestTargetBase target{"TT"};  // key for map
  TestMetricBase metric{"MM"};

  astl::ProcessedSamplesMap               processed;
  const astl::SampleTimestamp             base_ts{};  // epoch
  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(MakeSample(42U, base_ts));
  samples.emplace_back(MakeSample(100U, base_ts + astl::SampleTimestamp::duration{5}));
  processed[&target][&metric] = samples;

  // Act
  auto status = writer.WriteProcessedSamples(processed);

  // Assert
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  // Read file
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(content.empty());
  // Basic JSON array framing and display time unit metadata
  REQUIRE(content.front() == '[');
  REQUIRE(content.find("\"displayTimeUnit\":\"us\"") != std::string::npos);
  // File footer ']' is only written at writer destruction; during test lifetime it may still be open.
  // So just ensure the content contains at least one '{' event fragment.
  REQUIRE(content.find('{') != std::string::npos);
  // Expect two counter events with composite name <Target>.<Metric> and metric field in args
  REQUIRE(content.find("\"name\":\"TT.MM\"") != std::string::npos);
  REQUIRE(content.find("\"metric\":\"MM\"") != std::string::npos);
  REQUIRE(content.find("\"value\":42") != std::string::npos);
  REQUIRE(content.find("\"value\":100") != std::string::npos);
  // target name sanitized
  REQUIRE(content.find("\"target\":\"TT\"") != std::string::npos);
}

TEST_CASE("PerfettoOutput emits process and thread metadata", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_metadata.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());
  TestTargetBase            target{"MetaTarget"};
  TestMetricBase            metric{"MetaMetric"};
  astl::ProcessedSamplesMap processed;
  processed[&target][&metric] = {MakeSample(static_cast<uint64_t>(42), astl::SampleTimestamp{})};
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  // Global displayTimeUnit metadata should be present
  REQUIRE(content.find("\"displayTimeUnit\":\"us\"") != std::string::npos);
  REQUIRE(content.find("\"ph\":\"M\",\"pid\":1,\"name\":\"process_name\",\"args\":{\"name\":\"MetaTarget\"}") !=
          std::string::npos);
  REQUIRE(
      content.find("\"ph\":\"M\",\"pid\":1,\"tid\":1,\"name\":\"thread_name\",\"args\":{\"name\":\"MetaMetric\"}") !=
      std::string::npos);
}

TEST_CASE("PerfettoOutput empty map yields no events but valid JSON", "[perfetto_output]") {  // NOLINT
  auto                 tmp_guard = TempFileGuard{"astl_perfetto_empty.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());

  astl::ProcessedSamplesMap empty;
  auto                      status = writer.WriteProcessedSamples(empty);
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  // With global trace_metadata event now emitted, no counter/instant events should appear but displayTimeUnit may.
  bool matches_variant =
      (content == "[\n") || (content == "[") || (content == "[\n\n") ||
      (content.find("\"displayTimeUnit\":\"us\"") != std::string::npos &&
       content.find("\"ph\":\"C\"") == std::string::npos && content.find("\"ph\":\"I\"") == std::string::npos);
  REQUIRE(matches_variant);
}

// Edge case: names containing whitespace and quotes should be sanitized to underscores.
TEST_CASE("PerfettoOutput sanitizes target and metric names", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_sanitize.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());

  // Custom dummy types with problematic names
  TestTargetBase                          target{"My Target \"Alpha\""};
  TestMetricBase                          metric{"Power Rate \"Watt\""};
  astl::ProcessedSamplesMap               processed;
  const astl::SampleTimestamp             base_ts{};
  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(MakeSample(7U, base_ts));
  processed[&target][&metric] = samples;

  auto status = writer.WriteProcessedSamples(processed);
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(content.empty());

  // Expect quotes and whitespace replaced with underscores in both target and metric names.
  // Composite name should include sanitized target and metric separated by a dot
  // Adjusted expectation: quotes replaced by single '_' each, not doubled at end.
  REQUIRE(content.find("\"name\":\"My_Target__Alpha_.Power_Rate__Watt_\"") != std::string::npos);
  // Args should still contain individual sanitized target and metric fields.
  REQUIRE(content.find("\"target\":\"My_Target__Alpha_\"") != std::string::npos);
  REQUIRE(content.find("\"metric\":\"Power_Rate__Watt_\"") != std::string::npos);
  // Ensure raw problematic substrings not present.
  REQUIRE(content.find("My Target \"Alpha\"") == std::string::npos);
  REQUIRE(content.find("Power Rate \"Watt\"") == std::string::npos);
}

// Multiple targets and metrics should generate an event per sample.
TEST_CASE("PerfettoOutput multiple targets & metrics event count", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_multi.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());

  astl::ProcessedSamplesMap   processed;
  const astl::SampleTimestamp base_ts{};
  // Create 2 targets x 3 metrics each with 2 samples => 12 events expected.
  std::vector<std::unique_ptr<TestTargetBase>> targets;
  std::vector<std::unique_ptr<TestMetricBase>> metrics;
  for (int target_index = 0; target_index < 2; ++target_index) {  // NOLINT(readability-magic-numbers)
    targets.emplace_back(std::make_unique<TestTargetBase>("T" + std::to_string(target_index)));
  }
  for (int metric_index = 0; metric_index < 3; ++metric_index) {  // NOLINT(readability-magic-numbers)
    metrics.emplace_back(std::make_unique<TestMetricBase>("M" + std::to_string(metric_index)));
  }

  for (auto& tgt : targets) {
    for (auto& met : metrics) {
      std::vector<astl::ProcessedSampledData> samples;
      samples.emplace_back(MakeSample(static_cast<uint64_t>(10 + samples.size()), base_ts));
      samples.emplace_back(MakeSample(static_cast<uint64_t>(20 + samples.size()),
                                      base_ts + astl::SampleTimestamp::duration{std::chrono::microseconds{
                                                    1}}));  // NOLINT(readability-magic-numbers)
      processed[tgt.get()][met.get()] = samples;
    }
  }

  auto status = writer.WriteProcessedSamples(processed);
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(content.empty());

  // Count occurrences of '"ph":"C"' as proxy for event count.
  size_t            event_count = 0;
  size_t            pos         = 0;
  const std::string needle      = "\"ph\":\"C\"";
  while ((pos = content.find(needle, pos)) != std::string::npos) {
    ++event_count;
    pos += needle.size();
  }
  REQUIRE(event_count == 12);
}

TEST_CASE("PerfettoOutput instant string event", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_instant.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());

  TestTargetBase                          target{"TStr"};
  TestMetricBase                          metric{"StateMetric"};
  astl::ProcessedSamplesMap               processed;
  const astl::SampleTimestamp             base_ts{};
  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(astl::ProcessedSampledData{astl::AstlValue{std::string{"EnteringState"}}, base_ts});
  processed[&target][&metric] = samples;

  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("\"ph\":\"I\"") != std::string::npos);
  REQUIRE(content.find("EnteringState") != std::string::npos);
  REQUIRE(content.find("\"s\":\"t\"") != std::string::npos);  // scope thread
}

TEST_CASE("PerfettoOutput per-metric tid differentiation", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_tid.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());

  TestTargetBase target{"T"};
  TestMetricBase metric_one{"M1"};
  TestMetricBase metric_two{"M2"};

  astl::ProcessedSamplesMap   processed;
  const astl::SampleTimestamp base_ts{};
  processed[&target][&metric_one] =
      std::vector<astl::ProcessedSampledData>{MakeSample(static_cast<uint64_t>(1), base_ts)};
  processed[&target][&metric_two] =
      std::vector<astl::ProcessedSampledData>{MakeSample(static_cast<uint64_t>(2), base_ts)};

  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  auto        extract_tid_for_metric = [&](const std::string& metric_composite) {
    size_t pos = content.find("\"name\":\"" + metric_composite + "\"");
    REQUIRE(pos != std::string::npos);
    size_t tid_pos = content.find("\"tid\":", pos);
    REQUIRE(tid_pos != std::string::npos);
    tid_pos += 6;  // length of "tid":
    while (tid_pos < content.size() && content[tid_pos] == ' ') {
      ++tid_pos;
    }
    size_t end = tid_pos;
    while (end < content.size() && isdigit(static_cast<unsigned char>(content[end]))) {
      ++end;
    }
    REQUIRE(end > tid_pos);
    return std::stoi(content.substr(tid_pos, end - tid_pos));
  };
  int tid_m1 = extract_tid_for_metric("T.M1");
  int tid_m2 = extract_tid_for_metric("T.M2");
  REQUIRE(tid_m1 != tid_m2);
  // Same pid for both events
  auto extract_pid_for_metric = [&](const std::string& metric_composite) {
    size_t pos = content.find("\"name\":\"" + metric_composite + "\"");
    REQUIRE(pos != std::string::npos);
    size_t pid_pos = content.find("\"pid\":", pos);
    REQUIRE(pid_pos != std::string::npos);
    pid_pos += 6;  // length of "pid":
    while (pid_pos < content.size() && content[pid_pos] == ' ') {
      ++pid_pos;
    }
    size_t end = pid_pos;
    while (end < content.size() && isdigit(static_cast<unsigned char>(content[end]))) {
      ++end;
    }
    REQUIRE(end > pid_pos);
    return std::stoi(content.substr(pid_pos, end - pid_pos));
  };
  REQUIRE(extract_pid_for_metric("T.M1") == extract_pid_for_metric("T.M2"));
}

TEST_CASE("PerfettoOutput pid/tid stability across multiple writes", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_stability.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());

  TestTargetBase target{"TStable"};
  TestMetricBase metric_a{"MA"};
  TestMetricBase metric_b{"MB"};

  const astl::SampleTimestamp base_ts{};
  // First write
  astl::ProcessedSamplesMap processed1;
  processed1[&target][&metric_a] =
      std::vector<astl::ProcessedSampledData>{MakeSample(static_cast<uint64_t>(10), base_ts)};
  REQUIRE(writer.WriteProcessedSamples(processed1) == ASTL_STATUS_SUCCESS);
  // Second write adds new metric mB and another sample for mA.
  astl::ProcessedSamplesMap processed2;
  processed2[&target][&metric_a] = std::vector<astl::ProcessedSampledData>{
      MakeSample(static_cast<uint64_t>(11), base_ts + astl::SampleTimestamp::duration{1})};
  processed2[&target][&metric_b] = std::vector<astl::ProcessedSampledData>{
      MakeSample(static_cast<uint64_t>(12), base_ts + astl::SampleTimestamp::duration{2})};
  REQUIRE(writer.WriteProcessedSamples(processed2) == ASTL_STATUS_SUCCESS);

  std::ifstream stability_stream(tmp_guard.path);
  REQUIRE(stability_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(stability_stream)), std::istreambuf_iterator<char>());
  // Expect two events for MA and one for MB
  size_t count_metric_a = 0;
  size_t scan           = 0;
  while ((scan = content.find("\"name\":\"TStable.MA\"", scan)) != std::string::npos) {
    ++count_metric_a;
    scan += 5;
  }
  size_t count_metric_b = 0;
  scan                  = 0;
  while ((scan = content.find("\"name\":\"TStable.MB\"", scan)) != std::string::npos) {
    ++count_metric_b;
    scan += 5;
  }
  REQUIRE(count_metric_a == 2);
  REQUIRE(count_metric_b == 1);
  // Extract tid for each occurrence of MA and MB (ignore timestamps due to ms scaling compression to 0).
  auto extract_single_tid = [&](size_t start_pos) {
    size_t tid_pos = content.find("\"tid\":", start_pos);
    REQUIRE(tid_pos != std::string::npos);
    tid_pos += 6;  // length of "tid":
    while (tid_pos < content.size() && content[tid_pos] == ' ') {
      ++tid_pos;
    }
    size_t end = tid_pos;
    while (end < content.size() && isdigit(static_cast<unsigned char>(content[end]))) {
      ++end;
    }
    REQUIRE(end > tid_pos);
    return std::stoi(content.substr(tid_pos, end - tid_pos));
  };
  // Find occurrences for MA
  size_t first_ma = content.find("\"name\":\"TStable.MA\"");
  REQUIRE(first_ma != std::string::npos);
  size_t second_ma = content.find("\"name\":\"TStable.MA\"", first_ma + 10);
  REQUIRE(second_ma != std::string::npos);
  int tid_ma1 = extract_single_tid(first_ma);
  int tid_ma2 = extract_single_tid(second_ma);
  REQUIRE(tid_ma1 == tid_ma2);
  // MB
  size_t mb_pos = content.find("\"name\":\"TStable.MB\"");
  REQUIRE(mb_pos != std::string::npos);
  int tid_mb = extract_single_tid(mb_pos);
  REQUIRE(tid_mb != tid_ma1);
}

TEST_CASE("PerfettoOutput JSON escaping for instant event", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_escape.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());
  TestTargetBase              target{"TEsc"};
  TestMetricBase              metric{"EscMetric"};
  const astl::SampleTimestamp base_ts{};
  std::string                 raw = std::string{"Quote:\" Backslash:\\ Newline:\n Tab:\t"};
  astl::ProcessedSamplesMap   processed;
  processed[&target][&metric] = {
      astl::ProcessedSampledData{astl::AstlValue{raw}, base_ts}
  };
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream escape_stream(tmp_guard.path);
  REQUIRE(escape_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(escape_stream)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("Quote:\\\" Backslash:\\\\ Newline:\\n Tab:\\t") != std::string::npos);
}

TEST_CASE("PerfettoOutput skips null target and metric", "[perfetto_output]") {  // NOLINT
  TempFileGuard tmp_guard{"astl_perfetto_nulls.json"};
  {
    astl::PerfettoOutput writer(tmp_guard.path);
    REQUIRE(writer.Ready());

    astl::ProcessedSamplesMap processed;
    TestMetricBase            metric{"ValidMetric"};
    // Insert nullptr target & metric entries to ensure skipping logic holds.
    processed[nullptr][&metric] =
        std::vector<astl::ProcessedSampledData>{MakeSample(static_cast<uint64_t>(5), astl::SampleTimestamp{})};
    // Removed invalid fake target pointer with nullptr metric to avoid undefined behavior.

    REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  }
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  // No valid target+metric pairs -> result should be an empty array body.
  // Expect only metadata events (process/thread) and no counter or instant events.
  REQUIRE(content.find("\"ph\":\"C\"") == std::string::npos);
  REQUIRE(content.find("\"ph\":\"I\"") == std::string::npos);
}

TEST_CASE("PerfettoOutput skips empty samples vector", "[perfetto_output]") {  // NOLINT
  TempFileGuard tmp_guard{"astl_perfetto_empty_samples.json"};
  {
    astl::PerfettoOutput writer(tmp_guard.path);
    REQUIRE(writer.Ready());
    TestTargetBase            target{"TEmpty"};
    TestMetricBase            metric{"MEmpty"};
    astl::ProcessedSamplesMap processed;
    processed[&target][&metric] = {};  // empty vector
    REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  }
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  // Expect process metadata (pid) but no counter events since sample vector empty.
  REQUIRE(content.find("\"ph\":\"M\"") != std::string::npos);  // process metadata
  REQUIRE(content.find("\"ph\":\"C\"") == std::string::npos);  // no counter events
}

TEST_CASE("PerfettoOutput category inference - power & temperature (unit-based)", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_category_power_temp.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());

  TestTargetBase target{"CatT"};
  class UnitMetric : public astl::IMetric {  // Expanded to satisfy braces style
   public:
    UnitMetric(std::string name_param, astl_units_t units_param) : name_(std::move(name_param)), units_(units_param) {}
    bool CheckCapabilities(const astl::Capabilities& caps) const override {
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
      if (!props) {
        return ASTL_STATUS_BAD_ARGUMENT;
      }
      props->_handle = this;
      props->_name   = name_.c_str();
      props->_units  = units_;
      return ASTL_STATUS_SUCCESS;
    }
    auto             Name() const -> std::string const& override { return name_; }
    astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
      (void)processed_sample;
      return ASTL_STATUS_SUCCESS;
    }

   private:
    std::string  name_;
    astl_units_t units_{};  // default member init
  };
  UnitMetric                  power_metric{"SoC Power", ASTL_UNITS_WATTS};
  UnitMetric                  temp_metric{"Die Temperature", ASTL_UNITS_CELSIUS};
  const astl::SampleTimestamp base_timestamp{};
  astl::ProcessedSamplesMap   processed;
  processed[&target][&power_metric] =
      std::vector<astl::ProcessedSampledData>{MakeSample(static_cast<uint64_t>(1), base_timestamp)};
  processed[&target][&temp_metric] =
      std::vector<astl::ProcessedSampledData>{MakeSample(static_cast<uint64_t>(2), base_timestamp)};
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("\"cat\":\"Power\"") != std::string::npos);
  REQUIRE(content.find("\"cat\":\"Temperature\"") != std::string::npos);
}

TEST_CASE("PerfettoOutput category inference - frequency (unit-based)", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_category_frequency.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());
  TestTargetBase target{"CatT2"};
  class FreqMetric : public astl::IMetric {
   public:
    FreqMetric() : name_("Core Frequency"), units_(ASTL_UNITS_MHERTZ) {}
    bool CheckCapabilities(const astl::Capabilities& capabilities) const override {
      (void)capabilities;
      return true;
    }
    std::expected<astl::OperationSequence, astl_status_code> GetOperations() override {
      return astl::OperationSequence{};
    }
    astl_status_code ReceiveRawSample(const astl::RawSampledData& raw_sample) override {
      (void)raw_sample;
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
    astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
      (void)processed_sample;
      return ASTL_STATUS_SUCCESS;
    }

   private:
    std::string  name_;
    astl_units_t units_{};  // default member init
  } metric;
  const astl::SampleTimestamp base_timestamp{};
  astl::ProcessedSamplesMap   processed;
  processed[&target][&metric] = {MakeSample(static_cast<uint64_t>(1000), base_timestamp)};
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("\"cat\":\"Frequency\"") != std::string::npos);
}

TEST_CASE("PerfettoOutput category inference - voltage (unit-based)", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_category_voltage.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());
  TestTargetBase target{"CatT2"};
  class VoltMetric : public astl::IMetric {
   public:
    VoltMetric() : name_("VDD Voltage"), units_(ASTL_UNITS_VOLTS) {}
    bool CheckCapabilities(const astl::Capabilities& capabilities) const override {
      (void)capabilities;
      return true;
    }
    std::expected<astl::OperationSequence, astl_status_code> GetOperations() override {
      return astl::OperationSequence{};
    }
    astl_status_code ReceiveRawSample(const astl::RawSampledData& raw_sample) override {
      (void)raw_sample;
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
    astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
      (void)processed_sample;
      return ASTL_STATUS_SUCCESS;
    }

   private:
    std::string  name_;
    astl_units_t units_{};  // default member init
  } metric;
  const astl::SampleTimestamp base_timestamp{};
  astl::ProcessedSamplesMap   processed;
  processed[&target][&metric] = {MakeSample(static_cast<uint64_t>(1200), base_timestamp)};
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("\"cat\":\"Voltage\"") != std::string::npos);
}

TEST_CASE("PerfettoOutput category inference - state (unit-based)", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_category_state.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());
  TestTargetBase target{"CatT2"};
  class StateMetric : public astl::IMetric {
   public:
    StateMetric() : name_("Power State"), units_(ASTL_UNITS_NONE) {}
    bool CheckCapabilities(const astl::Capabilities& capabilities) const override {
      (void)capabilities;
      return true;
    }
    std::expected<astl::OperationSequence, astl_status_code> GetOperations() override {
      return astl::OperationSequence{};
    }
    astl_status_code ReceiveRawSample(const astl::RawSampledData& raw_sample) override {
      (void)raw_sample;
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
    astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
      (void)processed_sample;
      return ASTL_STATUS_SUCCESS;
    }

   private:
    std::string  name_;
    astl_units_t units_{};  // default member init
  } metric;
  const astl::SampleTimestamp base_timestamp{};
  astl::ProcessedSamplesMap   processed;
  processed[&target][&metric] = {
      astl::ProcessedSampledData{astl::AstlValue{std::string{"EnteringSleep"}}, base_timestamp}
  };
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream(tmp_guard.path);
  REQUIRE(input_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("\"cat\":\"State\"") != std::string::npos);
}

TEST_CASE("PerfettoOutput category inference fallback (unit-based)", "[perfetto_output]") {  // NOLINT
  TempFileGuard        tmp_guard{"astl_perfetto_category_fallback.json"};
  astl::PerfettoOutput writer(tmp_guard.path);
  REQUIRE(writer.Ready());
  TestTargetBase target{"FTT"};
  class UnitMetricFallback : public astl::IMetric {  // NOLINT(cppcoreguidelines-special-member-functions)
   public:
    UnitMetricFallback() : name_("MiscData") {}
    bool CheckCapabilities(const astl::Capabilities& caps) const override {
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
      if (!props) {
        return ASTL_STATUS_BAD_ARGUMENT;
      }
      props->_handle = this;
      props->_name   = name_.c_str();
      props->_units  = units_;
      return ASTL_STATUS_SUCCESS;
    }
    auto             Name() const -> std::string const& override { return name_; }
    astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
      (void)processed_sample;
      return ASTL_STATUS_SUCCESS;
    }

   private:
    std::string  name_;
    astl_units_t units_{ASTL_UNITS_NONE};
  } metric;
  const astl::SampleTimestamp base_timestamp{};
  astl::ProcessedSamplesMap   processed;
  processed[&target][&metric] =
      std::vector<astl::ProcessedSampledData>{MakeSample(static_cast<uint64_t>(5), base_timestamp)};
  REQUIRE(writer.WriteProcessedSamples(processed) == ASTL_STATUS_SUCCESS);
  std::ifstream input_stream3(tmp_guard.path);
  REQUIRE(input_stream3.is_open());
  std::string content((std::istreambuf_iterator<char>(input_stream3)), std::istreambuf_iterator<char>());
  // Fallback should now be an empty category string ("cat":"")
  REQUIRE(content.find("\"cat\":\"\"") != std::string::npos);
}

// New test: validate deferred Perfetto emission occurs only at StopCollection.
TEST_CASE("PerfettoOutput deferred emission via Orchestrator StopCollection", "[perfetto_output]") {  // NOLINT
  // Arrange: set environment variable to a temp file path and ensure it does not yet exist.
  TempFileGuard   tmp_guard{"astl_perfetto_deferred.json"};
  const auto&     perfetto_path = tmp_guard.path;
  std::error_code remove_error_code;
  std::filesystem::remove(perfetto_path, remove_error_code);
  // Guarantee clean start.
  REQUIRE_FALSE(std::filesystem::exists(perfetto_path));
  // setenv returns 0 on success.
  REQUIRE(astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", perfetto_path.string()) == ASTL_STATUS_SUCCESS);

  // Build minimal orchestrator with mocks + real OutputManager (for Perfetto path).
  auto topology_manager = std::make_unique<MockTopologyManager>();
  // Prepare single concrete target (use TestTargetBase from existing test helpers).
  auto                                        concrete_target = std::make_unique<TestTargetBase>("DeferredT");
  auto*                                       target_ptr      = concrete_target.get();
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(concrete_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  REQUIRE(topology_manager->GetTargets().size() == 1);

  auto collector_manager = std::make_unique<MockCollectorManager>();
  // Collector manager expectations used during orchestrator construction / stop.
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  // Metric manager should process & summarize successfully.
  ALLOW_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<astl::OutputManager>();

  // Construct orchestrator (throws on null managers; all non-null here).
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));

  // Create a concrete metric and sink a processed sample prior to StopCollection.
  TestMetricBase                          metric{"DeferredMetric", ASTL_UNITS_WATTS};
  const astl::SampleTimestamp             ts_base{};  // epoch
  std::vector<astl::ProcessedSampledData> processed_samples;
  processed_samples.emplace_back(MakeSample(static_cast<uint64_t>(123), ts_base));
  // Invoke sink to populate orchestrator's processed samples map.
  REQUIRE(orchestrator.SinkProcessedSamples(target_ptr, &metric,
                                            std::span<const astl::ProcessedSampledData>(processed_samples)) ==
          ASTL_STATUS_SUCCESS);

  // Assert: file still not created before StopCollection (deferred emission).
  REQUIRE_FALSE(std::filesystem::exists(perfetto_path));

  // Act: call StopCollection to trigger deferred emission.
  REQUIRE(orchestrator.StopCollection(target_ptr) == ASTL_STATUS_SUCCESS);

  // Assert: file now exists and contains expected composite name.
  REQUIRE(std::filesystem::exists(perfetto_path));
  std::ifstream trace_stream(perfetto_path);
  REQUIRE(trace_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(trace_stream)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(content.empty());
  // Composite name sanitized (no spaces) "DeferredT.DeferredMetric" and value 123 present.
  REQUIRE(content.find("\"name\":\"DeferredT.DeferredMetric\"") != std::string::npos);
  REQUIRE(content.find("\"value\":123") != std::string::npos);

  // Cleanup env var for isolation.
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");  // clear
}

// Negative path: env var set but no processed samples sunk; expect empty trace file body after StopCollection.
TEST_CASE("PerfettoOutput deferred emission empty map", "[perfetto_output]") {  // NOLINT
  TempFileGuard   tmp_guard{"astl_perfetto_empty_deferred.json"};
  const auto&     perfetto_path = tmp_guard.path;
  std::error_code remove_error_code;
  std::filesystem::remove(perfetto_path, remove_error_code);
  REQUIRE_FALSE(std::filesystem::exists(perfetto_path));
  REQUIRE(astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", perfetto_path.string()) == ASTL_STATUS_SUCCESS);

  auto                                        topology_manager = std::make_unique<MockTopologyManager>();
  auto                                        concrete_target  = std::make_unique<TestTargetBase>("EmptyDeferredT");
  auto*                                       target_ptr       = concrete_target.get();
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(concrete_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto               output_manager = std::make_unique<astl::OutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));

  // No SinkProcessedSamples calls -> processed map remains empty.
  REQUIRE_FALSE(std::filesystem::exists(perfetto_path));
  REQUIRE(orchestrator.StopCollection(target_ptr) == ASTL_STATUS_SUCCESS);

  REQUIRE(std::filesystem::exists(perfetto_path));
  std::ifstream trace_stream(perfetto_path);
  REQUIRE(trace_stream.is_open());
  std::string content((std::istreambuf_iterator<char>(trace_stream)), std::istreambuf_iterator<char>());
  // Empty body variants (writer destructor closes array). Accept a few expected forms.
  bool matches_variant =
      (content == "[\n\n]\n") || (content == "[\n]\n") || (content == "[]\n") || (content == "[]") ||
      (content == "[\n") ||
      (content.find("\"displayTimeUnit\":\"us\"") != std::string::npos &&
       content.find("\"ph\":\"C\"") == std::string::npos && content.find("\"ph\":\"I\"") == std::string::npos);
  REQUIRE(matches_variant);
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");
}

// Env var unset: ensure no file is emitted after StopCollection even if samples exist.
TEST_CASE("PerfettoOutput deferred emission env var unset", "[perfetto_output]") {  // NOLINT
  auto            perfetto_path = std::filesystem::temp_directory_path() / "astl_perfetto_unset_env.json";
  std::error_code remove_error_code;
  std::filesystem::remove(perfetto_path, remove_error_code);
  REQUIRE_FALSE(std::filesystem::exists(perfetto_path));
  // Explicitly ensure variable not set (ignore errors).
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");

  auto                                        topology_manager = std::make_unique<MockTopologyManager>();
  auto                                        concrete_target  = std::make_unique<TestTargetBase>("UnsetEnvT");
  auto*                                       target_ptr       = concrete_target.get();
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(concrete_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto               output_manager = std::make_unique<astl::OutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));

  // Sink one processed sample to show data exists but env var unset prevents emission.
  TestMetricBase                          metric{"UnsetMetric"};
  const astl::SampleTimestamp             ts_base{};
  std::vector<astl::ProcessedSampledData> processed_samples;
  processed_samples.emplace_back(MakeSample(static_cast<uint64_t>(777), ts_base));
  REQUIRE(orchestrator.SinkProcessedSamples(target_ptr, &metric,
                                            std::span<const astl::ProcessedSampledData>(processed_samples)) ==
          ASTL_STATUS_SUCCESS);

  REQUIRE(orchestrator.StopCollection(target_ptr) == ASTL_STATUS_SUCCESS);
  // Assert: still no file.
  REQUIRE_FALSE(std::filesystem::exists(perfetto_path));
}
