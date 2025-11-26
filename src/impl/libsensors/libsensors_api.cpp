/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include "libsensors/libsensors_api.hpp"

#include <dlfcn.h>

#include <array>
#include <utility>  // std::exchange

#include "astl_logger.hpp"

// ---------- helpers ----------
static auto TryDlopenCandidates(const std::string& explicit_path, std::string& dl_detail) -> void* {
  dlerror();  // clear
  if (!explicit_path.empty()) {
    if (void* handle = dlopen(explicit_path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
      return handle;
    }
    if (const char* dlerror_result = dlerror()) {
      dl_detail = dlerror_result;
    }
    return nullptr;
  }

  // Try common SONAMEs first; adjust as needed
  static constexpr std::array<const char*, 3> candidates = {"libsensors.so.5",
                                                            "libsensors.so.4",  // in case of older distros
                                                            "libsensors.so"};

  for (const char* name : candidates) {
    dlerror();
    if (void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
      return handle;
    }
    if (const char* dlerror_result = dlerror()) {
      dl_detail = dlerror_result;  // keep last error text
    }
  }
  return nullptr;
}

/**
 * @brief Helper to load a symbol from a dlopen handle
 *        If there's a problem, `dl_detail` will be updated, and false returned.
 */
template <class T>
static auto LoadSym(void* handle, const char* symbol, T& out_fn, std::string& dl_detail) -> bool {
  dlerror();
  void* fpointer = dlsym(handle, symbol);
  if (const char* dlerror_result = dlerror()) {
    dl_detail = dlerror_result;
    return false;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  out_fn = reinterpret_cast<T>(fpointer);
  return true;
}

// ---------- SensorsApi impl ----------

SensorsApi::SensorsApi(void* handle) : _handle{handle} {}

auto SensorsApi::Create(std::string explicit_path) -> std::shared_ptr<SensorsApi> {
  std::string detail;
  void*       handle = TryDlopenCandidates(explicit_path, detail);
  if (!handle) {
    return {};
  }

  auto ptr = std::shared_ptr<SensorsApi>(new SensorsApi(handle));

  // Resolve only what's really needed
  bool is_ok = true;
  is_ok &= LoadSym(handle, "sensors_init", ptr->init, detail);
  is_ok &= LoadSym(handle, "sensors_cleanup", ptr->cleanup, detail);
  is_ok &= LoadSym(handle, "sensors_snprintf_chip_name", ptr->snprintf_chip_name, detail);
  is_ok &= LoadSym(handle, "sensors_get_label", ptr->get_label, detail);
  is_ok &= LoadSym(handle, "sensors_get_value", ptr->get_value, detail);
  is_ok &= LoadSym(handle, "sensors_get_detected_chips", ptr->get_detected_chips, detail);
  is_ok &= LoadSym(handle, "sensors_get_features", ptr->get_features, detail);
  is_ok &= LoadSym(handle, "sensors_get_subfeature", ptr->get_subfeature, detail);

  if (!is_ok || !ptr->Ok()) {
    ASTL_LOG_ERROR("SensorsApi::Create: Failed to load required symbols: {}", detail);
    // Clean up before returning error
    dlclose(handle);
    return {};
  }
  if (ptr->init(nullptr) != 0) {
    ASTL_LOG_ERROR("SensorsApi::Create: sensors_init failed");
    dlclose(handle);
    return {};
  }
  return ptr;
}

// ----- moves & dtor ------
SensorsApi::SensorsApi(SensorsApi&& other) noexcept
    : init(std::exchange(other.init, nullptr)),
      cleanup(std::exchange(other.cleanup, nullptr)),
      snprintf_chip_name(std::exchange(other.snprintf_chip_name, nullptr)),
      get_label(std::exchange(other.get_label, nullptr)),
      get_value(std::exchange(other.get_value, nullptr)),
      get_detected_chips(std::exchange(other.get_detected_chips, nullptr)),
      get_features(std::exchange(other.get_features, nullptr)),
      get_subfeature(std::exchange(other.get_subfeature, nullptr)),
      _handle(std::exchange(other._handle, nullptr)) {}

SensorsApi& SensorsApi::operator=(SensorsApi&& other) noexcept {
  if (this != &other) {
    // Clean up existing
    if (_handle) {
      dlclose(_handle);
    }
    init               = std::exchange(other.init, nullptr);
    cleanup            = std::exchange(other.cleanup, nullptr);
    snprintf_chip_name = std::exchange(other.snprintf_chip_name, nullptr);
    get_label          = std::exchange(other.get_label, nullptr);
    get_value          = std::exchange(other.get_value, nullptr);
    get_detected_chips = std::exchange(other.get_detected_chips, nullptr);
    get_features       = std::exchange(other.get_features, nullptr);
    get_subfeature     = std::exchange(other.get_subfeature, nullptr);
    _handle            = std::exchange(other._handle, nullptr);
  }
  return *this;
}

SensorsApi::~SensorsApi() noexcept {
  if (_handle) {
    if (cleanup) {
      cleanup();
    }
    dlclose(_handle);
    _handle = nullptr;
  }
}

auto SensorsApi::Ok() const noexcept -> bool {
  return init != nullptr && cleanup != nullptr && snprintf_chip_name != nullptr && get_label != nullptr &&
         get_value != nullptr && get_detected_chips != nullptr && get_features != nullptr && get_subfeature != nullptr;
}
