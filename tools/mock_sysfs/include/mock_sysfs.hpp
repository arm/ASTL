#ifndef INCLUDE_MOCK_SYSFS_HPP_
#define INCLUDE_MOCK_SYSFS_HPP_

#include <fuse_lowlevel.h>

#include <memory>

#include "fsnode.hpp"

namespace mock_sysfs {

constexpr size_t kMaxContentLen = 1024;

constexpr mode_t kModeReadExecute = 0555;  // Read and execute
constexpr mode_t kModeReadOnly    = 0444;  // Read only
constexpr mode_t kModeReadWrite   = 0666;  // Read and write
constexpr mode_t kModeWriteOnly   = 0222;  // Write only

constexpr int    kDirectoryLinkCount = 2;     /// Directories always have at least two links ('.' and '..').
constexpr int    kFileLinkCount      = 1;     /// Files typically have one link.
constexpr off_t  kDefaultDirSize     = 4096;  /// Default size for a directory in bytes.
constexpr double kAttrTimeoutSec     = 1.0;   /// Timeout for file attribute caching (in seconds).
constexpr double kEntryTimeoutSec    = 1.0;   /// Timeout for directory entry caching (in seconds).

/**
 * @brief Main entry point for a FUSE-based file system.
 *
 * @details This program uses libfuse, a library that enables the implementation
 * of file systems in user space (FUSE). It initializes a FUSE session using the
 * provided command-line arguments, sets up the file system, and enters the FUSE
 * event loop. Simple options for help, version information, and single/multi-threaded
 * operation are supported.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 *
 * @return int Exit status of the program.
 */
extern const struct fuse_lowlevel_ops kFuseLowLevelOps;

struct FuseUserData {
  std::unique_ptr<FileSystemNode> root;
};

}  // namespace mock_sysfs

#endif  // INCLUDE_MOCK_SYSFS_HPP_
