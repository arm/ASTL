// main.cpp
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <optional>

#include "argparse/argparse.hpp"  // https://github.com/p-ranav/argparse
#include "nlohmann/json.hpp"      // https://github.com/nlohmann/json

using json = nlohmann::json;

struct AppConfig {
  // ---- defaults ----
  std::string              config_file   = "";
  std::optional<int>       collection_s  = std::nullopt;        // seconds, optional
  std::optional<std::string> process     = std::nullopt;
  std::vector<std::string> metrics       = {};

  // ---- human descriptions (for help + JSON _comment_ fields) ----
  static const char* desc_config_file()   { return "Path to JSON configuration file."; }
  static const char* desc_collection_s()  { return "Collection duration in seconds (optional)."; }
  static const char* desc_process()       { return "Process to launch (string)."; }
  static const char* desc_metrics()       { return "One or more metrics to collect (repeat --metric)."; }

  // merge JSON into this (config > defaults)
  void merge_from_json(const json& j) {
    if (j.contains("config_file"))    config_file  = j.at("config_file").get<std::string>();
    if (j.contains("collection_time")) collection_s = j.at("collection_time").get<int>();
    if (j.contains("process"))         process      = j.at("process").get<std::string>();
    if (j.contains("metrics"))         metrics      = j.at("metrics").get<std::vector<std::string>>();
  }

  // write defaults (or current values) with comments
  json to_json_with_comments(bool use_current_values = true) const {
    json out;

    // Values
    out["config_file"]     = use_current_values ? config_file : "";
    if (use_current_values) {
      if (collection_s) out["collection_time"] = *collection_s; else out["collection_time"] = nullptr;
      if (process)       out["process"]        = *process;      else out["process"] = nullptr;
    } else {
      out["collection_time"] = nullptr;
      out["process"]         = nullptr;
    }
    out["metrics"] = use_current_values ? metrics : std::vector<std::string>{};

    // Comments
    out["_comment_config_file"]    = std::string(desc_config_file())   + " (CLI: -c/--config-file)";
    out["_comment_collection_time"]= std::string(desc_collection_s())  + " (CLI: -t/--collection-time)";
    out["_comment_process"]        = std::string(desc_process())       + " (CLI: -p/--process)";
    out["_comment_metrics"]        = std::string(desc_metrics())       + " (CLI: --metric <val> ...)";
    return out;
  }
};

static std::optional<json> load_json_if_exists(const std::string& path) {
  if (path.empty()) return std::nullopt;
  std::ifstream ifs(path);
  if (!ifs) return std::nullopt;
  json j;
  ifs >> j;
  return j;
}

static void save_json(const std::string& path, const json& j) {
  std::ofstream ofs(path);
  if (!ofs) throw std::runtime_error("Failed to open " + path + " for write");
  ofs << j.dump(2) << "\n";
}

int main(int argc, char** argv) {
  try {
    AppConfig cfg_defaults;
    AppConfig cfg = cfg_defaults;

    argparse::ArgumentParser program("astl-cli", "1.0");

    // ---- subcommands ----
    argparse::ArgumentParser cmd_collect("collect");
    argparse::ArgumentParser cmd_list("list-metrics");
    // Note: help is built-in via --help

    // ---- global options (apply to all) ----
    program.add_argument("-c", "--config-file")
           .help(AppConfig::desc_config_file())
           .default_value(std::string{""});

    program.add_argument("-t", "--collection-time")
           .help(AppConfig::desc_collection_s())
           .scan<'i', int>(); // optional: we'll detect presence with is_used()

    program.add_argument("-p", "--process")
           .help(AppConfig::desc_process());

    program.add_argument("--metric")
           .help(AppConfig::desc_metrics())
           .append(); // allows multiple --metric

    // Save configs
    program.add_argument("--save-default-config")
           .help("Write default configuration (with comments) to the given JSON file and exit.");
    program.add_argument("--save-effective-config")
           .help("Write the merged effective configuration (with comments) to the given JSON file and exit.");

    // Register subcommands
    program.add_subparser(cmd_collect);
    program.add_subparser(cmd_list);

    // Parse CLI
    program.parse_args(argc, argv);

    // If asked: write defaults and exit
    if (program.is_used("--save-default-config")) {
      auto path = program.get<std::string>("--save-default-config");
      save_json(path, cfg_defaults.to_json_with_comments(false /*defaults*/));
      std::cout << "Wrote default config to: " << path << "\n";
      return 0;
    }

    // Load config file if provided
    if (program.is_used("--config-file")) {
      cfg.config_file = program.get<std::string>("--config-file");
    }
    if (auto j = load_json_if_exists(cfg.config_file)) {
      cfg.merge_from_json(*j);
    }

    // Apply CLI overrides (highest precedence)
    if (program.is_used("--collection-time")) {
      cfg.collection_s = program.get<int>("--collection-time");
    }
    if (program.is_used("--process")) {
      cfg.process = program.get<std::string>("--process");
    }
    if (program.is_used("--metric")) {
      cfg.metrics = program.get<std::vector<std::string>>("--metric");
    }

    // Save effective config (after merge) and exit
    if (program.is_used("--save-effective-config")) {
      auto path = program.get<std::string>("--save-effective-config");
      save_json(path, cfg.to_json_with_comments(true /*current*/));
      std::cout << "Wrote effective config to: " << path << "\n";
      return 0;
    }

    // ---- Dispatch subcommands ----
    if (program.is_subcommand_used("collect")) {
      // Stub implementation: show the effective config, then pretend to collect.
      std::cout << "[collect] using configuration:\n"
                << cfg.to_json_with_comments(true).dump(2) << "\n";
      // ... your collection logic here ...
      if (!cfg.collection_s) {
        std::cout << "(no collection time set; run until stopped)\n";
      } else {
        std::cout << "Collecting for " << *cfg.collection_s << " seconds...\n";
      }
      if (cfg.process) {
        std::cout << "Launching process: " << *cfg.process << "\n";
      }
      if (!cfg.metrics.empty()) {
        std::cout << "Metrics: ";
        for (auto& m : cfg.metrics) std::cout << m << " ";
        std::cout << "\n";
      }
      return 0;
    }

    if (program.is_subcommand_used("list-metrics")) {
      // In a real app, you might list discoverable metrics; here we show configured ones.
      std::cout << "[list-metrics]\n";
      if (cfg.metrics.empty()) {
        std::cout << "(no metrics configured)\n";
      } else {
        for (auto& m : cfg.metrics) std::cout << "- " << m << "\n";
      }
      return 0;
    }

    // No subcommand: print help summary
    std::cout << program; // prints top-level help
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}