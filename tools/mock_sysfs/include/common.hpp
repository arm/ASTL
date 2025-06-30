#ifndef INCLUDE_COMMON_HPP_
#define INCLUDE_COMMON_HPP_

#include <cstdint>

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

/***********************************************************************************
 **********************               DATA TYPES               *********************
 **********************************************************************************/

/** Generic value types we expect to use.
 */
typedef enum _astl_value_type_t {
  ASTL_VALUE_UINT8   = 0,  //!< Unsigned 8bit integer (char)
  ASTL_VALUE_UINT16  = 1,  //!< Unsigned 16bit integer (short)
  ASTL_VALUE_UINT32  = 2,  //!< Unsigned 32bit integer
  ASTL_VALUE_UINT64  = 3,  //!< Unsigned 64bit integer (long)
  ASTL_VALUE_FLOAT32 = 6,  //!< 32bit float
  ASTL_VALUE_FLOAT64 = 7,  //!< 64bit float (double)
  ASTL_VALUE_BOOL8   = 8,  //!< 8bit boolean
  ASTL_VALUE_STRING  = 9,  //!< String

  ASTL_VALUE_UNKNOWN = 0xFFFFFFFF,  //!< Unknown
} astl_value_type_t;

/** Value container. Processing of an astl_value_t should be based on astl_value_type_t
 * All readings will use this 64bit union to capture any data 64bit in size or less.
 * If there is a reading that is more than 64bit, more than one counter would be used
 * to capture all reading in up to 64bit chunks
 */
typedef union _astl_value_t {
  uint8_t  ui8;   //!< 8bits unsigned integer for UINT8
  uint16_t ui16;  //!< 16bits unsigned integer for UINT16
  uint32_t ui32;  //!< 32bits unsigned integer for UINT32
  uint64_t ui64;  //!< 64bits unsigned integer for UINT64
  float    fp32;  //!< 32bits float for FLOAT32
  double   fp64;  //!< 64bits float for FLAAT64
  bool     b8;    //!< 8bits boolean for BOOL8
  char*    str;   //!< 64bits pointer to string for STRING
} astl_value_t;

}  // namespace mock_sysfs

#endif  // INCLUDE_COMMON_HPP_
