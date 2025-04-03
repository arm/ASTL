#include "astl/astl.h"

constexpr auto              kAstlVersion = astl_version_t{ASTL_VERSION_MAJOR, ASTL_VERSION_MINOR, ASTL_VERSION_MICRO};
constexpr const char* const kAstlStringVersion = ASTL_VERSION_STRING;

const char* astlVersionString() { return kAstlStringVersion; }

astl_version_t astlVersion() { return kAstlVersion; }
