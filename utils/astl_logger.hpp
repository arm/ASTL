// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_ASTL_LOGGER_HPP_
#define INCLUDE_ASTL_LOGGER_HPP_

#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <source_location>
#include <string>
#include <vector>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE  // This needs to be defined before spdlog.h is included
#define SPDLOG_USE_STD_FORMAT

#include <spdlog/cfg/env.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "astl_magic_enum.hpp"
#include "astl_utils.hpp"
namespace astl {

// add astl_status_code to the to_string overload set
inline std::string to_string(astl_status_code status_code) { return astlStatusString(status_code); }

// Generic to_string for any enum using magic_enum
template <typename T>
requires std::is_enum_v<T>
inline std::string to_string(T enum_value) {
  auto enum_name = magic_enum::enum_name(enum_value);
  if (!enum_name.empty()) {
    return std::string(enum_name);
  }
  // Fallback to numeric value if enum name is not found
  return std::to_string(static_cast<std::underlying_type_t<T>>(enum_value));
}

// stream output function for astl_status_code
inline std::ostream& operator<<(std::ostream& output_stream, astl_status_code status_code) {
  return output_stream << astl::to_string(status_code);
}

// Generic stream output for any enum using magic_enum
template <typename T>
requires std::is_enum_v<T>
inline std::ostream& operator<<(std::ostream& output_stream, T enum_value) {
  return output_stream << astl::to_string(enum_value);
}

}  // namespace astl

/**
 * @brief std::format formatter specialization for astl_status_code
 *
 * This formatter enables astl_status_code values to be used directly in std::format calls.
 * It leverages the existing astl::to_string(astl_status_code) function to convert the
 * status code to its string representation.
 *
 * @example
 * astl_status_code status = ASTL_SUCCESS;
 * auto message = std::format("Operation result: {}", status);
 */
template <>
struct std::formatter<astl_status_code> : std::formatter<std::string> {
  auto format(astl_status_code status_code, auto& ctx) const {
    return std::formatter<std::string>::format(astl::to_string(status_code), ctx);
  }
};

/**
 * @brief Generic std::format formatter for any enum type using magic_enum
 *
 * This template specialization provides automatic formatting support for any enum type
 * when used with std::format. It uses the magic_enum library to convert enum values
 * to their string names at runtime.
 *
 * @tparam T The enum type to format (constrained by std::is_enum_v<T>)
 *
 */
template <typename T>
requires std::is_enum_v<T>
struct std::formatter<T> : std::formatter<std::string> {
  auto format(T enum_value, auto& ctx) const {
    auto enum_name = magic_enum::enum_name(enum_value);
    if (!enum_name.empty()) {
      return std::formatter<std::string>::format(std::string(enum_name), ctx);
    }
    // Fallback to numeric value if enum name is not found
    return std::formatter<std::string>::format(std::to_string(static_cast<std::underlying_type_t<T>>(enum_value)), ctx);
  }
};

