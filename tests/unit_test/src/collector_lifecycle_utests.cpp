// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "collector/collector_lifecycle_helpers.hpp"

namespace {
struct RecordingSampler {
  bool paused = false;
  int  calls  = 0;
  void Pause() {
    paused = true;
    ++calls;
  }
  void Resume() {
    paused = false;
    ++calls;
  }
};
}  // namespace

TEST_CASE("Collector lifecycle helpers preserve marker ordering and sink errors", "[CollectionLifecycle]") {
  using State            = astl::CollectionLifecycleState;
  State            state = State::STARTED;
  RecordingSampler sampler;
  int              markers      = 0;
  const auto       pause_status = astl::collector_detail::PauseCollectionLifecycle(state, &sampler, [&] {
    CHECK(sampler.paused);
    CHECK(state == State::PAUSED);
    ++markers;
    return ASTL_STATUS_FILE_ERROR;
  });
  CHECK(pause_status == ASTL_STATUS_FILE_ERROR);
  CHECK(state == State::PAUSED);

  const auto resume_status = astl::collector_detail::ResumeCollectionLifecycle(state, &sampler, [&] {
    CHECK(sampler.paused);
    CHECK(state == State::PAUSED);
    ++markers;
    return ASTL_STATUS_FILE_ERROR;
  });
  CHECK(resume_status == ASTL_STATUS_FILE_ERROR);
  CHECK(state == State::STARTED);
  CHECK_FALSE(sampler.paused);
  CHECK(sampler.calls == 2);
  CHECK(markers == 2);
}

TEST_CASE("Rejected collector lifecycle calls have no side effects", "[CollectionLifecycle]") {
  using State = astl::CollectionLifecycleState;
  RecordingSampler sampler;
  int              markers = 0;
  auto             emit    = [&] {
    ++markers;
    return ASTL_STATUS_SUCCESS;
  };
  State state = State::STOPPED;
  CHECK(astl::collector_detail::PauseCollectionLifecycle(state, &sampler, emit) ==
        ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
  CHECK(astl::collector_detail::ResumeCollectionLifecycle(state, &sampler, emit) ==
        ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
  CHECK(state == State::STOPPED);
  state = State::UNCONFIGURED;
  CHECK(astl::collector_detail::PauseCollectionLifecycle(state, &sampler, emit) ==
        ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  CHECK(astl::collector_detail::ResumeCollectionLifecycle(state, &sampler, emit) ==
        ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  CHECK(state == State::UNCONFIGURED);
  CHECK(sampler.calls == 0);
  CHECK(markers == 0);
}

TEST_CASE("Collector lifecycle markers work without a periodic sampler", "[CollectionLifecycle]") {
  using State   = astl::CollectionLifecycleState;
  State state   = State::STARTED;
  int   markers = 0;
  auto  emit    = [&] {
    ++markers;
    return ASTL_STATUS_SUCCESS;
  };
  auto* sampler = static_cast<RecordingSampler*>(nullptr);
  CHECK(astl::collector_detail::PauseCollectionLifecycle(state, sampler, emit) == ASTL_STATUS_SUCCESS);
  CHECK(state == State::PAUSED);
  CHECK(astl::collector_detail::ResumeCollectionLifecycle(state, sampler, emit) == ASTL_STATUS_SUCCESS);
  CHECK(state == State::STARTED);
  CHECK(markers == 2);
}
