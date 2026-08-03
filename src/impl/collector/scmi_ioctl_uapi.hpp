// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_IOCTL_UAPI_HPP_
#define SCMI_IOCTL_UAPI_HPP_

#include <cstdint>

#if defined(__linux__)
#  include <sys/ioctl.h>
#endif

// This file mirrors a C kernel UAPI. Keep the C arrays, macro request definitions, and numeric request IDs in the
// same shape as the ABI instead of replacing them with C++ wrappers.
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/** @brief First SCMI telemetry userspace ABI version. */
#define SCMI_TLM_ABI_VERSION_V1 1

/** @brief Latest ABI version understood by this UAPI mirror. */
#define SCMI_TLM_CURRENT_ABI_VERSION SCMI_TLM_ABI_VERSION_V1

/** @brief Number of bytes used by the data-event implementation UUID. */
#define SCMI_TLM_DE_IMPL_UUID_MAX 16

/**
 * @brief ABI and instance information returned by SCMI_TLM_GET_ABI_INFO.
 *
 * This structure mirrors the SCMI telemetry kernel UAPI so ASTL can build
 * without requiring a kernel header that may not yet be available on the build
 * host.
 */
struct scmi_tlm_abi_info {
  /** @brief Size of this structure supplied by userspace and reported by the driver. */
  uint32_t size;

  /** @brief Version of the ioctl ABI implemented by the driver. */
  uint32_t abi_version;

  /** @brief Capabilities of the ioctl ABI itself. */
  uint32_t abi_features;

/** @brief The ABI defines SCMI_TLM_RESET. */
#define SCMI_TLM_ABI_FEAT_RESET (1U << 0)

  /** @brief Data-event implementation UUID in wire byte order. */
  uint8_t de_impl_version[SCMI_TLM_DE_IMPL_UUID_MAX];

  /** @brief Number of data events exposed by the target. */
  uint32_t num_des;

  /** @brief Number of data-event groups exposed by the target. */
  uint32_t num_groups;

  /** @brief Number of supported target-level update intervals. */
  uint32_t num_intervals;

  /** @brief Number of shared memory transport interfaces exposed by the target. */
  uint32_t num_shmtis;

  /** @brief Capabilities reported by this SCMI telemetry instance. */
  uint32_t features;

/** @brief The instance implements reset. */
#define SCMI_TLM_BASE_SUPPORT_RESET (1U << 0)

/** @brief The instance implements platform-triggered single sampling. */
#define SCMI_TLM_BASE_SUPPORT_SINGLE_SAMPLE (1U << 1)

/** @brief The instance supports per-group configuration. */
#define SCMI_TLM_BASE_SUPPORT_GROUP_CONFIG (1U << 2)

/** @brief The instance supports continuous-update notifications. */
#define SCMI_TLM_BASE_SUPPORT_UPDATE_NOTIFICATION (1U << 3)

  /** @brief Reserved for future ABI expansion. */
  uint64_t reserved;
};

/**
 * @brief SCMI telemetry update interval represented as seconds scaled by a base-10 exponent.
 */
struct scmi_tlm_update_interval {
  /** @brief Seconds component of the interval. */
  uint32_t secs;

  /** @brief Base-10 exponent applied to secs. */
  int32_t exp;
};

/**
 * @brief Target or group telemetry configuration used by SCMI_TLM_GET_CFG and SCMI_TLM_SET_CFG.
 */
struct scmi_tlm_config {
  /** @brief Non-zero when telemetry is enabled. */
  uint8_t enable;

  /** @brief Non-zero when timestamps are enabled. */
  uint8_t t_enable;

  /** @brief Configuration flags defined by the kernel UAPI. */
  uint8_t flags;

#define SCMI_TLM_CONFIG_GROUP           (1U << 0)
#define SCMI_TLM_CONFIG_FLAGS           SCMI_TLM_CONFIG_GROUP
#define SCMI_TLM_CONFIG_IS_GROUP(flags) ((flags) & SCMI_TLM_CONFIG_GROUP)

  /** @brief Padding reserved by the kernel UAPI. */
  uint8_t pad;