namespace astl {

namespace detail {

/* a concept to ensure that types can be logged with << as a fallback for std::format_error */
template <typename T>
concept Streamable = requires(std::ostream& out_stream, T const& val) { out_stream << val; };

/* best effort logger for std::format_error */
template <typename T>
void AppendArg(std::ostringstream& oss, T const& arg_val) {
  if constexpr (Streamable<T>) {
    oss << arg_val;
  } else {
    oss << "<non-streamable>";
  }
}

/* best effort logger for std::format_error */
template <typename... Args>
std::string DumpArgs(Args const&... args) {
  std::ostringstream oss;
  std::string_view   separator;

  ((oss << separator, AppendArg(oss, args), separator = ", "), ...);

  return oss.str();
}

}  // namespace detail

/* @brief Predefined log level supported by the astl logger
 * The set is separate from the spdlog levels to abstract the use of spdlog library
 * All verbose modes automatically activate all lesser verbose modes when used
 */
// NOLINTBEGIN
enum class LogLevel {
  Trace   = 0,  //!< very verbose mode, should be activated for tracing only and should not be visible to users
  Debug   = 1,  //!< verbose mode for debugging, debug messages should not be visible to end users
  Info    = 2,  //!< Info mode is somewhat verbose, messages logged as info generally should not be visible to end users
  Warning = 3,  //!< Warning mode is less verbose and should be used for logging warnings to users.
  Error   = 4,  //!< Error mode is for logging failures. Error messages should be made visible to end users.
  Critical = 5,  //!< Critical mode is catastrophic. This should only be used if the process must terminate
  Off      = 6,  //!< Off mode is to turn off all logging.
  Default  = 7,  //!< Default is the predefined mode at compile time
  None     = 8,  //!< No mode selected
  Unknown
};
// NOLINTEND

/* Default log level for the Logger instance */
static constexpr astl::LogLevel kDefaultLogLevel = astl::LogLevel::Warning;

/* Default logging to console for Logger instance */
static constexpr bool kDefaultLogConsole = false;

/* Default formatting for Logger instance */
static constexpr bool kDefaultFormatting =
    true; /* Set to 'true' for the singleton logger object meant for logging log messages */

static constexpr const char* kDefaultLogName = "astl.log";

/* Default spdlog level when the Logger default is set to LogLevel::Default */
static constexpr spdlog::level::level_enum kDefaultSpdlogLevel = spdlog::level::warn;

/* @brief ASTL Logger class for logging and output file writing
 * When used as a singleton, it is used to log messages to the console, to a file or to both using a predefined format
 * with time, message level and source location when source location is activated.
 * The logging level, whether to log to the console or not and a filename for logging to a file are options that
 * can be specified when the singleton is first constructed.
 * ASTL_LOG_LEVEL, ASTL_LOG_CONSOLE and ASTL_LOG_NAME are environment variables available for overriding compiled
 * in values.
 * ASTL_LOG_SOURCE_LOC environment variable is also available for acticativing source location to be added to the
 * log messages These enviroment variables are meant for development and debugging only
 *
 * For writing output files, a new Logger object can be instantiated separately from the singleton and used for
 * writing formated text to the specified file. It is also affected by the above mentioned environment variables.
 * ASTL_LOG_NAME, in particular, will cause filename conflicts if more than one logger object is instanciated with the
 * environment variable set.
 */
class Logger {
 private:
  /* @brief Map of expected string names set in ASTL_LOG_LEVEL environment variable
   * to LogLevel enum values
   */
  static inline const std::map<std::string, astl::LogLevel> kLevelNameMap = {
      {"TRACE",    astl::LogLevel::Trace   },
      {"DEBUG",    astl::LogLevel::Debug   },
      {"INFO",     astl::LogLevel::Info    },
      {"WARN",     astl::LogLevel::Warning },
      {"ERROR",    astl::LogLevel::Error   },
      {"CRITICAL", astl::LogLevel::Critical},
      {"OFF",      astl::LogLevel::Off     },
      {"DEFAULT",  astl::LogLevel::Default },
  };

  /* @brief Map of ASTL LogLevel enum values to spdlog log level equivalents */
  static inline const std::map<LogLevel, spdlog::level::level_enum> kSpdlogLevelMap = {
      {astl::LogLevel::Trace,    spdlog::level::trace   },
      {astl::LogLevel::Debug,    spdlog::level::debug   },
      {astl::LogLevel::Info,     spdlog::level::info    },
      {astl::LogLevel::Warning,  spdlog::level::warn    },
      {astl::LogLevel::Error,    spdlog::level::err     },
      {astl::LogLevel::Critical, spdlog::level::critical},
      {astl::LogLevel::Off,      spdlog::level::off     },
      {astl::LogLevel::Default,  kDefaultSpdlogLevel    },
  };

