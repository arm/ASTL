#ifndef ASTL_MOCK_CLASSES_H_
#define ASTL_MOCK_CLASSES_H_

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "counter.hpp"
#include "target.hpp"

/**
 * @brief A mockable implementation of the astl::ITarget interface
 *
 * Set up expected function calls and their results and side effects with REQUIRE_CALL and ALLOW_CALL
 */
struct MockTarget : public astl::ITarget {
 private:
  std::vector<std::unique_ptr<astl::ICounter>> _counters;

 public:
  static constexpr bool trompeloeil_movable_mock = true;  // cppcheck-suppress unusedStructMember

  MockTarget() = default;
  explicit MockTarget(std::vector<std::unique_ptr<astl::ICounter>> counters) : _counters{std::move(counters)} {}

  MAKE_MOCK1(GetProperties, astl_status_code(astl_target_properties_t* target), override);
  MAKE_CONST_MOCK0(GetCounterCount, size_t(), override);
  std::vector<std::unique_ptr<astl::ICounter>> const& GetCounters() const override { return _counters; };

  MAKE_MOCK2(ConfigureCounterCollection,
             astl_status_code(astl_collection_parameters_t const* const collection_params,
                              std::span<astl::ICounter*>                counters),
             override);

  MAKE_MOCK0(ReadImmediate, astl_status_code(), override);
  MAKE_MOCK0(StartCollection, astl_status_code(), override);
  MAKE_MOCK0(PauseCollection, astl_status_code(), override);
  MAKE_MOCK0(ResumeCollection, astl_status_code(), override);
  MAKE_MOCK0(StopCollection, astl_status_code(), override);
  using RType = std::expected<uint32_t, astl_status_code>;  // define this separately to avoid MACRO expansion quirk
  MAKE_CONST_MOCK1(GetCounterSampleCount, RType(const astl::ICounter*), override);
};

/**
 * @brief A mockable implementation of the astl::ICounter interface
 *
 * Set up expected function calls and their results and side effects with REQUIRE_CALL and ALLOW_CALL
 */
struct MockCounter : public astl::ICounter {
  // clang-format off
  MAKE_MOCK1(GetProperties, auto(astl_counter_properties_t*) -> astl_status_code, override);
  MAKE_MOCK1(ConfigureCollection, auto(astl_collection_parameters_t const* const) -> astl_status_code, override);
  // clang-format on
};

#endif  // ASTL_MOCK_CLASSES_H_