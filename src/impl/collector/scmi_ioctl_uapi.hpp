// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_IOCTL_UAPI_HPP_
#define SCMI_IOCTL_UAPI_HPP_

#include <cstdint>

#if defined(__linux__)
#  include <sys/ioctl.h>
#endif

// This file mirrors include/uapi/linux/scmi.h from the V8 SCMI telemetry
// patchset. Keep all layouts and ioctl argument types identical to that UAPI.
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#define SCMI_TLM_ABI_VERSION_V1      1
#define SCMI_TLM_CURRENT_ABI_VERSION SCMI_TLM_ABI_VERSION_V1
#define SCMI_TLM_DE_IMPL_UUID_SZ     16
#define SCMI_TLM_SHMTI_ID_INVALID    0xFFFFFFFF

struct scmi_tlm_abi_info {
  uint32_t size;
  uint32_t abi_version;
  uint32_t abi_features;
#define SCMI_TLM_ABI_FEAT_RESET       (1U << 0)
#define SCMI_TLM_ABI_FEAT_EVENT       (1U << 1)
#define SCMI_TLM_ABI_FEAT_UUID_LIST   (1U << 2)
#define SCMI_TLM_ABI_FEAT_BATCH_STATE (1U << 3)
#define SCMI_TLM_ABI_FEAT_BATCHED_CFG (1U << 4)
#define SCMI_TLM_ABI_FEAT_DE_TRACKING (1U << 5)
  uint8_t  primary_de_impl_version[SCMI_TLM_DE_IMPL_UUID_SZ];
  uint32_t num_des;
  uint32_t num_groups;
  uint32_t num_intervals;
  uint32_t num_shmtis;
  uint32_t features;
#define SCMI_TLM_SCMI_SUPPORT_RESET               (1U << 0)
#define SCMI_TLM_SCMI_SUPPORT_SINGLE_SAMPLE       (1U << 1)
#define SCMI_TLM_SCMI_SUPPORT_GROUP_CONFIG        (1U << 2)
#define SCMI_TLM_SCMI_SUPPORT_UPDATE_NOTIFICATION (1U << 3)
  uint64_t reserved;
};

struct scmi_tlm_update_interval {
  uint32_t secs;
  int32_t  exp;
};

struct scmi_tlm_config {
  uint8_t enable;
  uint8_t t_enable;
  uint8_t flags;
#define SCMI_TLM_CONFIG_GROUP       (1U << 0)
#define SCMI_TLM_CONFIG_FLAGS       SCMI_TLM_CONFIG_GROUP
#define SCMI_TLM_CONFIG_IS_GROUP(f) ((f) & SCMI_TLM_CONFIG_GROUP)
  uint8_t                  pad;
  uint32_t                 grp_id;
  scmi_tlm_update_interval active;
  uint64_t                 reserved;
};

struct scmi_tlm_intervals {
  uint32_t grp_id;
  uint8_t  flags;
#define SCMI_TLM_INTERV_GROUP       (1U << 0)
#define SCMI_TLM_INTERV_DISCRETE    (1U << 1)
#define SCMI_TLM_INTERV_FLAGS       (SCMI_TLM_INTERV_GROUP | SCMI_TLM_INTERV_DISCRETE)
#define SCMI_TLM_INTERV_IS_GROUP(f) ((f) & SCMI_TLM_INTERV_GROUP)
  uint8_t  pad[3];
  uint32_t num_intervals;
  uint32_t pad2;
  uint64_t reserved;
#define SCMI_TLM_UPDATE_INTVL_SEGMENT_LOW  0
#define SCMI_TLM_UPDATE_INTVL_SEGMENT_HIGH 1
#define SCMI_TLM_UPDATE_INTVL_SEGMENT_STEP 2
  uint64_t intervals;
};

struct scmi_tlm_de_config {
  uint32_t id;
  uint32_t enable;
  uint32_t t_enable;
  uint32_t sid;
  uint32_t offset;
  uint32_t pad;
  uint8_t  uuid[SCMI_TLM_DE_IMPL_UUID_SZ];
  uint64_t reserved;
};

struct scmi_tlm_de_info {
  uint32_t id;
  uint32_t grp_id;
  uint32_t data_sz;
  uint32_t type;
  uint32_t unit;
  int32_t  unit_exp;
  uint32_t ts_rate;
  uint32_t instance_id;
  uint32_t compo_instance_id;
  uint32_t compo_type;
  uint8_t  persistent;
  uint8_t  flags;
#define SCMI_TLM_DEINFO_GROUP        (1U << 0)
#define SCMI_TLM_DEINFO_FLAGS        SCMI_TLM_DEINFO_GROUP
#define SCMI_TLM_DEINFO_HAS_GROUP(f) ((f) & SCMI_TLM_DEINFO_GROUP)
  uint8_t  pad[2];
  uint32_t pad2;
  uint8_t  name[16];
  uint64_t reserved;
};

struct scmi_tlm_des_list {
  uint32_t num_des;
  uint32_t pad;
  uint64_t des;
};

struct scmi_tlm_de_sample {
  uint32_t id;
  uint32_t pad;
  uint64_t tstamp;
  uint64_t val;
};

struct scmi_tlm_data_read {
  uint32_t grp_id;
  uint8_t  flags;
#define SCMI_TLM_READ_GROUP       (1U << 0)
#define SCMI_TLM_READ_FLAGS       SCMI_TLM_READ_GROUP
#define SCMI_TLM_READ_IS_GROUP(f) ((f) & SCMI_TLM_READ_GROUP)
  uint8_t  pad[3];
  uint32_t pad2;
  uint32_t num_samples;
  uint64_t samples;
};

