#include <catch2/catch_test_macros.hpp>

#include "astl/astl.h"

TEST_CASE("astlVersion", "[matches header definition]") {
  astl_version_t version = astlVersion();
  REQUIRE(version._major == ASTL_VERSION_MAJOR);
  REQUIRE(version._minor == ASTL_VERSION_MINOR);
  REQUIRE(version._micro == ASTL_VERSION_MICRO);
}

TEST_CASE("astlVersionString", "[matches header definition]") {
  const char* version_string = astlVersionString();
  REQUIRE(std::string(version_string) == std::string(ASTL_VERSION_STRING));
}

TEST_CASE("astlErrorString", "[matches header definition]") {
  astl_error_code error        = ASTL_ERROR_BAD_ARGUMENT;
  const char*     error_string = astlErrorString(error);
  REQUIRE(std::string(error_string) == "BAD_ARGUMENT");

  REQUIRE(std::string(astlErrorString(ASTL_ERROR_NO_DATA_COLLECTED)) == "NO_DATA_COLLECTED");
  REQUIRE(std::string(astlErrorString(ASTL_ERROR_INTERNAL)) == "INTERNAL");
  // for now at least, anything about ASTL_ERROR_INTERNAL is unknown
  astl_error_code truly_unknown = static_cast<astl_error_code>(ASTL_ERROR_INTERNAL + ASTL_ERROR_BAD_ARGUMENT);
  REQUIRE(std::string(astlErrorString(truly_unknown)) == "UNKNOWN");
  REQUIRE(std::string(astlErrorString(ASTL_ERROR_INTERNAL)) == "INTERNAL");
}

TEST_CASE("astlGetTargetCount", "[unimplemented for now]") {
  REQUIRE(astlGetTargetCount(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetTargets", "[unimplemented for now]") {
  REQUIRE(astlGetTargets(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetCounterCount", "[unimplemented for now]") {
  REQUIRE(astlGetCounterCount(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetCounters", "[unimplemented for now]") {
  REQUIRE(astlGetCounters(nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricCount", "[unimplemented for now]") {
  REQUIRE(astlGetMetricCount(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetrics", "[unimplemented for now]") {
  REQUIRE(astlGetMetrics(nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroupCount", "[unimplemented for now]") {
  REQUIRE(astlGetMetricGroupCount(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroups", "[unimplemented for now]") {
  REQUIRE(astlGetMetricGroups(nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroupMetrics", "[unimplemented for now]") {
  REQUIRE(astlGetMetricGroupMetrics(nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureCounterCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlConfigureCounterCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureCounterCollection", "[unimplemented for now]") {
  REQUIRE(astlConfigureCounterCollection(nullptr, nullptr, 0) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureMetricCollection", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricCollection(nullptr, nullptr, 0) == ASTL_ERROR_NOT_IMPLEMENTED);
}

/*** CONFIGURE METRIC GROUPS ***/
TEST_CASE("astlConfigureMetricGroupCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricGroupCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureMetricGroupCollection", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricGroupCollection(nullptr, nullptr, 0) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlReadImmediateOnTarget", "[unimplemented for now]") {
  REQUIRE(astlReadImmediateOnTarget(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlReadImmediate", "[unimplemented for now]") {
  REQUIRE(astlReadImmediate() == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlStartCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlStartCollectionOnTarget(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlStartCollection", "[unimplemented for now]") {
  REQUIRE(astlStartCollection() == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlPauseCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlPauseCollectionOnTarget(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlPauseCollection", "[unimplemented for now]") {
  REQUIRE(astlPauseCollection() == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlResumeCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlResumeCollectionOnTarget(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlResumeCollection", "[unimplemented for now]") {
  REQUIRE(astlResumeCollection() == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlStopCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlStopCollectionOnTarget(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlStopCollection", "[unimplemented for now]") {
  REQUIRE(astlStopCollection() == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetCounterSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetCounterSampleCountOnTarget(nullptr, nullptr, 0) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetCounterSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetCounterSamplesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSampleCountOnTarget(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSamplesOnTarget(nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSampleCount", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSampleCount(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSamples", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSamples(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

/*** COLLECTED METRIC SAMPLES ***/
TEST_CASE("astlGetMetricSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetMetricSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetMetricSamplesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSampleCountOnTarget(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSamplesOnTarget(nullptr, nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSampleCount", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSampleCount(nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSamples", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSamples(nullptr, nullptr) == ASTL_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("astlTest", "[deprecated for now]") { REQUIRE(astlTest() == ASTL_ERROR_DEPRECATED_API); }