  /* brief Get the LogLevel from the ASTL_LOG_LEVEL environment variable
   *
   * @param env_var The environment variable enum value
   *
   * @return the LogLevel corresponding the environment variable value
   */
  static astl::LogLevel GetEnvVarLogLevel(astl::EnvVar env_var = astl::EnvVar::ASTL_LOG_LEVEL) {
    std::string var = GetEnvVar(env_var);
    std::transform(var.begin(), var.end(), var.begin(),
                   [](unsigned char character) { return std::toupper(character); });
    astl::LogLevel level = kLevelNameMap.contains(var) ? kLevelNameMap.at(var) : astl::LogLevel::None;
    return level;
  }

  /* @brief The spdlog level for a given LogLevel
   *
   * @param level the LogLevel level
   *
   * @return The corresponding spdlog level
   */
  static spdlog::level::level_enum GetSpdLogLevel(astl::LogLevel level) {
    spdlog::level::level_enum spdlog_level = spdlog::level::off;

    if (kSpdlogLevelMap.contains(level)) {
      spdlog_level = kSpdlogLevelMap.at(level);
    } else {
      std::cerr << "[Critical] Could not find a log level in map." << '\n';
    }

    return spdlog_level;
  }

 public:
  /* @brief Default constructor
   * The log file name, enabling logging to the console and setting the log level can be done through enviroment
   * variables.
   */
  Logger() : _component_tag("ASTL") {
    const std::string& log_name        = GetEnvVar(astl::EnvVar::ASTL_LOG_NAME);
    bool               console_enabled = IsEnvVarSet(astl::EnvVar::ASTL_LOG_CONSOLE);
    astl::LogLevel     log_level       = GetEnvVarLogLevel(astl::EnvVar::ASTL_LOG_LEVEL);
    /* Normalize None/Unknown to Off for deterministic behavior */
    if (log_level == astl::LogLevel::None || log_level == astl::LogLevel::Unknown) {
      log_level = astl::LogLevel::Off;
    }
    /* Initially, have all formatting cleared. This is useful for using the logger to write to output file instead of
     * logging warnings and errors etc. use SetDefaultFormatting() to set default formatting for instanciated logger
     * objects
     */
    bool default_formatting = false;
    InitializeLogger(log_level, console_enabled, default_formatting, log_name);
  }

  /* @brief Explicit constructor
   *
   * @param level    LogLevel used for the console and file sinks.
   * @param console  boolean to enable or disable the console sink for logging to the console.
   * @param default_formatting boolean to enable default formatting. Set to false to remove all formatting
   *                           This useful for using the logger to write to an output file instead lof logging
   * formatted log messages.
   * @param log_name The log file name for logging to a file
   *
   * Note: The arguments can be overriden with environment variables. The log file name and log level from
   * environment variables have precedence over the passed in arguments. This allows for dynamically overriding
   * for debug purposes. Logging to console is enabled if either it is set in the argumen or set in the
   * environment variable.
   */
  explicit Logger(astl::LogLevel level, bool console, bool default_formatting,
                  const std::string& log_name = std::string(), const std::string& component_tag = std::string())
      : _component_tag(component_tag) {
    const bool use_logging_env_overrides = default_formatting;
    const bool console_enabled = console || (use_logging_env_overrides && IsEnvVarSet(astl::EnvVar::ASTL_LOG_CONSOLE));
    const std::string& var_file_name =
        use_logging_env_overrides ? GetEnvVar(astl::EnvVar::ASTL_LOG_NAME) : std::string();
    astl::LogLevel var_level =
        use_logging_env_overrides ? GetEnvVarLogLevel(astl::EnvVar::ASTL_LOG_LEVEL) : astl::LogLevel::None;
    InitializeLogger(var_level == astl::LogLevel::None ? level : var_level, console_enabled, default_formatting,
                     var_file_name.empty() ? log_name : var_file_name);
  }

  /* @brief Instance of singleton Logger
   * is used for sharing the same logging configuration across all files in a binary.
   *
   * @return static Logger instance
   */
  static Logger& GetInstance() noexcept {
    static Logger logger_instance =
        Logger(kDefaultLogLevel, kDefaultLogConsole, kDefaultFormatting, kDefaultLogName, "ASTL");
    return logger_instance;
  }