struct scmi_tlm_batch {
  uint32_t num_items;
  uint32_t item_sz;
  uint64_t reserved;
  uint64_t states;
  uint64_t items;
};

struct scmi_tlm_grp_info {
  uint32_t grp_id;
  uint32_t num_des;
  uint32_t num_intervals;
  uint32_t pad;
  uint64_t reserved;
};

struct scmi_tlm_grps_list {
  uint32_t num_grps;
  uint32_t pad;
  uint64_t grps;
};

struct scmi_tlm_grp_desc {
  uint32_t grp_id;
  uint32_t num_des;
  uint64_t composing_des;
  uint64_t reserved;
};

struct scmi_tlm_shmti_info {
  uint32_t sid;
  uint32_t fd;
  uint32_t len;
  uint32_t offset;
  uint64_t reserved;
};

struct scmi_tlm_shmtis_list {
  uint32_t num_shmtis;
  uint32_t pad;
  uint64_t shmtis;
};

struct scmi_tlm_uuid {
  uint8_t bytes[SCMI_TLM_DE_IMPL_UUID_SZ];
};

struct scmi_tlm_uuid_list {
  uint32_t num_uuids;
  uint32_t pad;
  uint64_t uuids;
};

struct scmi_tlm_event {
#define SCMI_TLM_EVT_GENERATION 0x0U
#define SCMI_TLM_EVT_LAST       SCMI_TLM_EVT_GENERATION
  uint32_t type;
  uint32_t efd;
  uint32_t cookie;
  uint32_t pad;
  uint64_t reserved;
};

#if defined(__linux__)
#  define SCMI_TLM_IOCTL_MAGIC     0xF1
#  define SCMI_TLM_GET_ABI_INFO    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x00, struct scmi_tlm_abi_info)
#  define SCMI_TLM_GET_CFG         _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x01, struct scmi_tlm_config)
#  define SCMI_TLM_SET_CFG         _IOW(SCMI_TLM_IOCTL_MAGIC, 0x02, struct scmi_tlm_config)
#  define SCMI_TLM_GET_INTRVS      _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x03, struct scmi_tlm_intervals)
#  define SCMI_TLM_GET_DE_CFG      _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x04, struct scmi_tlm_batch)
#  define SCMI_TLM_SET_DE_CFG      _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x05, struct scmi_tlm_batch)
#  define SCMI_TLM_GET_DE_INFO     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x06, struct scmi_tlm_de_info)
#  define SCMI_TLM_GET_DE_LIST     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x07, struct scmi_tlm_des_list)
#  define SCMI_TLM_DE_READ         _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x08, struct scmi_tlm_de_sample)
#  define SCMI_TLM_GET_ALL_CFG     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x09, struct scmi_tlm_de_config)
#  define SCMI_TLM_SET_ALL_CFG     _IOW(SCMI_TLM_IOCTL_MAGIC, 0x0A, struct scmi_tlm_de_config)
#  define SCMI_TLM_GET_GRP_LIST    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0B, struct scmi_tlm_grps_list)
#  define SCMI_TLM_GET_GRP_INFO    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0C, struct scmi_tlm_grp_info)
#  define SCMI_TLM_GET_GRP_DESC    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0D, struct scmi_tlm_grp_desc)
#  define SCMI_TLM_SINGLE_READ     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0E, struct scmi_tlm_data_read)
#  define SCMI_TLM_BULK_READ       _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0F, struct scmi_tlm_data_read)
#  define SCMI_TLM_BATCH_READ      _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x10, struct scmi_tlm_batch)
#  define SCMI_TLM_GET_SHMTI_LIST  _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x11, struct scmi_tlm_shmtis_list)
#  define SCMI_TLM_RESET           _IO(SCMI_TLM_IOCTL_MAGIC, 0x12)
#  define SCMI_TLM_GET_UUID_LIST   _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x13, struct scmi_tlm_uuid_list)
#  define SCMI_TLM_EVENT_SUBSCRIBE _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x14, struct scmi_tlm_event)
#endif

static_assert(sizeof(scmi_tlm_abi_info) == 56);
static_assert(sizeof(scmi_tlm_update_interval) == 8);
static_assert(sizeof(scmi_tlm_config) == 24);
static_assert(sizeof(scmi_tlm_intervals) == 32);
static_assert(sizeof(scmi_tlm_de_config) == 48);
static_assert(sizeof(scmi_tlm_de_info) == 72);
static_assert(sizeof(scmi_tlm_des_list) == 16);
static_assert(sizeof(scmi_tlm_de_sample) == 24);
static_assert(sizeof(scmi_tlm_data_read) == 24);
static_assert(sizeof(scmi_tlm_batch) == 32);
static_assert(sizeof(scmi_tlm_grp_info) == 24);
static_assert(sizeof(scmi_tlm_grps_list) == 16);
static_assert(sizeof(scmi_tlm_grp_desc) == 24);
static_assert(sizeof(scmi_tlm_shmti_info) == 24);
static_assert(sizeof(scmi_tlm_shmtis_list) == 16);
static_assert(sizeof(scmi_tlm_uuid) == 16);
static_assert(sizeof(scmi_tlm_uuid_list) == 16);
static_assert(sizeof(scmi_tlm_event) == 24);

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#endif  // SCMI_IOCTL_UAPI_HPP_
