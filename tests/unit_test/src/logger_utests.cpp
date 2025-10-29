#include <filesystem>
#include <fstream>
#include <memory>

#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "astl_logger.hpp"
#include "orchestrator/orchestrator.hpp"

/* Scoped test RAII helper class for setting and restoring environment variables
 */
class ModifiedEnvVar {
 public:
  ModifiedEnvVar(const std::string &var_name, const std::string &value)
      : var_name{var_name}, old_value{astl::GetEnvVar(var_name)} {
    astl::SetEnvVar(var_name, value);
  }
  ~ModifiedEnvVar() {
    // restore the environment variable to its original state
    astl::SetEnvVar(var_name, old_value);
  }

  ModifiedEnvVar(const ModifiedEnvVar &)                = delete;
  ModifiedEnvVar &operator=(const ModifiedEnvVar &)     = delete;
  ModifiedEnvVar(ModifiedEnvVar &&) noexcept            = delete;
  ModifiedEnvVar &operator=(ModifiedEnvVar &&) noexcept = delete;
  ModifiedEnvVar()                                      = delete;

 private:
  std::string var_name;
  std::string old_value;
};

/* Test environment variable related utility functions
 */
TEST_CASE("environment variables", "[env_var]") {
  std::string logfile_name = "logger_test1.log";

  // Log file name environment variable
  SECTION("Set logname environment variable") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_NAME", logfile_name);
    REQUIRE(astl::GetEnvVar("ASTL_LOG_NAME") == logfile_name);
  }

  // Log level environement variable
  SECTION("Set loglevel environment variable") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_LEVEL", "trace");
    REQUIRE(astl::GetEnvVar("ASTL_LOG_LEVEL") == "trace");
  }

  // Log console environement variable
  SECTION("Set console environement variable to 0") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_CONSOLE", "0");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_CONSOLE") == false);
  }
  SECTION("Set console environement variable to no") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_CONSOLE", "no");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_CONSOLE") == false);
  }
  SECTION("Set console environement variable to false") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_CONSOLE", "false");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_CONSOLE") == false);
  }

  SECTION("Set console environement variable to off") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_CONSOLE", "off");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_CONSOLE") == false);
  }
  SECTION("Set console environement variable to true") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_CONSOLE", "true");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_CONSOLE") == true);
  }

  // Source location environment variable
  SECTION("Set source location environment variable to 0") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_SOURCE_LOC", "0");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_SOURCE_LOC") == false);
  }
  SECTION("Set source location environment variable to no") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_SOURCE_LOC", "no");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_SOURCE_LOC") == false);
  }
  SECTION("Set source location environment variable to false") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_SOURCE_LOC", "false");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_SOURCE_LOC") == false);
  }
  SECTION("Set source location environment variable to off") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_SOURCE_LOC", "off");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_SOURCE_LOC") == false);
  }
  SECTION("Set source location environment variable to true") {
    ModifiedEnvVar modified_env_var("ASTL_LOG_SOURCE_LOC", "true");
    REQUIRE(astl::IsEnvVarSet("ASTL_LOG_SOURCE_LOC") == true);
  }
}

/* Test random number generator
 */
TEST_CASE("random number", "[rand_num]") {
  uint64_t random_number = 0;
  random_number          = astl::GetRandomNumber();
  REQUIRE(random_number != 0);
}

/* Scoped test RAII helper class for managing log files and automatically
 * deleting them when the test is done. This ensures not lingering test files
 * are left behind if some tests fail
 */
class LoggerScopedTest {
 public:
  LoggerScopedTest(const std::string &log_name, const std::string &log_level, bool log_console, bool source_loc)
      : logfile_name{log_name},
        modified_logname_var{"ASTL_LOG_NAME", log_name},
        modified_loglevel_var{"ASTL_LOG_LEVEL", log_level},
        modified_logconsole_var{"ASTL_LOG_CONSOLE", std::to_string(static_cast<int>(log_console))},
        modified_srcloc_var{"ASTL_LOG_SOURCE_LOC", std::to_string(static_cast<int>(source_loc))},
        logger{std::make_shared<astl::Logger>()} {}

  LoggerScopedTest(const LoggerScopedTest &)                = delete;
  LoggerScopedTest &operator=(const LoggerScopedTest &)     = delete;
  LoggerScopedTest(LoggerScopedTest &&) noexcept            = delete;
  LoggerScopedTest &operator=(LoggerScopedTest &&) noexcept = delete;
  LoggerScopedTest()                                        = delete;

  ~LoggerScopedTest() {
    if (logfile.is_open()) {
      logfile.close();
      std::filesystem::remove(logfile_name);
    }
  }

