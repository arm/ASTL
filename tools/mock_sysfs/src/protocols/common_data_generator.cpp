#include "common_data_generator.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace mock_sysfs {

static const auto kProgramStart = std::chrono::steady_clock::now();

static std::mt19937& GetEngine() {
  static std::random_device rand_dev;
  static std::mt19937       engine(rand_dev());
  return engine;
}
//------------------------------------------------------------------------------
// CounterRampGenerator
//------------------------------------------------------------------------------
template <typename T>
requires AllowedIntegral<T>
T CounterRampGenerator<T>::GenerateRamp() {
  T val          = current_value_;
  current_value_ = current_value_ + step_value_;
  return val;
}

//------------------------------------------------------------------------------
// NoisyRampGenerator
//------------------------------------------------------------------------------
template <typename T>
requires(AllowedFloating<T>)
T NoisyRampGenerator<T>::GenerateNoisyRamp() {
  double base = current_value_;
  current_value_ += slope_per_sample_;
  // Add Gaussian noise
  return static_cast<T>(base + dist_(GetEngine()));
}

//------------------------------------------------------------------------------
// StepGenerator<T>
//------------------------------------------------------------------------------
template <typename T>
requires(AllowedIntegral<T> || AllowedFloating<T>)
T StepGenerator<T>::GenerateStep() {
  std::size_t intervals = call_count_ / step_index_;
  T           val       = initial_value_ + (static_cast<T>(intervals) * step_value_);
  ++call_count_;
  return val;
}

//------------------------------------------------------------------------------
// PulseGenerator
//------------------------------------------------------------------------------
template <typename T>
requires(AllowedIntegral<T> || AllowedFloating<T>)
T PulseGenerator<T>::GeneratePulse() {
  std::size_t current  = call_count_;
  bool        in_pulse = (current >= random_index_) && (current < random_index_ + width_);
  T           val      = in_pulse ? magnitude_ : T{};
  ++call_count_;
  if (current == random_index_ + width_ - 1) {
    std::uniform_int_distribution<std::size_t> dist(1, max_interval_);
    random_index_ = call_count_ + dist(GetEngine());
  }
  return val;
}

//------------------------------------------------------------------------------
// CSVDataGenerator
//------------------------------------------------------------------------------
CSVDataGenerator::CSVDataGenerator(const std::string& csv_path) {
  std::ifstream file(csv_path);
  if (!file.is_open()) {
    return;
  }

  std::string line;
  bool        title = true;  // csv starts with timestamp,data
  while (std::getline(file, line)) {
    if (title) {
      title = false;
      continue;
    }
    std::stringstream str_stream(line);

    std::string ts_str;
    std::string data_str;
    if (!std::getline(str_stream, ts_str, ',') || !std::getline(str_stream, data_str)) {
      continue;
    }

    try {
      uint64_t ts_uint64 = std::stoull(ts_str);
      timestamps_.push_back(ts_uint64);
      data_.push_back(data_str);
    } catch (...) {
      // skip malformed
    }
  }
}

std::string CSVDataGenerator::GenerateCSV() {
  // millisecond count since program start
  uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kProgramStart).count());

  // upper_bound finds first element > now_ms
  auto it = std::upper_bound(timestamps_.begin(), timestamps_.end(), now_ms);
  if (it == timestamps_.begin()) {
    return "";
  }
  size_t idx = std::distance(timestamps_.begin(), it) - 1;
  return data_[idx];
}

//------------------------------------------------------------------------------
// Explicit instantiations
//------------------------------------------------------------------------------

template class CounterRampGenerator<uint8_t>;
template class CounterRampGenerator<uint16_t>;
template class CounterRampGenerator<uint32_t>;
template class CounterRampGenerator<uint64_t>;
template class CounterRampGenerator<int8_t>;
template class CounterRampGenerator<int16_t>;
template class CounterRampGenerator<int32_t>;
template class CounterRampGenerator<int64_t>;

template class NoisyRampGenerator<float>;
template class NoisyRampGenerator<double>;

template class StepGenerator<uint8_t>;
template class StepGenerator<uint16_t>;
template class StepGenerator<uint32_t>;
template class StepGenerator<uint64_t>;
template class StepGenerator<int8_t>;
template class StepGenerator<int16_t>;
template class StepGenerator<int32_t>;
template class StepGenerator<int64_t>;
template class StepGenerator<float>;
template class StepGenerator<double>;

template class PulseGenerator<uint8_t>;
template class PulseGenerator<uint16_t>;
template class PulseGenerator<uint32_t>;
template class PulseGenerator<uint64_t>;
template class PulseGenerator<int8_t>;
template class PulseGenerator<int16_t>;
template class PulseGenerator<int32_t>;
template class PulseGenerator<int64_t>;
template class PulseGenerator<float>;
template class PulseGenerator<double>;

}  // namespace mock_sysfs