  static auto FormatMessage(std::string const& message) -> std::string { return message; }

  static auto FormatMessage(std::string&& message) -> std::string { return std::move(message); }

 private:
  /* @brief main logging function that invokes the spdlog logger log function
   *
   * @param log_level  The message severity
   * @param location   The source location where the log message is logged from
   * @param log_text   The text of the log message
   *
   * Note: the source location is added to the message if the source location environment variable is set
   * static
   */
  template <typename... Args>
  void Log(astl::LogLevel log_level, const std::source_location& location, std::format_string<Args...> log_text,
           Args&&... args) {
    if (!ShouldLog(log_level)) {
      return;
    }
    bool               source_loc_enabled = IsEnvVarSet(astl::EnvVar::ASTL_LOG_SOURCE_LOC);
    spdlog::source_loc spdlog_location;
    if (source_loc_enabled) {
      spdlog_location = {location.file_name(), static_cast<int>(location.line()), location.function_name()};
    }
    try {
      auto formatted_text = FormatMessage(std::format(log_text, std::forward<Args>(args)...));
      EnsureLogger()->log(spdlog_location, GetSpdLogLevel(log_level), std::move(formatted_text));
    } catch (const std::format_error& e) {
      std::ostringstream oss;
      oss << "LOG FORMAT ERROR: " << e.what() << "\n"
          << "  Format string: \"" << log_text.get() << "\"\n"
          << "  Arguments: [" << detail::DumpArgs(std::forward<Args>(args)...) << "]\n";
      EnsureLogger()->log(spdlog_location, GetSpdLogLevel(log_level), FormatMessage(oss.str()));
    }
  }

  /* @brief main logging function that invokes the spdlog logger log function without source location
   *
   * @param log_level  The message severity
   * @param log_text   The text of the log message
   */
  template <typename... Args>
  void Log(astl::LogLevel log_level, std::format_string<Args...> log_text, Args&&... args) {
    if (!ShouldLog(log_level)) {
      return;
    }
    try {
      auto formatted_text = FormatMessage(std::format(log_text, std::forward<Args>(args)...));
      EnsureLogger()->log(GetSpdLogLevel(log_level), std::move(formatted_text));
    } catch (const std::format_error& e) {
      std::ostringstream oss;
      oss << "LOG FORMAT ERROR: " << e.what() << "\n"
          << "  Format string: \"" << log_text.get() << "\"\n"
          << "  Arguments: [" << detail::DumpArgs(std::forward<Args>(args)...) << "]\n";
      EnsureLogger()->log(GetSpdLogLevel(log_level), FormatMessage(oss.str()));
    }
  }

  /* @brief logging function that takes pre-formatted or runtime text, and performs no formatting
   *
   * @param log_level  The message severity
   * @param location   The source location where the log message is logged from
   * @param log_text   The text of the log message
   *
   * Note: the source location is added to the message if the source location environment variable is set
   * static
   */
  void Log(astl::LogLevel log_level, const std::source_location& location, std::string const& log_text) {
    if (!ShouldLog(log_level)) {
      return;
    }
    bool               source_loc_enabled = IsEnvVarSet(astl::EnvVar::ASTL_LOG_SOURCE_LOC);
    spdlog::source_loc spdlog_location;
    if (source_loc_enabled) {
      spdlog_location = {location.file_name(), static_cast<int>(location.line()), location.function_name()};
    }
    EnsureLogger()->log(spdlog_location, GetSpdLogLevel(log_level), FormatMessage(log_text));
  }

  /* @brief logging function that invokes the spdlog logger log function without source location or formatting
   *
   * @param log_level  The message severity
   * @param log_text   The text of the log message
   */
  void Log(astl::LogLevel log_level, std::string const& log_text) {
    if (!ShouldLog(log_level)) {
      return;
    }
    EnsureLogger()->log(GetSpdLogLevel(log_level), FormatMessage(log_text));
  }

