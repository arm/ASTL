#ifndef INCLUDE_COMMON_HPP_
#define INCLUDE_COMMON_HPP_

namespace mock_sysfs {

enum class ErrorCode {
  SUCCESS                 = 0,
  CMDLINE_PARSE_ERROR     = 1,
  MOUNTPOINT_MISSING      = 2,
  SESSION_CREATION_FAILED = 3,
  SIGNAL_HANDLER_ERROR    = 4,
  SYSFS_MOUNT_FAILED      = 5,
  SESSION_LOOP_FAILED     = 6,

  UNSUPPORTED_PROTOCOL = 7,
};

}  // namespace mock_sysfs

#endif  // INCLUDE_COMMON_HPP_
