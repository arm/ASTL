#ifndef INCLUDE_COMMON_DATA_GENERATOR_HPP_
#define INCLUDE_COMMON_DATA_GENERATOR_HPP_

#include <concepts>
#include <random>
#include <vector>

namespace mock_sysfs {

template <typename T>
concept AllowedUnsignedIntegral =
    std::same_as<T, uint8_t> || std::same_as<T, uint16_t> || std::same_as<T, uint32_t> || std::same_as<T, uint64_t>;

template <typename T>
concept AllowedSignedIntegral =
    std::same_as<T, int8_t> || std::same_as<T, int16_t> || std::same_as<T, int32_t> || std::same_as<T, int64_t>;

template <typename T>
concept AllowedIntegral = AllowedUnsignedIntegral<T> || AllowedSignedIntegral<T>;

template <typename T>
concept AllowedFloating = std::same_as<T, float> || std::same_as<T, double>;

template <typename T>
concept AllowedString = std::same_as<T, std::string>;

/**
 * @brief Generator for a monotonic counter ramp.
 *
 * Produces a non-decreasing (except for overflow) sequence:
 * each call to GenerateRamp() returns the current counter value,
 * then increments by a fixed step.
 *
 * @tparam T  An integral type satisfying AllowedIntegral<T>.
 */
template <typename T>
requires AllowedIntegral<T>
class CounterRampGenerator {
 public:
  /**
   * @param start_value Initial counter value.
   * @param step_value  Increment per call.
   */
  CounterRampGenerator(uint64_t start_value, uint64_t step_value)
      : current_value_(start_value), step_value_(step_value) {}

  /**
   * @brief Generate the next counter value.
   * @return Next counter sample.
   */
  T GenerateRamp();

 private:
  T current_value_{};
  T step_value_{};
};

/**
 * @brief Generator for a noisy ramp (linear trend + Gaussian noise).
 *
 * Each call to GenerateNoisyRamp() returns the next sample combining drift and noise.
 *
 * @tparam T  A floating‐point type satisfying AllowedFloating<T>.
 */
template <typename T>
requires(AllowedFloating<T>)
class NoisyRampGenerator {
 public:
  /**
   * @param start_value      Starting value of the ramp.
   * @param slope_per_sample Increment per call.
   * @param noise_stddev     Standard deviation for Gaussian noise.
   */
  NoisyRampGenerator(double start_value, double slope_per_sample, double noise_stddev)
      : current_value_(start_value), slope_per_sample_(slope_per_sample), dist_(0.0, noise_stddev) {}

  /**
   * @brief Generate the next noisy ramp value.
   * @return Next sample value.
   */
  T GenerateNoisyRamp();

 private:
  T                           current_value_{};
  T                           slope_per_sample_{};
  std::normal_distribution<T> dist_;
};

/**
 * @brief Generator for a step change signal.
 *
 * Outputs initial_value for calls before step_index, then step_value thereafter.
 *
 * @tparam T Data type of the signal.
 */
template <typename T>
requires(AllowedIntegral<T> || AllowedFloating<T>)
class StepGenerator {
 public:
  /**
   * @param initial_value Value before the step.
   * @param step_value    Value at and after the step.
   * @param step_index    Number of calls before switching to step_value.
   */
  StepGenerator(T initial_value, T step_value, std::size_t step_index)
      : initial_value_(initial_value), step_value_(step_value), step_index_(step_index) {}

  /**
   * @brief Generate the next step-change sample.
   * @return Next sample of type T.
   */
  T GenerateStep();

 private:
  T           initial_value_{};
  T           step_value_{};
  std::size_t step_index_{};
  std::size_t call_count_{};
};

/**
 * @brief Generator for impulse/pulse trains.
 *
 * Produces spikes of a given magnitude and width at specified call indices.
 * Each call to GeneratePulse() returns either 0 or the pulse magnitude.
 *
 * @tparam T  A numeric type satisfying AllowedIntegral<T> or AllowedFloating<T>.
 */
template <typename T>
requires(AllowedIntegral<T> || AllowedFloating<T>)
class PulseGenerator {
 public:
  /**
   * @param magnitude     Amplitude of each pulse.
   * @param width         Width of each pulse in calls (default = 1).
   * @param max_interval  Maximum spacing (in calls) between pulses (>=1).
   */
  PulseGenerator(T magnitude, std::size_t width, std::size_t max_interval)
      : magnitude_(magnitude), width_(width), max_interval_(max_interval) {}

  /**
   * @brief Generate the next pulse sample.
   * @return magnitude if within a pulse, otherwise 0.
   */
  T GeneratePulse();

 private:
  std::size_t random_index_{};
  std::size_t max_interval_{};
  T           magnitude_{};
  std::size_t width_{};
  std::size_t call_count_{};
};

/**
 * @brief Generator that pulls data from a timestamped CSV.
 *
 * On each Generate(), returns the data field corresponding to the latest
 * CSV timestamp <= now. CSV format is two columns: timestamp,data
 * (timestamp in milliseconds).
 */
class CSVDataGenerator {
 public:
  /**
   * @param csv_path Filesystem path to the CSV file.
   */
  explicit CSVDataGenerator(const std::string& csv_path, uint8_t column);

  /**
   * @brief Generate returns the data value for the most recent timestamp <= now.
   * @return Data string from CSV, or empty if no timestamp <= now.
   */
  std::string GenerateCSV();

 private:
  std::vector<uint64_t>    timestamps_;
  std::vector<std::string> data_;
};

// -----------------------------------------------------------------------------
// Extern‐template declarations to prevent implicit instantiation in other TUs.
// Each concrete type used elsewhere must match an explicit instantiation
// in the corresponding .cpp file.
// -----------------------------------------------------------------------------

extern template class CounterRampGenerator<uint8_t>;
extern template class CounterRampGenerator<uint16_t>;
extern template class CounterRampGenerator<uint32_t>;
extern template class CounterRampGenerator<uint64_t>;
extern template class CounterRampGenerator<int8_t>;
extern template class CounterRampGenerator<int16_t>;
extern template class CounterRampGenerator<int32_t>;
extern template class CounterRampGenerator<int64_t>;

extern template class NoisyRampGenerator<float>;
extern template class NoisyRampGenerator<double>;

extern template class StepGenerator<uint8_t>;
extern template class StepGenerator<uint16_t>;
extern template class StepGenerator<uint32_t>;
extern template class StepGenerator<uint64_t>;
extern template class StepGenerator<int8_t>;
extern template class StepGenerator<int16_t>;
extern template class StepGenerator<int32_t>;
extern template class StepGenerator<int64_t>;
extern template class StepGenerator<float>;
extern template class StepGenerator<double>;

extern template class PulseGenerator<uint8_t>;
extern template class PulseGenerator<uint16_t>;
extern template class PulseGenerator<uint32_t>;
extern template class PulseGenerator<uint64_t>;
extern template class PulseGenerator<int8_t>;
extern template class PulseGenerator<int16_t>;
extern template class PulseGenerator<int32_t>;
extern template class PulseGenerator<int64_t>;
extern template class PulseGenerator<float>;
extern template class PulseGenerator<double>;

}  // namespace mock_sysfs

#endif  // INCLUDE_COMMON_DATA_GENERATOR_HPP_