 public:
  // TODO(ASTL-73): Use default value std::source_location::current() in log functions
  /* @brief Log function for trace level messages with source location
   *
   * @param log_text formatted text to log
   * @param location the source location. Note: Location is expected to be passed in the caller as it is not possible
   * to set default current location automatically in conjuction with non terminal variadic paramics and default
   * values
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogTrace(const std::source_location& location, std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Trace, location, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for trace level messages without source location
   *
   * @param log_text formatted text to log
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogTrace(std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Trace, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for debug level messages with source location
   *
   * @param log_text formatted text to log
   * @param location the source location. Note: Location is expected to be passed in the caller as it is not possible
   * to set default current location automatically in conjuction with non terminal variadic paramics and default
   * values
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogDebug(const std::source_location& location, std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Debug, location, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for debug level messages without source location
   *
   * @param log_text formatted text to log
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogDebug(const std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Debug, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for info level messages with source location
   *
   * @param log_text formatted text to log
   * @param location the source location. Note: Location is expected to be passed in the caller as it is not possible
   * to set default current location automatically in conjuction with non terminal variadic paramics and default
   * values
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogInfo(const std::source_location& location, std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Info, location, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for info level messages without source location
   *
   * @param log_text formatted text to log
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogInfo(std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Info, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for warning level messages with source location
   *
   * @param log_text formatted text to log
   * @param location the source location. Note: Location is expected to be passed in the caller as it is not possible
   * to set default current location automatically in conjuction with non terminal variadic paramics and default
   * values
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogWarning(const std::source_location& location, std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Warning, location, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for warning level messages without source location
   *
   * @param log_text formatted text to log
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogWarning(std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Warning, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for error level messages with source location
   *
   * @param log_text formatted text to log
   * @param location the source location. Note: Location is expected to be passed in the caller as it is not possible
   * to set default current location automatically in conjuction with non terminal variadic paramics and default
   * values
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogError(const std::source_location& location, std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Error, location, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for error level messages without source location
   *
   * @param log_text formatted text to log
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogError(std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Error, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for critical level messages with source location
   *
   * @param log_text format string to log
   * @param location the source location. Note: Location is expected to be passed in the caller as it is not possible
   * to set default current location automatically in conjuction with non terminal variadic paramics and default
   * values
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogCritical(const std::source_location& location, std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Critical, location, log_text, std::forward<Args>(args)...);
  }

  /* @brief Log function for critical level messages without source location
   *
   * @param log_text formatted text to log
   * @param ... Variable arguments to be formatted according to the format string in log_text
   */
  template <typename... Args>
  void LogCritical(std::format_string<Args...> log_text, Args&&... args) {
    Log(astl::LogLevel::Critical, log_text, std::forward<Args>(args)...);
  }

  /* @brief Write message function meant for logging to an output file without regard to severity level
   *
   * @param log_text pre-formatted text to log
   *
   * Note: This function is meant for loggers with no formatting enabled, or for writing runtime-strings.
   * If default formatting is enabled, then Write() is same as LogCritical()) with no source location or formatting
   */
  template <typename... Args>
  void Write(const std::string& log_text) {
    Log(astl::LogLevel::Critical, log_text);
  }

 private:
  /* @brief Logger initialization function. It sets up the console and/or file sinks and registers the logger with
   * spdlog
   *
   * @param level    LogLevel used for the console and file sinks. Default: LogLevel::Off
   * @param console  boolean to enable or disable the console sink for logging to the console. Default: false
   * @param default_formatting boolean to enable default formatting. Set to false to remove all formatting
   * @param log_name log file name for logging to a file.
   *
   * Note: if the level is not set to off and a log file name is not specified, then console is enabled by default
   * even if console argument is set to false.
   */
  void InitializeLogger(astl::LogLevel level = astl::LogLevel::Off, bool console = false,
                        bool default_formatting = false, const std::string& log_name = std::string()) noexcept {
    _default_formatting_enabled = default_formatting;
    _configured_level           = level;
    _console_enabled            = console;
    _log_name                   = log_name;
    _logger.reset();
  }

