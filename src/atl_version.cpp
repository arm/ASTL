#include "atl/atl_version.h"

constexpr auto kAtlVersion =
    atl_version{ATL_VERSION_MAJOR, ATL_VERSION_MINOR, ATL_VERSION_MICRO};
constexpr const char *const kAtlStringVersion = ATL_VERSION_STRING;

const char *atlVersionString() { return kAtlStringVersion; }

atl_version atlVersion() { return kAtlVersion; }