  // Opening the file must be done after the first log message is logged
  void OpenLog() {
    logfile.open(logfile_name, std::ios::in);
    if (!logfile.is_open()) {
      throw std::runtime_error("Failed to open log file");
    }
  }

  void LogGetLine(std::string &line) {
    std::string my_line;
    if (logfile.eof()) {
      logfile.clear();  // clear EOF flag to allow reading again
      logfile.seekg(logfile.tellg());
    }
    if (getline(logfile, my_line)) {
      line = my_line;
      getline(logfile, my_line);  // trigger eof
    }
  }

  void RewindLog() {
    logfile.clear();
    logfile.seekg(0, std::ios::beg);
  }

  std::shared_ptr<astl::Logger> GetLogger() { return logger; }

 private:
  std::ifstream logfile;
  std::string   logfile_name;

  ModifiedEnvVar                modified_logname_var;
  ModifiedEnvVar                modified_loglevel_var;
  ModifiedEnvVar                modified_logconsole_var;
  ModifiedEnvVar                modified_srcloc_var;
  std::shared_ptr<astl::Logger> logger;
};

/* trace, debug, info, warning, error and critical level should be logged
 * when log level is trace
 */
TEST_CASE("Logger log level trace tests", "[trace]") {
  std::string logfile_name = "trace_test.log";
  std::string log_level    = "trace";
  bool        log_console  = false;
  bool        source_loc   = false;

  // test_format should start with the same value as test_string.
  // format strings have to be constexpr for std::format to do compile-time checks on them
  std::string                test_string = "This is a trace level test log message: ";
  constexpr std::string_view test_format = "This is a trace level test log message: {:d}";

  LoggerScopedTest scoped_test = LoggerScopedTest(logfile_name, log_level, log_console, source_loc);

  std::shared_ptr<astl::Logger> logger = scoped_test.GetLogger();
  REQUIRE(logger != nullptr);

  logger->SetDefaultFormatting();

  // LogTrace should be able to log
  logger->LogTrace(test_format, 0);  // NOLINT
  std::string expected_string = "[:::-trace-:::] " + test_string + "0";
  scoped_test.OpenLog();

  std::string line;
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogDebug should be able to log
  logger->LogDebug(test_format, 1);  // NOLINT
  expected_string = "[:::-debug-:::] " + test_string + "1";

  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogInfo should be able to log"
  logger->LogInfo(test_format, 2);  // NOLINT
  expected_string = "[:::-info-:::] " + test_string + "2";
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogWarning should be able to log
  logger->LogWarning(test_format, 3);  // NOLINT
  expected_string = "[:::-warning-:::] " + test_string + "3";
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogError should be able to log
  logger->LogError(test_format, 4);  // NOLINT
  expected_string = "[:::-error-:::] " + test_string + "4";
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogCritical should be able to log"
  logger->LogCritical(test_format, 5);  // NOLINT
  expected_string = "[:::-critical-:::] " + test_string + "5";
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // Write should be able to log runtime string with no formatting
  logger->ClearFormatting();
  logger->Write(test_string);
  expected_string                 = test_string;
  std::string not_expected_string = "[:::-critical-:::] " + test_string;
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);
  REQUIRE(line.find(not_expected_string) == std::string::npos);
}

/* critical level should be logged when log level is critical.
 * All others should not
 */
TEST_CASE("Logger log level critical tests", "[critcal]") {
  std::string logfile_name = "critical_test.log";
  std::string log_level    = "critical";
  bool        log_console  = false;
  bool        source_loc   = false;

  // format strings must be constexpr for std::format's compile-time check
  std::string                test_string = "This is a critical level test log message: ";
  constexpr std::string_view test_format = "This is a critical level test log message: {:d}";

  LoggerScopedTest scoped_test = LoggerScopedTest(logfile_name, log_level, log_console, source_loc);

  std::shared_ptr<astl::Logger> logger = scoped_test.GetLogger();
  REQUIRE(logger != nullptr);

  logger->SetDefaultFormatting();

  // LogTrace should not be able to log
  logger->LogTrace(test_format, 0);  // NOLINT
  scoped_test.OpenLog();
  std::string line;
  scoped_test.LogGetLine(line);
  REQUIRE(line.empty());

  // LogDebug should not be able to log
  logger->LogDebug(test_format, 1);  // NOLINT
  scoped_test.RewindLog();
  scoped_test.LogGetLine(line);
  REQUIRE(line.empty());

  // LogInfo should not be able to log
  logger->LogInfo(test_format, 2);  // NOLINT
  scoped_test.RewindLog();
  scoped_test.LogGetLine(line);
  REQUIRE(line.empty());

  // LogWarning should not be able to log
  logger->LogWarning(test_format, 3);  // NOLINT
  scoped_test.RewindLog();
  scoped_test.LogGetLine(line);
  REQUIRE(line.empty());

  // LogError should not be able to log
  logger->LogError(test_format, 4);  // NOLINT
  scoped_test.RewindLog();
  scoped_test.LogGetLine(line);
  REQUIRE(line.empty());

  // LogCritical should be able to log
  logger->LogCritical(test_format, 5);  // NOLINT
  std::string expected_string = "[:::-critical-:::] " + test_string + "5";
  scoped_test.RewindLog();
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // Write should be able to log with no formatting
  logger->ClearFormatting();
  logger->Write(test_string);
  expected_string                 = test_string;
  std::string not_expected_string = "[:::-critical-:::] " + test_string + "6";
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);
  REQUIRE(line.find(not_expected_string) == std::string::npos);
}