  auto ShouldLog(astl::LogLevel log_level) const -> bool {
    const auto configured_level = GetSpdLogLevel(_configured_level);
    return configured_level != spdlog::level::off && GetSpdLogLevel(log_level) >= configured_level;
  }

  auto EnsureLogger() -> std::shared_ptr<spdlog::logger> {
    std::lock_guard<std::mutex> lock(_logger_mutex);
    if (_logger != nullptr) {
      return _logger;
    }

    const bool log_console = _console_enabled || (_configured_level != astl::LogLevel::Off && _log_name.empty());

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::off);

    const auto                    spdlog_level = GetSpdLogLevel(_configured_level);
    std::vector<spdlog::sink_ptr> sinks;
    std::vector<std::string>      spdlog_initialization_errors;

    if (spdlog_level != spdlog::level::off) {
      if (log_console) {
        console_sink->set_level(spdlog_level);
        sinks.push_back(console_sink);
      }

      if (!_log_name.empty()) {
        try {
          const auto log_path = std::filesystem::path(_log_name);
          if (!log_path.parent_path().empty()) {
            std::error_code error_code;
            std::filesystem::create_directories(log_path.parent_path(), error_code);
            if (error_code) {
              throw spdlog::spdlog_ex(std::format("failed to create log directory '{}': {}",
                                                  log_path.parent_path().string(), error_code.message()));
            }
          }

          // Formatted loggers append so multiple logger instances (for example ATX + ASTL) can share one file.
          // Unformatted writer-style loggers truncate to keep generated output files fresh per run.
          auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(_log_name, !_default_formatting_enabled);
          file_sink->set_level(spdlog_level);
          sinks.push_back(file_sink);
        } catch (const spdlog::spdlog_ex& ex) {
          std::string msg =
              "Logger initialization failed: could not create file sink for log file '" + _log_name + "':" + ex.what();
          spdlog_initialization_errors.push_back(std::move(msg));
        }
      }
    }

    if (sinks.empty()) {
      sinks.push_back(console_sink);
    }

    /* spdlog requires all loggers to have unique internal names. Using a random number as part of the name guarantees
     * all instances of Logger will will have uniquely named spdlog loggers */
    uint64_t random_number = astl::GetRandomNumber();
    _logger = std::make_shared<spdlog::logger>(std::format("astl_{:x}", random_number), sinks.begin(), sinks.end());
    _logger->set_level(spdlog_level);  // Set the logger log level
    _logger->flush_on(spdlog_level);   // Set level for flushing, the higher the level the more expensive flushing gets
    _default_formatting_enabled ? SetDefaultFormatting() : ClearFormatting();
    try {
      spdlog::register_logger(_logger);
    } catch (const spdlog::spdlog_ex& ex) {
      const auto* what = ex.what() ? ex.what() : "unknown spdlog error";
      // coverity[uncaught_exception:FALSE] - this is a valid std::format format string.
      spdlog_initialization_errors.push_back(
          std::format("Logger initialization failed: could not register logger with spdlog: {}", what));
    }

    // Log any initialization errors, such as file sink creation failures
    for (const auto& error_msg : spdlog_initialization_errors) {
      std::cerr << "SPDLOG registration error: " << error_msg << "\n";
    }
    return _logger;
  }

 public:
  /* @brief Set default formatting for the logger.
   * The default output format includes time in [HOUR:MINUTE:SECOND:MILLISECON:MICROSECOND] format
   * Message level with coloring in [:::-LEVEL-:::] format
   * if source location logging is enabled, then level is logged as[:::-LEVEL-:SOURCE:LINE:FUNCTION] format
   */
  void SetDefaultFormatting() {
    _default_formatting_enabled = true;
    if (_logger == nullptr) {
      return;
    }
    const std::string pattern_body = "[%H:%M:%S.%e.%f] [:::%^-%l-%$:%s:%#:%!] %v";
    if (_component_tag.empty()) {
      _logger->set_pattern(pattern_body);
      return;
    }
    _logger->set_pattern(std::format("[{}] {}", _component_tag, pattern_body));
  }