  /** @brief Group identifier for group-scoped configuration, or zero for target-scoped configuration. */
  uint32_t grp_id;

  /** @brief Active update interval. */
  struct scmi_tlm_update_interval active;
};

/**
 * @brief User-buffer descriptor for querying supported update intervals.
 */
struct scmi_tlm_intervals {
  /** @brief Group identifier for group-scoped interval queries, or zero for target-scoped queries. */
  uint32_t grp_id;

  /** @brief Query flags defined by the kernel UAPI. */
  uint8_t flags;

#define SCMI_TLM_INTERV_GROUP           (1U << 0)
#define SCMI_TLM_INTERV_DISCRETE        (1U << 1)
#define SCMI_TLM_INTERV_FLAGS           (SCMI_TLM_INTERV_GROUP | SCMI_TLM_INTERV_DISCRETE)
#define SCMI_TLM_INTERV_IS_GROUP(flags) ((flags) & SCMI_TLM_INTERV_GROUP)

  /** @brief Padding reserved by the kernel UAPI. */
  uint8_t pad[3];

  /** @brief Number of intervals requested by or returned to userspace. */
  uint32_t num_intervals;

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad2;

  /** @brief Userspace pointer to an array of scmi_tlm_update_interval entries. */
  uint64_t intervals;
};

#define SCMI_TLM_UPDATE_INTVL_SEGMENT_LOW  0
#define SCMI_TLM_UPDATE_INTVL_SEGMENT_HIGH 1
#define SCMI_TLM_UPDATE_INTVL_SEGMENT_STEP 2

/**
 * @brief Per-data-event configuration used by SCMI_TLM_GET_DE_CFG and SCMI_TLM_SET_DE_CFG.
 */
struct scmi_tlm_de_config {
  /** @brief SCMI data event identifier. */
  uint32_t id;

  /** @brief Non-zero when the data event is enabled. */
  uint32_t enable;

  /** @brief Non-zero when timestamps are enabled for the data event. */
  uint32_t t_enable;

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad;
};

/**
 * @brief Per-data-event metadata returned by SCMI_TLM_GET_DE_INFO.
 */
struct scmi_tlm_de_info {
  /** @brief SCMI data event identifier. */
  uint32_t id;

  /** @brief Group containing this data event, if any. */
  uint32_t grp_id;

  /** @brief Size in bytes of the data event value. */
  uint32_t data_sz;

  /** @brief SCMI data event type. */
  uint32_t type;

  /** @brief SCMI unit identifier for the data event value. */
  uint32_t unit;

  /** @brief Base-10 exponent applied to the unit. */
  int32_t unit_exp;

  /** @brief Timestamp tick rate in KHz. */
  uint32_t ts_rate;

  /** @brief Data event instance identifier. */
  uint32_t instance_id;

  /** @brief Composed component instance identifier. */
  uint32_t compo_instance_id;

  /** @brief Composed component type. */
  uint32_t compo_type;

  /** @brief Non-zero when the data event is persistent. */
  uint8_t persistent;

  /** @brief Data event flags defined by the kernel UAPI. */
  uint8_t flags;

#define SCMI_TLM_DEINFO_GROUP            (1U << 0)
#define SCMI_TLM_DEINFO_FLAGS            SCMI_TLM_DEINFO_GROUP
#define SCMI_TLM_DEINFO_HAS_GROUP(flags) ((flags) & SCMI_TLM_DEINFO_GROUP)

  /** @brief Padding reserved by the kernel UAPI. */
  uint8_t pad[2];

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad2;

  /** @brief Optional NUL-terminated data event name. */
  uint8_t name[16];
};

/**
 * @brief User-buffer descriptor for listing data event metadata.
 */
struct scmi_tlm_des_list {
  /** @brief Number of data events requested by or returned to userspace. */
  uint32_t num_des;

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad;

  /** @brief Userspace pointer to an array of scmi_tlm_de_info entries. */
  uint64_t des;
};

/**
 * @brief Single data event sample returned by SCMI_TLM_DE_READ and bulk read ioctls.
 */