TEST_CASE("Logger with source location", "[src_loc]") {
  std::string logfile_name = "src_loc_test.log";
  std::string log_level    = "trace";
  bool        log_console  = false;
  bool        source_loc   = true;

  std::string                test_string = "This is a src loc test log message: ";
  constexpr std::string_view test_format = "This is a src loc test log message: {:d}";

  LoggerScopedTest scoped_test = LoggerScopedTest(logfile_name, log_level, log_console, source_loc);

  std::shared_ptr<astl::Logger> logger = scoped_test.GetLogger();
  REQUIRE(logger != nullptr);

  logger->SetDefaultFormatting();

  // LogTrace should be able to log
  std::source_location src_loc = std::source_location::current();
  logger->LogTrace(src_loc, test_format, 0);  // NOLINT
  uint32_t    line_number   = src_loc.line();
  std::string function_name = src_loc.function_name();
  std::string file_name     = src_loc.file_name();
  file_name                 = file_name.substr(file_name.find_last_of('/') + 1);

  std::string expected_string =
      "[:::-trace-:" + file_name + ":" + std::to_string(line_number) + ":" + function_name + "] " + test_string + "0";

  scoped_test.OpenLog();
  std::string line;
  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogDebug should be able to log
  src_loc = std::source_location::current();
  logger->LogDebug(src_loc, test_format, 1);  // NOLINT
  line_number   = src_loc.line();
  function_name = src_loc.function_name();
  file_name     = src_loc.file_name();
  file_name     = file_name.substr(file_name.find_last_of('/') + 1);  // NOLINT
  expected_string =
      "[:::-debug-:" + file_name + ":" + std::to_string(line_number) + ":" + function_name + "] " + test_string + "1";

  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogInfo should be able to log
  src_loc = std::source_location::current();
  logger->LogInfo(src_loc, test_format, 2);  // NOLINT
  line_number   = src_loc.line();
  function_name = src_loc.function_name();
  file_name     = src_loc.file_name();
  file_name     = file_name.substr(file_name.find_last_of('/') + 1);  // NOLINT
  expected_string =
      "[:::-info-:" + file_name + ":" + std::to_string(line_number) + ":" + function_name + "] " + test_string + "2";

  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogWarning should be able to log
  src_loc = std::source_location::current();
  logger->LogWarning(src_loc, test_format, 3);  // NOLINT
  line_number   = src_loc.line();
  function_name = src_loc.function_name();
  file_name     = src_loc.file_name();
  file_name     = file_name.substr(file_name.find_last_of('/') + 1);  // NOLINT
  expected_string =
      "[:::-warning-:" + file_name + ":" + std::to_string(line_number) + ":" + function_name + "] " + test_string + "3";

  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogError should be able to log
  src_loc = std::source_location::current();
  logger->LogError(src_loc, test_format, 4);  // NOLINT
  line_number   = src_loc.line();
  function_name = src_loc.function_name();
  file_name     = src_loc.file_name();
  file_name     = file_name.substr(file_name.find_last_of('/') + 1);  // NOLINT
  expected_string =
      "[:::-error-:" + file_name + ":" + std::to_string(line_number) + ":" + function_name + "] " + test_string + "4";

  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);

  // LogCritical should be able to log
  src_loc = std::source_location::current();
  logger->LogCritical(src_loc, test_format, 5);  // NOLINT
  line_number     = src_loc.line();
  function_name   = src_loc.function_name();
  file_name       = src_loc.file_name();
  file_name       = file_name.substr(file_name.find_last_of('/') + 1);  // NOLINT
  expected_string = "[:::-critical-:" + file_name + ":" + std::to_string(line_number) + ":" + function_name + "] " +
                    test_string + "5";

  scoped_test.LogGetLine(line);
  REQUIRE(line.find(expected_string) != std::string::npos);
}