  /* @brief Removes all spdlog output formatting and preconfigured output patten
   * after the formatting is cleared, the text string is logged as is. Even return character is not added.
   * This is useful for logging already formatted text to an output file
   */
  void ClearFormatting() {
    _default_formatting_enabled = false;
    if (_logger == nullptr) {
      return;
    }
    auto formatter =
        std::make_unique<spdlog::pattern_formatter>("%v", spdlog::pattern_time_type::local, std::string(""));
    _logger->set_formatter(std::move(formatter));
  }

 private:
  std::shared_ptr<spdlog::logger> _logger{nullptr};  //!< spdlog logger object with all formatting and sinks.
  std::mutex                      _logger_mutex;
  std::string                     _component_tag;
  astl::LogLevel                  _configured_level{astl::LogLevel::Off};
  bool                            _console_enabled{false};
  std::string                     _log_name;
  bool                            _default_formatting_enabled{false};
};

// NOLINTBEGIN
#define ASTL_LOG_TRACE_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogTrace(std::source_location::current(), format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_DEBUG_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogDebug(std::source_location::current(), format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_INFO_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogInfo(std::source_location::current(), format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_WARNING_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogWarning(std::source_location::current(), format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_ERROR_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogError(std::source_location::current(), format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_CRITIAL_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogCritical(std::source_location::current(), format __VA_OPT__(, ) __VA_ARGS__)

#define ASTL_LOG_TRACE_NO_SRC_LOC(format, ...) astl::Logger::GetInstance().LogTrace(format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_DEBUG_NO_SRC_LOC(format, ...) astl::Logger::GetInstance().LogDebug(format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_INFO_NO_SRC_LOC(format, ...)  astl::Logger::GetInstance().LogInfo(format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_WARNING_NO_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogWarning(format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_ERROR_NO_SRC_LOC(format, ...) astl::Logger::GetInstance().LogError(format __VA_OPT__(, ) __VA_ARGS__)
#define ASTL_LOG_CRITIAL_NO_SRC_LOC(format, ...) \
  astl::Logger::GetInstance().LogCritical(format __VA_OPT__(, ) __VA_ARGS__)

#ifdef ASTL_DEBUG
#  define ASTL_LOG_TRACE    ASTL_LOG_TRACE_SRC_LOC
#  define ASTL_LOG_DEBUG    ASTL_LOG_DEBUG_SRC_LOC
#  define ASTL_LOG_INFO     ASTL_LOG_INFO_SRC_LOC
#  define ASTL_LOG_WARNING  ASTL_LOG_WARNING_SRC_LOC
#  define ASTL_LOG_ERROR    ASTL_LOG_ERROR_SRC_LOC
#  define ASTL_LOG_CRITICAL ASTL_LOG_CRITIAL_SRC_LOC
#else /* RELEASE */
#  define ASTL_LOG_TRACE    ASTL_LOG_TRACE_NO_SRC_LOC
#  define ASTL_LOG_DEBUG    ASTL_LOG_DEBUG_NO_SRC_LOC
#  define ASTL_LOG_INFO     ASTL_LOG_INFO_NO_SRC_LOC
#  define ASTL_LOG_WARNING  ASTL_LOG_WARNING_NO_SRC_LOC
#  define ASTL_LOG_ERROR    ASTL_LOG_ERROR_NO_SRC_LOC
#  define ASTL_LOG_CRITICAL ASTL_LOG_CRITIAL_NO_SRC_LOC
#endif
// NOLINTEND
}  // namespace astl

#endif /* INCLUDE_ASTL_LOGGER_HPP_ */
