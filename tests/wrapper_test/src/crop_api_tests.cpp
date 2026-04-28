// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crop_api_tests.cpp
 * @brief Tests for the astlCropSamplesOnTarget, astlCropMetricSamplesOnTarget, and astlCropSamples C APIs.
 *
 * Coverage:
 *  - NULL params pointer
 *  - Incompatible params struct size
 *  - Non-zero params flags
 *  - Window array validation (NULL, window_count == 0, wrong windows[0].size,
 *    non-zero window flags, start_ts > end_ts)
 *  - Valid call returns ASTL_STATUS_NOT_IMPLEMENTED (implementation pending)
 *
 * Note: astlCropSamplesOnTarget does not yet validate the windows array; those
 * checks are only exercised for astlCropMetricSamplesOnTarget and astlCropSamples.
 */

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"

// ---------------------------------------------------------------------------
// Sentinel handles used throughout
// ---------------------------------------------------------------------------
namespace {
const int                  kSentinelTarget{};
const int                  kSentinelMetric{};
astl_target_handle_t const kTarget = static_cast<astl_target_handle_t>(&kSentinelTarget);
astl_metric_handle_t const kMetric = static_cast<astl_metric_handle_t>(&kSentinelMetric);
}  // namespace

// ===========================================================================
// astlCropSamplesOnTarget
// ===========================================================================