struct scmi_tlm_de_sample {
  /** @brief SCMI data event identifier. */
  uint32_t id;

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad;

  /** @brief SCMI hardware timestamp associated with the value. */
  uint64_t tstamp;

  /** @brief Raw data event value. */
  uint64_t val;
};

/**
 * @brief User-buffer descriptor for single, bulk, and batch sample reads.
 */
struct scmi_tlm_data_read {
  /** @brief Group identifier for group-scoped reads. */
  uint32_t grp_id;

  /** @brief Read flags defined by the kernel UAPI. */
  uint8_t flags;

#define SCMI_TLM_READ_GROUP           (1U << 0)
#define SCMI_TLM_READ_FLAGS           SCMI_TLM_READ_GROUP
#define SCMI_TLM_READ_IS_GROUP(flags) ((flags) & SCMI_TLM_READ_GROUP)

  /** @brief Padding reserved by the kernel UAPI. */
  uint8_t pad[3];

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad2;

  /** @brief Number of samples requested by or returned to userspace. */
  uint32_t num_samples;

  /** @brief Userspace pointer to an array of scmi_tlm_de_sample entries. */
  uint64_t samples;
};

/**
 * @brief Data-event group metadata returned by SCMI_TLM_GET_GRP_INFO.
 */
struct scmi_tlm_grp_info {
  /** @brief Group identifier. */
  uint32_t grp_id;

  /** @brief Number of data events in the group. */
  uint32_t num_des;

  /** @brief Number of supported update intervals for the group. */
  uint32_t num_intervals;

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad;
};

/**
 * @brief User-buffer descriptor for listing data-event group metadata.
 */
struct scmi_tlm_grps_list {
  /** @brief Number of groups requested by or returned to userspace. */
  uint32_t num_grps;

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad;

  /** @brief Userspace pointer to an array of scmi_tlm_grp_info entries. */
  uint64_t grps;
};

/**
 * @brief Data-event group composition descriptor returned by SCMI_TLM_GET_GRP_DESC.
 */
struct scmi_tlm_grp_desc {
  /** @brief Group identifier. */
  uint32_t grp_id;

  /** @brief Number of composing data events requested by or returned to userspace. */
  uint32_t num_des;

  /** @brief Userspace pointer to an array of scmi_tlm_de_info entries. */
  uint64_t composing_des;
};

/**
 * @brief Shared memory transport interface descriptor.
 */
struct scmi_tlm_shmti_info {
  /** @brief Shared memory transport interface identifier. */
  uint32_t sid;

  /** @brief File descriptor associated with the shared memory interface. */
  uint32_t fd;

  /** @brief Length in bytes of the shared memory region. */
  uint32_t len;

  /** @brief Offset in bytes of the shared memory region. */
  uint32_t offset;
};

/**
 * @brief User-buffer descriptor for listing shared memory transport interfaces.
 */
struct scmi_tlm_shmtis_list {
  /** @brief Number of interfaces requested by or returned to userspace. */
  uint32_t num_shmtis;

  /** @brief Padding reserved by the kernel UAPI. */
  uint32_t pad;

  /** @brief Userspace pointer to an array of scmi_tlm_shmti_info entries. */
  uint64_t shmtis;
};

#if defined(__linux__)
/** @brief SCMI telemetry ioctl request magic value from the kernel UAPI. */
#  define SCMI_TLM_IOCTL_MAGIC 0xF1

/** @brief Negotiate the ABI and get scmi_tlm_abi_info for a telemetry target. */
#  define SCMI_TLM_GET_ABI_INFO _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x00, struct scmi_tlm_abi_info)

/** @brief Get target or group telemetry configuration. */
#  define SCMI_TLM_GET_CFG _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x01, struct scmi_tlm_config)

/** @brief Set target or group telemetry configuration. */
#  define SCMI_TLM_SET_CFG _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x02, struct scmi_tlm_config)

/** @brief Get supported target or group update intervals. */
#  define SCMI_TLM_GET_INTRVS _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x03, struct scmi_tlm_intervals)

