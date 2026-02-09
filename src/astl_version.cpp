#include "astl/astl.h"

constexpr auto              kAstlVersion = astl_version_t{ASTL_VERSION_MAJOR, ASTL_VERSION_MINOR, ASTL_VERSION_MICRO};
constexpr const char* const kAstlStringVersion = ASTL_VERSION_STRING;

auto astlVersionString() noexcept -> const char* { return kAstlStringVersion; }

auto astlVersion() noexcept -> astl_version_t { return kAstlVersion; }