TEST_CASE("astlCropSamplesOnTarget - NULL params", "[crop_api]") {
  REQUIRE(astlCropSamplesOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlCropSamplesOnTarget - incompatible struct size", "[crop_api]") {
  astl_crop_window_t                   window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_on_target_params_t params{};
  params.target_handle = kTarget;
  params.windows       = &window;
  params.window_count  = 1;
  params.flags         = 0;

  SECTION("size too small") {
    params.size = sizeof(astl_crop_samples_on_target_params_t) - 1;
    REQUIRE(astlCropSamplesOnTarget(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("size too large") {
    params.size = sizeof(astl_crop_samples_on_target_params_t) + 1;
    REQUIRE(astlCropSamplesOnTarget(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }
}

TEST_CASE("astlCropSamplesOnTarget - non-zero params flags", "[crop_api]") {
  astl_crop_window_t                   window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_samples_on_target_params_t);
  params.flags         = 1U;
  params.target_handle = kTarget;
  params.windows       = &window;
  params.window_count  = 1;
  REQUIRE(astlCropSamplesOnTarget(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlCropSamplesOnTarget - valid params returns NOT_IMPLEMENTED", "[crop_api]") {
  REQUIRE(CropSamplesOnTarget(kTarget, 0, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlCropSamplesOnTarget - valid params with non-zero window bounds returns NOT_IMPLEMENTED", "[crop_api]") {
  REQUIRE(CropSamplesOnTarget(kTarget, 1'000'000, 5'000'000) == ASTL_STATUS_NOT_IMPLEMENTED);
}

// ===========================================================================
// astlCropMetricSamplesOnTarget
// ===========================================================================

TEST_CASE("astlCropMetricSamplesOnTarget - NULL params", "[crop_api]") {
  REQUIRE(astlCropMetricSamplesOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlCropMetricSamplesOnTarget - incompatible struct size", "[crop_api]") {
  astl_crop_window_t                          window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_metric_samples_on_target_params_t params{};
  params.target_handle = kTarget;
  params.metric_handle = kMetric;
  params.windows       = &window;
  params.window_count  = 1;
  params.flags         = 0;

  SECTION("size too small") {
    params.size = sizeof(astl_crop_metric_samples_on_target_params_t) - 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("size too large") {
    params.size = sizeof(astl_crop_metric_samples_on_target_params_t) + 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }
}

TEST_CASE("astlCropMetricSamplesOnTarget - non-zero params flags", "[crop_api]") {
  astl_crop_window_t                          window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_metric_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_metric_samples_on_target_params_t);
  params.flags         = 1U;
  params.target_handle = kTarget;
  params.metric_handle = kMetric;
  params.windows       = &window;
  params.window_count  = 1;
  REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlCropMetricSamplesOnTarget - window array validation", "[crop_api]") {
  astl_crop_metric_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_metric_samples_on_target_params_t);
  params.flags         = 0;
  params.target_handle = kTarget;
  params.metric_handle = kMetric;

  SECTION("NULL windows pointer") {
    params.windows      = nullptr;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("window_count is zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, 0, 0};
    params.windows      = &window;
    params.window_count = 0;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("windows[0].size is wrong") {
    astl_crop_window_t window{sizeof(astl_crop_window_t) - 1, 0, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("windows[0].flags is non-zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), /*flags=*/1U, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("start_ts > end_ts (both non-zero)") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/5'000'000, /*end_ts=*/1'000'000};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE("astlCropMetricSamplesOnTarget - valid params returns NOT_IMPLEMENTED", "[crop_api]") {
  REQUIRE(CropMetricSamplesOnTarget(kTarget, kMetric, 0, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlCropMetricSamplesOnTarget - valid params with non-zero window bounds returns NOT_IMPLEMENTED",
          "[crop_api]") {
  REQUIRE(CropMetricSamplesOnTarget(kTarget, kMetric, 1'000'000, 5'000'000) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlCropMetricSamplesOnTarget - start_ts == end_ts (single-point window) is valid", "[crop_api]") {
  REQUIRE(CropMetricSamplesOnTarget(kTarget, kMetric, 3'000'000, 3'000'000) == ASTL_STATUS_NOT_IMPLEMENTED);
}

// ===========================================================================
// astlCropSamples
// ===========================================================================

TEST_CASE("astlCropSamples - NULL params", "[crop_api]") {
  REQUIRE(astlCropSamples(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlCropSamples - incompatible struct size", "[crop_api]") {
  astl_crop_window_t         window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_params_t params{};
  params.windows      = &window;
  params.window_count = 1;
  params.flags        = 0;

  SECTION("size too small") {
    params.size = sizeof(astl_crop_samples_params_t) - 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("size too large") {
    params.size = sizeof(astl_crop_samples_params_t) + 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }
}

TEST_CASE("astlCropSamples - non-zero params flags", "[crop_api]") {
  astl_crop_window_t         window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_params_t params{};
  params.size         = sizeof(astl_crop_samples_params_t);
  params.flags        = 1U;
  params.windows      = &window;
  params.window_count = 1;
  REQUIRE(astlCropSamples(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlCropSamples - window array validation", "[crop_api]") {
  astl_crop_samples_params_t params{};
  params.size  = sizeof(astl_crop_samples_params_t);
  params.flags = 0;

  SECTION("NULL windows pointer") {
    params.windows      = nullptr;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("window_count is zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, 0, 0};
    params.windows      = &window;
    params.window_count = 0;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("windows[0].size is wrong") {
    astl_crop_window_t window{sizeof(astl_crop_window_t) + 1, 0, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }

  SECTION("windows[0].flags is non-zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), /*flags=*/1U, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("start_ts > end_ts (both non-zero)") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/9'000'000, /*end_ts=*/2'000'000};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("start_ts == 0 with non-zero end_ts (no lower bound) is valid") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/0, /*end_ts=*/5'000'000};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_NOT_IMPLEMENTED);
  }

  SECTION("end_ts == 0 with non-zero start_ts (no upper bound) is valid") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/1'000'000, /*end_ts=*/0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

TEST_CASE("astlCropSamples - valid params returns NOT_IMPLEMENTED", "[crop_api]") {
  REQUIRE(CropSamples(0, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlCropSamples - valid params with non-zero window bounds returns NOT_IMPLEMENTED", "[crop_api]") {
  REQUIRE(CropSamples(1'000'000, 5'000'000) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlCropSamples - multiple windows all valid returns NOT_IMPLEMENTED", "[crop_api]") {
  std::array<astl_crop_window_t, 3> windows{};
  windows[0] = {sizeof(astl_crop_window_t), 0, 1'000'000, 2'000'000};
  windows[1] = {sizeof(astl_crop_window_t), 0, 4'000'000, 6'000'000};
  windows[2] = {sizeof(astl_crop_window_t), 0, 0, 8'000'000};

  astl_crop_samples_params_t params{};
  params.size         = sizeof(astl_crop_samples_params_t);
  params.flags        = 0;
  params.windows      = windows.data();
  params.window_count = static_cast<uint32_t>(windows.size());
  REQUIRE(astlCropSamples(&params) == ASTL_STATUS_NOT_IMPLEMENTED);
}