/** @brief Get one data event's enable and timestamp-enable configuration. */
#  define SCMI_TLM_GET_DE_CFG _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x04, struct scmi_tlm_de_config)

/** @brief Set one data event's enable and timestamp-enable configuration. */
#  define SCMI_TLM_SET_DE_CFG _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x05, struct scmi_tlm_de_config)

/** @brief Get one data event's metadata. */
#  define SCMI_TLM_GET_DE_INFO _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x06, struct scmi_tlm_de_info)

/** @brief List data event identifiers exposed by the target. */
#  define SCMI_TLM_GET_DE_LIST _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x07, struct scmi_tlm_des_list)

/** @brief Read one data event sample immediately. */
#  define SCMI_TLM_DE_READ _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x08, struct scmi_tlm_de_sample)

/** @brief Get all data-event configuration state. */
#  define SCMI_TLM_GET_ALL_CFG _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x09, struct scmi_tlm_de_config)

/** @brief Set all data-event configuration state. */
#  define SCMI_TLM_SET_ALL_CFG _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0A, struct scmi_tlm_de_config)

/** @brief List data-event group identifiers exposed by the target. */
#  define SCMI_TLM_GET_GRP_LIST _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0B, struct scmi_tlm_grps_list)

/** @brief Get metadata for one data-event group. */
#  define SCMI_TLM_GET_GRP_INFO _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0C, struct scmi_tlm_grp_info)

/** @brief Get the data event identifiers composing one group. */
#  define SCMI_TLM_GET_GRP_DESC _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0D, struct scmi_tlm_grp_desc)

/** @brief Read a single group sample set. */
#  define SCMI_TLM_SINGLE_READ _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0E, struct scmi_tlm_data_read)

/** @brief Read a bulk group sample set. */
#  define SCMI_TLM_BULK_READ _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0F, struct scmi_tlm_data_read)

/** @brief Read a batch group sample set. */
#  define SCMI_TLM_BATCH_READ _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x10, struct scmi_tlm_data_read)

/** @brief List shared memory transport interfaces exposed by the target. */
#  define SCMI_TLM_GET_SHMTI_LIST _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x11, struct scmi_tlm_shmtis_list)

/** @brief Reset telemetry state when both ABI and instance advertise reset support. */
#  define SCMI_TLM_RESET _IO(SCMI_TLM_IOCTL_MAGIC, 0x12)
#endif

static_assert(sizeof(scmi_tlm_abi_info) == 56, "Unexpected SCMI telemetry ABI info layout");
static_assert(sizeof(scmi_tlm_update_interval) == 8, "Unexpected SCMI telemetry interval layout");
static_assert(sizeof(scmi_tlm_config) == 16, "Unexpected SCMI telemetry configuration layout");
static_assert(sizeof(scmi_tlm_intervals) == 24, "Unexpected SCMI telemetry intervals layout");
static_assert(sizeof(scmi_tlm_de_config) == 16, "Unexpected SCMI telemetry data-event configuration layout");
static_assert(sizeof(scmi_tlm_de_info) == 64, "Unexpected SCMI telemetry data-event information layout");
static_assert(sizeof(scmi_tlm_des_list) == 16, "Unexpected SCMI telemetry data-event list layout");
static_assert(sizeof(scmi_tlm_de_sample) == 24, "Unexpected SCMI telemetry data-event sample layout");
static_assert(sizeof(scmi_tlm_data_read) == 24, "Unexpected SCMI telemetry data-read layout");
static_assert(sizeof(scmi_tlm_grp_info) == 16, "Unexpected SCMI telemetry group information layout");
static_assert(sizeof(scmi_tlm_grps_list) == 16, "Unexpected SCMI telemetry group list layout");
static_assert(sizeof(scmi_tlm_grp_desc) == 16, "Unexpected SCMI telemetry group descriptor layout");
static_assert(sizeof(scmi_tlm_shmti_info) == 16, "Unexpected SCMI telemetry SHMTI information layout");
static_assert(sizeof(scmi_tlm_shmtis_list) == 16, "Unexpected SCMI telemetry SHMTI list layout");

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#endif  // SCMI_IOCTL_UAPI_HPP_
