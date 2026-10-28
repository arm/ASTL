// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <fstream>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "config/config_lookup_loader.hpp"
#include "config/config_source.hpp"
#include "config/embedded_config.hpp"

namespace {
void WriteResource(const std::filesystem::path& root, const std::string& path, const std::string& data) {
  std::filesystem::create_directories((root / path).parent_path());
  std::ofstream(root / path) << data;
}
}  // namespace
TEST_CASE("Config environment reads distinguish missing empty and nonempty values", "[ConfigSource]") {
  EnvVarGuard base(astl::EnvVar::ASTL_CONFIG_DIR);
  REQUIRE(astl::UnsetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR) == ASTL_STATUS_SUCCESS);
  bool present = true;
  CHECK(astl::GetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, &present).empty());
  CHECK_FALSE(present);
  REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, "explicit-config") == ASTL_STATUS_SUCCESS);
  CHECK(astl::GetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, &present) == "explicit-config");
  CHECK(present);
#ifndef _WIN32  // The Windows CRT removes variables when assigned an empty value.
  REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, "") == ASTL_STATUS_SUCCESS);
  CHECK(astl::GetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, &present).empty());
  CHECK(present);
  CHECK_FALSE(astl::config::ConfigSource::Create());
#endif
}

TEST_CASE("Config source selection uses explicit then adjacent then bundled data", "[ConfigSource]") {
  const auto    root = std::filesystem::temp_directory_path() / "astl-config-selection-test";
  TempFileGuard cleanup(root);
  std::filesystem::create_directories(root);
  const auto  binary = root / "astl-binary";
  EnvVarGuard base(astl::EnvVar::ASTL_CONFIG_DIR);
  REQUIRE(astl::UnsetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR) == ASTL_STATUS_SUCCESS);
  SECTION("bundled fallback") {
    auto source = astl::config::ConfigSource::Create(binary);
    REQUIRE(source);
    REQUIRE_FALSE(astl::config::EmbeddedResources().empty());
    CHECK((*source)->Exists("groups/metric_groups.json"));
    auto lookup = astl::config::LoadRepoMetaFragments("scmi/public", source->get());
    REQUIRE(lookup);
    for (const auto& entry : lookup->spec_files_by_uuid) {
      REQUIRE((*source)->Load(entry.spec_file.resolved_specification_file));
    }
  }

  SECTION("adjacent directory replaces bundled data") {
    WriteResource(root / "config", "selected.json", R"({"source":"adjacent"})");
    auto source = astl::config::ConfigSource::Create(binary);
    REQUIRE(source);
    CHECK((*source)->Load("selected.json")->at("source") == "adjacent");
    CHECK_FALSE((*source)->Exists("groups/metric_groups.json"));
  }
  SECTION("explicit directory takes precedence") {
    WriteResource(root / "config", "bad.json", "{");
    WriteResource(root / "explicit", "selected.json", R"({"source":"explicit"})");
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, (root / "explicit").string()) == ASTL_STATUS_SUCCESS);
    auto source = astl::config::ConfigSource::Create(binary);
    REQUIRE(source);
    CHECK((*source)->Load("selected.json")->at("source") == "explicit");
  }
  SECTION("invalid explicit directory does not fall back") {
    WriteResource(root / "config", "valid.json", "{}");
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, (root / "missing").string()) == ASTL_STATUS_SUCCESS);
    auto source = astl::config::ConfigSource::Create(binary);
    REQUIRE_FALSE(source);
    CHECK(source.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
  SECTION("empty selected directory does not fall back") {
    const auto directory = root / "config";
    std::filesystem::create_directories(directory);
    for (const bool explicit_source : {false, true}) {
      if (explicit_source) {
        REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR, directory.string()) == ASTL_STATUS_SUCCESS);
      }
      auto source = astl::config::ConfigSource::Create(binary);
      REQUIRE_FALSE(source);
      CHECK(source.error() == ASTL_STATUS_BAD_CONFIGURATION);
    }
  }
  SECTION("invalid adjacent data does not fall back") {
    WriteResource(root / "config", "bad.json", "{");
    auto source = astl::config::ConfigSource::Create(binary);
    REQUIRE_FALSE(source);
    CHECK(source.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

TEST_CASE("Config references must remain within the selected tree", "[ConfigSource]") {
  const auto    root = std::filesystem::temp_directory_path() / "astl-reference-test";
  TempFileGuard cleanup(root);
  EnvVarGuard   base(astl::EnvVar::ASTL_CONFIG_DIR, root.string());
  for (const auto* field : {"extends", "metrics_file", "specification_file"}) {
    for (const auto& reference :
         {nlohmann::json(42), nlohmann::json((root / "target.json").generic_string()), nlohmann::json("../target.json"),
          nlohmann::json(".."), nlohmann::json("missing.json")}) {
      CAPTURE(field, reference);
      nlohmann::json doc;
      if (std::string_view(field) == "extends") {
        doc[field] = reference;
      } else {
        doc[std::string_view(field) == "metrics_file" ? "scmi_uuid_mapping" : "uuid_mapping"]["id"][field] = reference;
      }
      WriteResource(root, "target.json", "{}");
      WriteResource(root, "lookup.json", doc.dump());
      auto source = astl::config::ConfigSource::Create();
      REQUIRE_FALSE(source);
      CHECK(source.error() == ASTL_STATUS_BAD_CONFIGURATION);
    }
  }
}

TEST_CASE("Config snapshot preserves relative names and contents", "[ConfigSource]") {
  const auto    root = std::filesystem::temp_directory_path() / "astl-snapshot-test";
  TempFileGuard cleanup(root);
  WriteResource(root, "target.json", R"({"value":1})");
  WriteResource(root, "nested/child.json", R"({"extends":"../target.json"})");
  WriteResource(root, "nested/platform_lookup.json",
                R"({"scmi_uuid_mapping":{"id":{"metrics_file":"../target.json"}}})");
  EnvVarGuard base(astl::EnvVar::ASTL_CONFIG_DIR, root.string());
  auto        source = astl::config::ConfigSource::Create();
  REQUIRE(source);
  std::filesystem::remove_all(root);
  auto child = (*source)->Load("nested/child.json");
  REQUIRE(child);
  CHECK(child->at("extends") == "../target.json");
  auto target = (*source)->Load(std::filesystem::path("nested") / child->at("extends").get<std::string>());
  REQUIRE(target);
  CHECK(target->at("value") == 1);
  CHECK((*source)->Files("nested").size() == 2);
  CHECK((*source)->Exists("nested/../target.json"));
  CHECK_FALSE((*source)->Load(root / "target.json"));
}
