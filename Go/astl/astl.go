// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

package astl

/*
#cgo CFLAGS: -I${SRCDIR}/../../include -I${SRCDIR}/../../build/debug/include
#cgo amd64 LDFLAGS: -L${SRCDIR}/../../build/debug/x86_64/lib -Wl,-rpath,${SRCDIR}/../../build/debug/x86_64/lib
#cgo arm64 LDFLAGS: -L${SRCDIR}/../../build/debug/arm64/lib -Wl,-rpath,${SRCDIR}/../../build/debug/arm64/lib
#cgo 386 LDFLAGS: -L${SRCDIR}/../../build/debug/x86/lib -Wl,-rpath,${SRCDIR}/../../build/debug/x86/lib
#cgo LDFLAGS: -lastl-0d
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "astl/astl.h"

static inline uint8_t astl_go_value_ui8(astl_value_t value) { return value.ui8; }
static inline uint16_t astl_go_value_ui16(astl_value_t value) { return value.ui16; }
static inline uint32_t astl_go_value_ui32(astl_value_t value) { return value.ui32; }
static inline uint64_t astl_go_value_ui64(astl_value_t value) { return value.ui64; }
static inline float astl_go_value_fp32(astl_value_t value) { return value.fp32; }
static inline double astl_go_value_fp64(astl_value_t value) { return value.fp64; }
static inline bool astl_go_value_b8(astl_value_t value) { return value.b8; }
*/
import "C"

import (
	"fmt"
	"unsafe"
)

type Status uint32

const (
	StatusSuccess                         Status = Status(C.ASTL_STATUS_SUCCESS)
	StatusBadArgument                     Status = Status(C.ASTL_STATUS_BAD_ARGUMENT)
	StatusBadConfiguration                Status = Status(C.ASTL_STATUS_BAD_CONFIGURATION)
	StatusInvalidTargetHandle             Status = Status(C.ASTL_STATUS_INVALID_TARGET_HANDLE)
	StatusInvalidCounterHandle            Status = Status(C.ASTL_STATUS_INVALID_COUNTER_HANDLE)
	StatusInvalidMetricHandle             Status = Status(C.ASTL_STATUS_INVALID_METRIC_HANDLE)
	StatusInvalidMetricGroupHandle        Status = Status(C.ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE)
	StatusNotSupported                    Status = Status(C.ASTL_STATUS_NOT_SUPPORTED)
	StatusDeprecatedAPI                   Status = Status(C.ASTL_STATUS_DEPRECATED_API)
	StatusNoTargetFound                   Status = Status(C.ASTL_STATUS_NO_TARGET_FOUND)
	StatusOldStructVersion                Status = Status(C.ASTL_STATUS_OLD_STRUCT_VERSION)
	StatusNewStructVersion                Status = Status(C.ASTL_STATUS_NEW_STRUCT_VERSION)
	StatusNoCountersFound                 Status = Status(C.ASTL_STATUS_NO_COUNTERS_FOUND)
	StatusNoMetricsFound                  Status = Status(C.ASTL_STATUS_NO_METRICS_FOUND)
	StatusNoMetricGroupsFound             Status = Status(C.ASTL_STATUS_NO_METRIC_GROUPS_FOUND)
	StatusBufferTooSmall                  Status = Status(C.ASTL_STATUS_BUFFER_TOO_SMALL)
	StatusMetricReceivedInvalidSample     Status = Status(C.ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE)
	StatusMetricOverflowDetected          Status = Status(C.ASTL_STATUS_METRIC_OVERFLOW_DETECTED)
	StatusInvalidSamplingInterval         Status = Status(C.ASTL_STATUS_INVALID_SAMPLING_INTERVAL)
	StatusSamplingIntervalIgnored         Status = Status(C.ASTL_STATUS_SAMPLING_INTERVAL_IGNORED)
	StatusInvalidCollectionMode           Status = Status(C.ASTL_STATUS_INVALID_COLLECTION_MODE)
	StatusInvalidFlagValue                Status = Status(C.ASTL_STATUS_INVALID_FLAG_VALUE)
	StatusCounterNotSupportedOnTarget     Status = Status(C.ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET)
	StatusMetricNotSupportedOnTarget      Status = Status(C.ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET)
	StatusMetricGroupNotSupportedOnTarget Status = Status(C.ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET)
	StatusCollectionNotConfigured         Status = Status(C.ASTL_STATUS_COLLECTION_NOT_CONFIGURED)
	StatusCollectionNotRunning            Status = Status(C.ASTL_STATUS_COLLECTION_NOT_RUNNING)
	StatusCollectionNotStopped            Status = Status(C.ASTL_STATUS_COLLECTION_NOT_STOPPED)
	StatusCollectionNotPaused             Status = Status(C.ASTL_STATUS_COLLECTION_NOT_PAUSED)
	StatusCollectionAlreadyRunning        Status = Status(C.ASTL_STATUS_COLLECTION_ALREADY_RUNNING)
	StatusCollectionAlreadyStopped        Status = Status(C.ASTL_STATUS_COLLECTION_ALREADY_STOPPED)
	StatusCollectionAlreadyPaused         Status = Status(C.ASTL_STATUS_COLLECTION_ALREADY_PAUSED)
	StatusNoDataCollected                 Status = Status(C.ASTL_STATUS_NO_DATA_COLLECTED)
	StatusBufferLargerThanNeeded          Status = Status(C.ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED)
	StatusUnsupportedCollectorType        Status = Status(C.ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE)
	StatusFileOpenFailed                  Status = Status(C.ASTL_STATUS_FILE_OPEN_FAILED)
	StatusFileError                       Status = Status(C.ASTL_STATUS_FILE_ERROR)
	StatusOutOfMemory                     Status = Status(C.ASTL_STATUS_OUT_OF_MEMORY)
	StatusInvalidValueType                Status = Status(C.ASTL_STATUS_INVALID_VALUE_TYPE)
	StatusInvalidStateTransition          Status = Status(C.ASTL_STATUS_INVALID_STATE_TRANSITION)
	StatusPauseUnsupported                Status = Status(C.ASTL_STATUS_PAUSE_UNSUPPORTED)
	StatusResumeUnsupported               Status = Status(C.ASTL_STATUS_RESUME_UNSUPPORTED)
	StatusInternalError                   Status = Status(C.ASTL_STATUS_INTERNAL_ERROR)
)

func (s Status) String() string {
	return C.GoString(C.astlStatusString(C.astl_status_code(s)))
}

func StatusName(status Status) string {
	return status.String()
}

func LastStatusString() string {
	return C.GoString(C.astlGetLastStatusString())
}

type Error struct {
	Op     string
	Status Status
}

func (e Error) Error() string {
	if e.Op == "" {
		return e.Status.String()
	}
	return fmt.Sprintf("%s: %s", e.Op, e.Status.String())
}

type Units uint32

const (
	UnitsUnknown      Units = ^Units(0)
	UnitsNone         Units = Units(C.ASTL_UNITS_NONE)
	UnitsTicks        Units = Units(C.ASTL_UNITS_TICKS)
	UnitsSeconds      Units = Units(C.ASTL_UNITS_SECONDS)
	UnitsCelsius      Units = Units(C.ASTL_UNITS_CELSIUS)
	UnitsJoules       Units = Units(C.ASTL_UNITS_JOULES)
	UnitsWatts        Units = Units(C.ASTL_UNITS_WATTS)
	UnitsVolts        Units = Units(C.ASTL_UNITS_VOLTS)
	UnitsAmps         Units = Units(C.ASTL_UNITS_AMPS)
	UnitsBytes        Units = Units(C.ASTL_UNITS_BYTES)
	UnitsMBytesPerSec Units = Units(C.ASTL_UNITS_MBYTESPERSEC)
	UnitsMHz          Units = Units(C.ASTL_UNITS_MHZ)
	UnitsRPM          Units = Units(C.ASTL_UNITS_RPM)
	UnitsCount        Units = Units(C.ASTL_UNITS_COUNT)
	UnitsPercent      Units = Units(C.ASTL_UNITS_PERCENT)
)

type ValueType uint32

const (
	ValueUnknown ValueType = ^ValueType(0)
	ValueUInt8   ValueType = ValueType(C.ASTL_VALUE_UINT8)
	ValueUInt16  ValueType = ValueType(C.ASTL_VALUE_UINT16)
	ValueUInt32  ValueType = ValueType(C.ASTL_VALUE_UINT32)
	ValueUInt64  ValueType = ValueType(C.ASTL_VALUE_UINT64)
	ValueFloat32 ValueType = ValueType(C.ASTL_VALUE_FLOAT32)
	ValueFloat64 ValueType = ValueType(C.ASTL_VALUE_FLOAT64)
	ValueBool8   ValueType = ValueType(C.ASTL_VALUE_BOOL8)
)

type CounterType uint32

const (
	CounterTypeUnknown CounterType = ^CounterType(0)
	CounterTypeValue   CounterType = CounterType(C.ASTL_COUNTER_TYPE_VALUE)
	CounterTypeCount   CounterType = CounterType(C.ASTL_COUNTER_TYPE_COUNT)
	CounterTypeEvent   CounterType = CounterType(C.ASTL_COUNTER_TYPE_EVENT)
)

type MetricType uint32

const (
	MetricUnknown        MetricType = ^MetricType(0)
	MetricValue          MetricType = MetricType(C.ASTL_METRIC_VALUE)
	MetricFiniteSetValue MetricType = MetricType(C.ASTL_METRIC_FINITE_SET_VALUE)
	MetricEvent          MetricType = MetricType(C.ASTL_METRIC_EVENT)
	MetricDelta          MetricType = MetricType(C.ASTL_METRIC_DELTA)
	MetricResidency      MetricType = MetricType(C.ASTL_METRIC_RESIDENCY)
	MetricRate           MetricType = MetricType(C.ASTL_METRIC_RATE)
)

type MetricIdentifier uint32

const (
	MetricIdentifierUnknown         MetricIdentifier = ^MetricIdentifier(0)
	MetricIdentifierCount           MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_COUNT)
	MetricIdentifierTemperature     MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_TEMPERATURE)
	MetricIdentifierThermalLimit    MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_THERMAL_LIMIT)
	MetricIdentifierThermalThrottle MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_THERMAL_THROTTLE)
	MetricIdentifierEnergy          MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_ENERGY)
	MetricIdentifierPower           MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_POWER)
	MetricIdentifierPowerLimit      MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_POWER_LIMIT)
	MetricIdentifierPowerThrottle   MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_POWER_THROTTLE)
	MetricIdentifierFrequency       MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_FREQUENCY)
	MetricIdentifierVoltage         MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_VOLTAGE)
	MetricIdentifierCurrent         MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_CURRENT)
	MetricIdentifierBandwidth       MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_BANDWIDTH)
	MetricIdentifierFanSpeed        MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_FAN_SPEED)
	MetricIdentifierHumidity        MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_HUMIDITY)
	MetricIdentifierStatus          MetricIdentifier = MetricIdentifier(C.ASTL_METRIC_IDENTIFIER_STATUS)
)

type CollectionMode uint32

const (
	CollectionModeSampling  CollectionMode = CollectionMode(C.ASTL_COLLECTION_MODE_SAMPLING)
	CollectionModeImmediate CollectionMode = CollectionMode(C.ASTL_COLLECTION_MODE_IMMEDIATE)
	CollectionModeSnapshot  CollectionMode = CollectionMode(C.ASTL_COLLECTION_MODE_SNAPSHOT)
)

type CollectionParameterFlags uint32

const (
	CollectionParameterFlagNone                 CollectionParameterFlags = CollectionParameterFlags(C.ASTL_COLLECTION_PARAMETERS_FLAG_NONE)
	CollectionParameterFlagOptimizeOverhead     CollectionParameterFlags = CollectionParameterFlags(C.ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD)
	CollectionParameterFlagOptimizeMemory       CollectionParameterFlags = CollectionParameterFlags(C.ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY)
	CollectionParameterFlagOptimizeInterference CollectionParameterFlags = CollectionParameterFlags(C.ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_INTERFERENCE)
)

type MetricStatisticsFlags uint32

const (
	MetricStatisticsFlagRegularAverage      MetricStatisticsFlags = MetricStatisticsFlags(C.ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG)
	MetricStatisticsFlagTimeWeightedAverage MetricStatisticsFlags = MetricStatisticsFlags(C.ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG)
)

type SystemInfoFlags uint32

const (
	SystemInfoFlagHost          SystemInfoFlags = SystemInfoFlags(C.ASTL_SYSTEM_INFO_FLAG_HOST)
	SystemInfoFlagLoadedSession SystemInfoFlags = SystemInfoFlags(C.ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION)
)

type Version struct {
	Major uint32
	Minor uint32
	Micro uint32
}

type SystemInfo struct {
	Flags                uint32
	SoCName              string
	VendorID             string
	OSName               string
	KernelName           string
	KernelVersion        string
	KernelRelease        string
	FirmwareVersion      string
	Hostname             string
	Architecture         string
	CPUType              string
	CPUFeatures          string
	CacheInfo            string
	CoreCount            uint32
	NUMANodeCount        uint32
	SocketCount          uint32
	CacheLineSize        uint32
	MemoryTotal          uint64
	DistroName           string
	LibcVersion          string
	BootInfo             string
	HugePagesTotal       int64
	HugePageSizeKB       int64
	TransparentHugePages string
}

type Target struct {
	Handle       uintptr
	ParentHandle uintptr
	Name         string
	Description  string
	ID           string
}

type Counter struct {
	Handle              uintptr
	Name                string
	Description         string
	MinSamplingInterval uint32
	Units               Units
	Formula             string
	ValueType           ValueType
	CounterType         CounterType
}

type Metric struct {
	Handle              uintptr
	Name                string
	Description         string
	MinSamplingInterval uint32
	Units               Units
	ValueType           ValueType
	MetricType          MetricType
	Identifier          MetricIdentifier
}

type MetricGroup struct {
	Handle      uintptr
	Name        string
	Description string
}

type MetricState struct {
	Name        string
	Description string
	Value       any
}

type CollectionParameters struct {
	Flags            CollectionParameterFlags
	SamplingInterval uint32
	Mode             CollectionMode
}

type SampleFilter struct {
	StartTS uint64
	EndTS   uint64
}

type Sample struct {
	Timestamp uint64
	Value     any
}

type MetricStatistics struct {
	Flags   MetricStatisticsFlags
	Min     any
	Max     any
	Average float64
	Count   uint64
}

type DiscreteHistogramBin struct {
	Value any
	Count uint64
}

func VersionString() string {
	return C.GoString(C.astlVersionString())
}

func VersionInfo() Version {
	version := C.astlVersion()
	return Version{
		Major: uint32(version.major),
		Minor: uint32(version.minor),
		Micro: uint32(version.micro),
	}
}

func GetSystemInfo() (SystemInfo, error) {
	return GetSystemInfoWithFlags(0)
}

func GetSystemInfoWithFlags(flags SystemInfoFlags) (SystemInfo, error) {
	info, release, err := allocPlatformProps("GetSystemInfo")
	if err != nil {
		return SystemInfo{}, err
	}
	defer release()

	params := C.astl_get_system_info_params_t{
		size:        C.size_t(C.sizeof_astl_get_system_info_params_t),
		flags:       C.uint32_t(flags),
		system_info: info,
	}

	if err := checkStatus("astlGetSystemInfo", C.astlGetSystemInfo(&params)); err != nil {
		return SystemInfo{}, err
	}

	return SystemInfo{
		Flags:                uint32(info.flags),
		SoCName:              goString(info.soc_name),
		VendorID:             goString(info.vendor_id),
		OSName:               goString(info.os_name),
		KernelName:           goString(info.kernel_name),
		KernelVersion:        goString(info.kernel_version),
		KernelRelease:        goString(info.kernel_release),
		FirmwareVersion:      goString(info.firmware_version),
		Hostname:             goString(info.hostname),
		Architecture:         goString(info.architecture),
		CPUType:              goString(info.cpu_type),
		CPUFeatures:          goString(info.cpu_features),
		CacheInfo:            goString(info.cache_info),
		CoreCount:            uint32(info.core_count),
		NUMANodeCount:        uint32(info.numa_node_count),
		SocketCount:          uint32(info.socket_count),
		CacheLineSizeBytes:   uint32(info.cache_line_size_bytes),
		MemoryTotalBytes:     uint64(info.memory_total_bytes),
		LibcVersion:          goString(info.libc_version),
		BootInfo:             goString(info.boot_info),
		HugePagesTotal:       int64(info.huge_pages_total),
		HugePageSizeKB:       int64(info.huge_page_size_kb),
		TransparentHugePages: goString(info.transparent_huge_pages),
	}, nil
}

func GetTargets() ([]Target, error) {
	return fetchList("GetTargets", listQuery[C.astl_target_props_t, Target]{
		countOp: "astlGetTargetCount",
		fetchOp: "astlGetTargets",
		query: func(count *C.uint32_t) C.astl_status_code {
			params := C.astl_get_target_count_params_t{
				size:         C.size_t(C.sizeof_astl_get_target_count_params_t),
				flags:        0,
				target_count: count,
			}
			return C.astlGetTargetCount(&params)
		},
		elemSize: C.size_t(C.sizeof_astl_target_props_t),
		init: func(items []C.astl_target_props_t) {
			items[0].size = C.size_t(C.sizeof_astl_target_props_t)
		},
		fetch: func(items []C.astl_target_props_t, count *C.uint32_t) C.astl_status_code {
			params := C.astl_get_targets_params_t{
				size:         C.size_t(C.sizeof_astl_get_targets_params_t),
				flags:        0,
				targets:      &items[0],
				target_count: count,
			}
			return C.astlGetTargets(&params)
		},
		build: buildTargets,
	})
}

func GetCountersOnTarget(target Target) ([]Counter, error) {
	return fetchTargetList(target, counterListQuery())
}

func GetMetricsOnTarget(target Target) ([]Metric, error) {
	return fetchTargetList(target, metricListQuery())
}

func GetMetricGroups() ([]MetricGroup, error) {
	return fetchList("GetMetricGroups", metricGroupListQuery())
}

func GetMetricGroupsOnTarget(target Target) ([]MetricGroup, error) {
	return fetchTargetList(target, metricGroupListQueryOnTarget())
}

func GetMetricGroupMetricCount(group MetricGroup) (uint32, error) {
	group, err := resolveGlobalMetricGroup(group)
	if err != nil {
		return 0, err
	}
	return queryCount("GetMetricGroupMetricCount", "astlGetMetricGroupMetricCount", nil,
		func(count *C.uint32_t) C.astl_status_code {
			params := C.astl_get_metric_group_metric_count_params_t{
				size:                C.size_t(C.sizeof_astl_get_metric_group_metric_count_params_t),
				flags:               0,
				metric_group_handle: cMetricGroupHandle(group),
				metric_count:        count,
			}
			return C.astlGetMetricGroupMetricCount(&params)
		})
}

func GetMetricGroupMetrics(group MetricGroup) ([]Metric, error) {
	group, err := resolveGlobalMetricGroup(group)
	if err != nil {
		return nil, err
	}
	count, err := queryCount("GetMetricGroupMetricCount", "astlGetMetricGroupMetricCount", nil,
		func(count *C.uint32_t) C.astl_status_code {
			params := C.astl_get_metric_group_metric_count_params_t{
				size:                C.size_t(C.sizeof_astl_get_metric_group_metric_count_params_t),
				flags:               0,
				metric_group_handle: cMetricGroupHandle(group),
				metric_count:        count,
			}
			return C.astlGetMetricGroupMetricCount(&params)
		})
	if err != nil {
		return nil, err
	}
	return fetchMetricGroupMetrics("GetMetricGroupMetrics", "astlGetMetricGroupMetrics", count,
		func(count *C.uint32_t, metrics *C.astl_metric_props_t) C.astl_status_code {
			params := C.astl_get_metric_group_metrics_params_t{
				size:                C.size_t(C.sizeof_astl_get_metric_group_metrics_params_t),
				flags:               0,
				metric_group_handle: cMetricGroupHandle(group),
				metrics:             metrics,
				metric_count:        count,
			}
			return C.astlGetMetricGroupMetrics(&params)
		})
}

func GetMetricGroupMetricCountOnTarget(target Target, group MetricGroup) (uint32, error) {
	group, err := resolveMetricGroupOnTarget(target, group)
	if err != nil {
		return 0, err
	}
	return queryCount("GetMetricGroupMetricCountOnTarget", "astlGetMetricGroupMetricCountOnTarget", nil,
		func(count *C.uint32_t) C.astl_status_code {
			params := C.astl_get_metric_group_metric_count_on_target_params_t{
				size:                C.size_t(C.sizeof_astl_get_metric_group_metric_count_on_target_params_t),
				flags:               0,
				target_handle:       cTargetHandle(target),
				metric_group_handle: cMetricGroupHandle(group),
				metric_count:        count,
			}
			return C.astlGetMetricGroupMetricCountOnTarget(&params)
		})
}

func GetMetricGroupMetricsOnTarget(target Target, group MetricGroup) ([]Metric, error) {
	group, err := resolveMetricGroupOnTarget(target, group)
	if err != nil {
		return nil, err
	}
	count, err := queryCount("GetMetricGroupMetricCountOnTarget", "astlGetMetricGroupMetricCountOnTarget", nil,
		func(count *C.uint32_t) C.astl_status_code {
			params := C.astl_get_metric_group_metric_count_on_target_params_t{
				size:                C.size_t(C.sizeof_astl_get_metric_group_metric_count_on_target_params_t),
				flags:               0,
				target_handle:       cTargetHandle(target),
				metric_group_handle: cMetricGroupHandle(group),
				metric_count:        count,
			}
			return C.astlGetMetricGroupMetricCountOnTarget(&params)
		})
	if err != nil {
		return nil, err
	}
	return fetchMetricGroupMetrics("GetMetricGroupMetricsOnTarget", "astlGetMetricGroupMetricsOnTarget", count,
		func(count *C.uint32_t, metrics *C.astl_metric_props_t) C.astl_status_code {
			params := C.astl_get_metric_group_metrics_on_target_params_t{
				size:                C.size_t(C.sizeof_astl_get_metric_group_metrics_on_target_params_t),
				flags:               0,
				target_handle:       cTargetHandle(target),
				metric_group_handle: cMetricGroupHandle(group),
				metrics:             metrics,
				metric_count:        count,
			}
			return C.astlGetMetricGroupMetricsOnTarget(&params)
		})
}

func ConfigureCountersOnTarget(target Target, params CollectionParameters, counters []Counter) error {
	return configureOnTarget(target, params, counters, counterTargetCollectionConfig())
}

func ConfigureCounters(params CollectionParameters, counters []Counter) error {
	return configureCollection(params, counters, counterCollectionConfig())
}

func ConfigureMetricsOnTarget(target Target, params CollectionParameters, metrics []Metric) error {
	return configureOnTarget(target, params, metrics, metricTargetCollectionConfig())
}

func ConfigureMetrics(params CollectionParameters, metrics []Metric) error {
	return configureCollection(params, metrics, metricCollectionConfig())
}

func ConfigureMetricGroupsOnTarget(target Target, params CollectionParameters, groups []MetricGroup) error {
	return configureOnTarget(target, params, groups, metricGroupTargetCollectionConfig())
}

func ConfigureMetricGroups(params CollectionParameters, groups []MetricGroup) error {
	return configureCollection(params, groups, metricGroupCollectionConfig())
}

func ReadImmediateOnTarget(target Target) error {
	request := C.astl_read_immediate_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_read_immediate_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
	}
	return checkStatus("astlReadImmediateOnTarget", C.astlReadImmediateOnTarget(&request))
}

func ReadImmediate() error {
	request := C.astl_read_immediate_params_t{
		size:  C.size_t(C.sizeof_astl_read_immediate_params_t),
		flags: 0,
	}
	return checkStatus("astlReadImmediate", C.astlReadImmediate(&request))
}

func StartCollectionOnTarget(target Target) error {
	request := C.astl_start_collection_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_start_collection_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
	}
	return checkStatus("astlStartCollectionOnTarget", C.astlStartCollectionOnTarget(&request))
}

func StartCollection() error {
	request := C.astl_start_collection_params_t{
		size:  C.size_t(C.sizeof_astl_start_collection_params_t),
		flags: 0,
	}
	return checkStatus("astlStartCollection", C.astlStartCollection(&request))
}

func StartCollectionOnTargetPaused(target Target) error {
	request := C.astl_start_collection_on_target_paused_params_t{
		size:          C.size_t(C.sizeof_astl_start_collection_on_target_paused_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
	}
	return checkStatus("astlStartCollectionOnTargetPaused", C.astlStartCollectionOnTargetPaused(&request))
}

func StartCollectionPaused() error {
	request := C.astl_start_collection_paused_params_t{
		size:  C.size_t(C.sizeof_astl_start_collection_paused_params_t),
		flags: 0,
	}
	return checkStatus("astlStartCollectionPaused", C.astlStartCollectionPaused(&request))
}

func PauseCollectionOnTarget(target Target) error {
	request := C.astl_pause_collection_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_pause_collection_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
	}
	return checkStatus("astlPauseCollectionOnTarget", C.astlPauseCollectionOnTarget(&request))
}

func PauseCollection() error {
	request := C.astl_pause_collection_params_t{
		size:  C.size_t(C.sizeof_astl_pause_collection_params_t),
		flags: 0,
	}
	return checkStatus("astlPauseCollection", C.astlPauseCollection(&request))
}

func ResumeCollectionOnTarget(target Target) error {
	request := C.astl_resume_collection_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_resume_collection_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
	}
	return checkStatus("astlResumeCollectionOnTarget", C.astlResumeCollectionOnTarget(&request))
}

func ResumeCollection() error {
	request := C.astl_resume_collection_params_t{
		size:  C.size_t(C.sizeof_astl_resume_collection_params_t),
		flags: 0,
	}
	return checkStatus("astlResumeCollection", C.astlResumeCollection(&request))
}

func StopCollectionOnTarget(target Target) error {
	request := C.astl_stop_collection_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_stop_collection_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
	}
	return checkStatus("astlStopCollectionOnTarget", C.astlStopCollectionOnTarget(&request))
}

func StopCollection() error {
	request := C.astl_stop_collection_params_t{
		size:  C.size_t(C.sizeof_astl_stop_collection_params_t),
		flags: 0,
	}
	return checkStatus("astlStopCollection", C.astlStopCollection(&request))
}

func SaveCollection(path string) error {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	request := C.astl_save_params_t{
		size:             C.size_t(C.sizeof_astl_save_params_t),
		flags:            0,
		output_file_path: cPath,
	}
	return checkStatus("astlSaveCollection", C.astlSaveCollection(&request))
}

func LoadCollection(path string, chunkSizeBytes uint64) error {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	request := C.astl_load_params_t{
		size:             C.size_t(C.sizeof_astl_load_params_t),
		flags:            0,
		input_file_path:  cPath,
		chunk_size_bytes: C.size_t(chunkSizeBytes),
	}
	return checkStatus("astlLoadCollection", C.astlLoadCollection(&request))
}

// CropWindow defines an inclusive time range of samples to retain for CropSamples,
// CropSamplesOnTarget, and CropMetricSamplesOnTarget. Samples whose timestamp falls within
// [StartTS, EndTS] are kept; all others are discarded.
// Timestamps are CLOCK_MONOTONIC_RAW nanoseconds on Linux.
// A zero StartTS means no lower bound on the retained range; a zero EndTS means no upper bound.
type CropWindow struct {
	StartTS uint64
	EndTS   uint64
}

// CropSamplesOnTarget permanently retains samples for the specified target whose timestamps
// fall within the supplied time window, discarding all others. Collection must be stopped before calling this function.
// The operation is irreversible; reload the session to recover the original data.
func CropSamplesOnTarget(target Target, window CropWindow) error {
	cWindow := C.astl_crop_window_t{
		size:     C.size_t(C.sizeof_astl_crop_window_t),
		flags:    0,
		start_ts: C.uint64_t(window.StartTS),
		end_ts:   C.uint64_t(window.EndTS),
	}
	request := C.astl_crop_samples_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_crop_samples_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		windows:       &cWindow,
		window_count:  1,
	}
	return checkStatus("astlCropSamplesOnTarget", C.astlCropSamplesOnTarget(&request))
}

// CropMetricSamplesOnTarget permanently retains samples for the specified metric on the specified
// target whose timestamps fall within the supplied time window, discarding all others. Collection must be stopped
// before calling this function. The operation is irreversible; reload the session to recover the original data.
func CropMetricSamplesOnTarget(target Target, metric Metric, window CropWindow) error {
	cWindow := C.astl_crop_window_t{
		size:     C.size_t(C.sizeof_astl_crop_window_t),
		flags:    0,
		start_ts: C.uint64_t(window.StartTS),
		end_ts:   C.uint64_t(window.EndTS),
	}
	request := C.astl_crop_metric_samples_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_crop_metric_samples_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		windows:       &cWindow,
		window_count:  1,
	}
	return checkStatus("astlCropMetricSamplesOnTarget", C.astlCropMetricSamplesOnTarget(&request))
}

// CropSamples permanently retains samples across all targets whose timestamps fall within
// the supplied time window, discarding all others. Collection must be stopped before calling this function.
// The operation is irreversible; reload the session to recover the original data.
func CropSamples(window CropWindow) error {
	cWindow := C.astl_crop_window_t{
		size:     C.size_t(C.sizeof_astl_crop_window_t),
		flags:    0,
		start_ts: C.uint64_t(window.StartTS),
		end_ts:   C.uint64_t(window.EndTS),
	}
	request := C.astl_crop_samples_params_t{
		size:         C.size_t(C.sizeof_astl_crop_samples_params_t),
		flags:        0,
		windows:      &cWindow,
		window_count: 1,
	}
	return checkStatus("astlCropSamples", C.astlCropSamples(&request))
}

func GetCounterSamples(target Target, counter Counter, filter SampleFilter) ([]Sample, error) {
	count, releaseCount, err := allocUint32("GetCounterSamples")
	if err != nil {
		return nil, err
	}
	defer releaseCount()

	countParams := C.astl_get_counter_sample_count_on_target_params_t{
		size:           C.size_t(C.sizeof_astl_get_counter_sample_count_on_target_params_t),
		flags:          0,
		target_handle:  cTargetHandle(target),
		counter_handle: cCounterHandle(counter),
		sample_count:   count,
		start_ts:       C.uint64_t(filter.StartTS),
		end_ts:         C.uint64_t(filter.EndTS),
	}

	if err := checkStatus("astlGetCounterSampleCountOnTarget", C.astlGetCounterSampleCountOnTarget(&countParams)); err != nil {
		return nil, err
	}
	if *count == 0 {
		return []Sample{}, nil
	}

	ptr := (*C.astl_sample_t)(C.calloc(C.size_t(*count), C.size_t(C.sizeof_astl_sample_t)))
	if ptr == nil {
		return nil, Error{Op: "GetCounterSamples", Status: StatusOutOfMemory}
	}
	defer C.free(unsafe.Pointer(ptr))

	request := C.astl_get_counter_samples_on_target_params_t{
		size:           C.size_t(C.sizeof_astl_get_counter_samples_on_target_params_t),
		flags:          0,
		target_handle:  cTargetHandle(target),
		counter_handle: cCounterHandle(counter),
		samples:        ptr,
		sample_count:   count,
		start_ts:       C.uint64_t(filter.StartTS),
		end_ts:         C.uint64_t(filter.EndTS),
	}

	if err := checkStatus("astlGetCounterSamplesOnTarget", C.astlGetCounterSamplesOnTarget(&request)); err != nil {
		return nil, err
	}

	samplesBuf := unsafe.Slice(ptr, int(*count))
	samples := make([]Sample, 0, int(*count))
	for _, item := range samplesBuf {
		samples = append(samples, Sample{
			Timestamp: uint64(item.timestamp),
			Value:     decodeValue(item.value, counter.ValueType),
		})
	}
	return samples, nil
}

func GetMetricSamples(target Target, metric Metric, filter SampleFilter) ([]Sample, error) {
	count, releaseCount, err := allocUint32("GetMetricSamples")
	if err != nil {
		return nil, err
	}
	defer releaseCount()

	countParams := C.astl_get_metric_sample_count_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_sample_count_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		sample_count:  count,
		start_ts:      C.uint64_t(filter.StartTS),
		end_ts:        C.uint64_t(filter.EndTS),
	}

	if err := checkStatus("astlGetMetricSampleCountOnTarget", C.astlGetMetricSampleCountOnTarget(&countParams)); err != nil {
		return nil, err
	}
	if *count == 0 {
		return []Sample{}, nil
	}

	ptr := (*C.astl_sample_t)(C.calloc(C.size_t(*count), C.size_t(C.sizeof_astl_sample_t)))
	if ptr == nil {
		return nil, Error{Op: "GetMetricSamples", Status: StatusOutOfMemory}
	}
	defer C.free(unsafe.Pointer(ptr))

	request := C.astl_get_metric_samples_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_samples_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		samples:       ptr,
		sample_count:  count,
		start_ts:      C.uint64_t(filter.StartTS),
		end_ts:        C.uint64_t(filter.EndTS),
	}

	if err := checkStatus("astlGetMetricSamplesOnTarget", C.astlGetMetricSamplesOnTarget(&request)); err != nil {
		return nil, err
	}

	samplesBuf := unsafe.Slice(ptr, int(*count))
	samples := make([]Sample, 0, int(*count))
	for _, item := range samplesBuf {
		samples = append(samples, Sample{
			Timestamp: uint64(item.timestamp),
			Value:     decodeValue(item.value, metric.ValueType),
		})
	}
	return samples, nil
}

func GetMetricStatisticsOnTarget(target Target, metric Metric, filter SampleFilter, flags MetricStatisticsFlags) (MetricStatistics, error) {
	summary, releaseSummary, err := allocMetricStatistics(flags)
	if err != nil {
		return MetricStatistics{}, err
	}
	defer releaseSummary()

	request := C.astl_get_metric_statistics_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_statistics_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		summary:       summary,
		start_ts:      C.uint64_t(filter.StartTS),
		end_ts:        C.uint64_t(filter.EndTS),
	}

	if err := checkStatus("astlGetMetricStatisticsOnTarget", C.astlGetMetricStatisticsOnTarget(&request)); err != nil {
		return MetricStatistics{}, err
	}

	result := MetricStatistics{
		Flags:   MetricStatisticsFlags(summary.flags),
		Average: float64(C.astl_go_value_fp64(summary.avg)),
		Count:   uint64(summary.count),
	}
	if result.Count > 0 {
		result.Min = decodeValue(summary.min, metric.ValueType)
		result.Max = decodeValue(summary.max, metric.ValueType)
	}
	return result, nil
}

func GetMetricDiscreteHistogramOnTarget(target Target, metric Metric, filter SampleFilter) ([]DiscreteHistogramBin, error) {
	count, releaseCount, err := allocUint32("GetMetricDiscreteHistogramOnTarget")
	if err != nil {
		return nil, err
	}
	defer releaseCount()

	countParams := C.astl_get_metric_discrete_histogram_bin_count_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_discrete_histogram_bin_count_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		bin_count:     count,
		start_ts:      C.uint64_t(filter.StartTS),
		end_ts:        C.uint64_t(filter.EndTS),
	}

	if err := checkStatus("astlGetMetricDiscreteHistogramBinCountOnTarget", C.astlGetMetricDiscreteHistogramBinCountOnTarget(&countParams)); err != nil {
		return nil, err
	}
	if *count == 0 {
		return []DiscreteHistogramBin{}, nil
	}

	ptr := (*C.astl_discrete_histogram_bin_t)(C.calloc(C.size_t(*count), C.size_t(C.sizeof_astl_discrete_histogram_bin_t)))
	if ptr == nil {
		return nil, Error{Op: "GetMetricDiscreteHistogramOnTarget", Status: StatusOutOfMemory}
	}
	defer C.free(unsafe.Pointer(ptr))

	binsBuf := unsafe.Slice(ptr, int(*count))
	binsBuf[0].size = C.size_t(C.sizeof_astl_discrete_histogram_bin_t)

	request := C.astl_get_metric_discrete_histogram_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_discrete_histogram_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		bins:          ptr,
		bin_count:     count,
		start_ts:      C.uint64_t(filter.StartTS),
		end_ts:        C.uint64_t(filter.EndTS),
	}

	if err := checkStatus("astlGetMetricDiscreteHistogramOnTarget", C.astlGetMetricDiscreteHistogramOnTarget(&request)); err != nil {
		return nil, err
	}

	histogram := make([]DiscreteHistogramBin, 0, int(*count))
	for _, item := range binsBuf[:int(*count)] {
		histogram = append(histogram, DiscreteHistogramBin{
			Value: decodeValue(item.value, metric.ValueType),
			Count: uint64(item.count),
		})
	}
	return histogram, nil
}

func GetMetricStatesOnTarget(target Target, metric Metric) ([]MetricState, error) {
	count, releaseCount, err := allocUint32("GetMetricStatesOnTarget")
	if err != nil {
		return nil, err
	}
	defer releaseCount()

	countParams := C.astl_get_metric_state_count_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_state_count_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		state_count:   count,
	}

	if err := checkStatus("astlGetMetricStateCountOnTarget", C.astlGetMetricStateCountOnTarget(&countParams)); err != nil {
		return nil, err
	}
	if *count == 0 {
		return []MetricState{}, nil
	}

	ptr := (*C.astl_state_props_t)(C.calloc(C.size_t(*count), C.size_t(C.sizeof_astl_state_props_t)))
	if ptr == nil {
		return nil, Error{Op: "GetMetricStatesOnTarget", Status: StatusOutOfMemory}
	}
	defer C.free(unsafe.Pointer(ptr))

	statesBuf := unsafe.Slice(ptr, int(*count))
	statesBuf[0].size = C.size_t(C.sizeof_astl_state_props_t)

	request := C.astl_get_metric_states_on_target_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_states_on_target_params_t),
		flags:         0,
		target_handle: cTargetHandle(target),
		metric_handle: cMetricHandle(metric),
		states:        ptr,
		state_count:   count,
	}

	if err := checkStatus("astlGetMetricStatesOnTarget", C.astlGetMetricStatesOnTarget(&request)); err != nil {
		return nil, err
	}

	states := make([]MetricState, 0, int(*count))
	for _, item := range statesBuf[:int(*count)] {
		state := MetricState{
			Name:        goString(item.name),
			Description: goString(item.description),
		}
		if metric.MetricType == MetricFiniteSetValue {
			state.Value = decodeValue(item.value, metric.ValueType)
		}
		states = append(states, state)
	}
	return states, nil
}

func goString(s *C.char) string {
	if s == nil {
		return ""
	}
	return C.GoString(s)
}

func checkStatus(op string, code C.astl_status_code) error {
	if Status(code) == StatusSuccess {
		return nil
	}
	return Error{Op: op, Status: Status(code)}
}

func toCCollectionParams(params CollectionParameters) C.astl_collection_params_t {
	return C.astl_collection_params_t{
		size:              C.size_t(C.sizeof_astl_collection_params_t),
		flags:             C.uint32_t(params.Flags),
		sampling_interval: C.uint32_t(params.SamplingInterval),
		collection_mode:   C.astl_collection_mode_t(params.Mode),
	}
}

func allocPlatformProps(op string) (*C.astl_platform_props_t, func(), error) {
	ptr := (*C.astl_platform_props_t)(C.calloc(1, C.size_t(C.sizeof_astl_platform_props_t)))
	if ptr == nil {
		return nil, nil, Error{Op: op, Status: StatusOutOfMemory}
	}
	ptr.size = C.size_t(C.sizeof_astl_platform_props_t)
	return ptr, func() { C.free(unsafe.Pointer(ptr)) }, nil
}

func allocUint32(op string) (*C.uint32_t, func(), error) {
	ptr := (*C.uint32_t)(C.calloc(1, C.size_t(C.sizeof_uint32_t)))
	if ptr == nil {
		return nil, nil, Error{Op: op, Status: StatusOutOfMemory}
	}
	return ptr, func() { C.free(unsafe.Pointer(ptr)) }, nil
}

func allocCollectionParams(op string, params CollectionParameters) (*C.astl_collection_params_t, func(), error) {
	ptr := (*C.astl_collection_params_t)(C.calloc(1, C.size_t(C.sizeof_astl_collection_params_t)))
	if ptr == nil {
		return nil, nil, Error{Op: op, Status: StatusOutOfMemory}
	}
	*ptr = toCCollectionParams(params)
	return ptr, func() { C.free(unsafe.Pointer(ptr)) }, nil
}

func fetchMetricGroupMetrics(op string, statusOp string, count uint32,
	request func(*C.uint32_t, *C.astl_metric_props_t) C.astl_status_code) ([]Metric, error) {
	if count == 0 {
		return []Metric{}, nil
	}

	countPtr, releaseCount, err := allocResultCount(op, count)
	if err != nil {
		return nil, err
	}
	defer releaseCount()

	ptr := (*C.astl_metric_props_t)(C.calloc(C.size_t(count), C.size_t(C.sizeof_astl_metric_props_t)))
	if ptr == nil {
		return nil, Error{Op: op, Status: StatusOutOfMemory}
	}
	defer C.free(unsafe.Pointer(ptr))

	metricsBuf := unsafe.Slice(ptr, int(count))
	metricsBuf[0].size = C.size_t(C.sizeof_astl_metric_props_t)

	if err := checkStatus(statusOp, request(countPtr, ptr)); err != nil {
		return nil, err
	}
	return buildMetrics(metricsBuf[:int(*countPtr)]), nil
}

func resolveGlobalMetricGroup(group MetricGroup) (MetricGroup, error) {
	groups, err := GetMetricGroups()
	if err != nil {
		return group, err
	}
	for _, candidate := range groups {
		if candidate.Handle == group.Handle {
			return candidate, nil
		}
	}
	return group, nil
}

func resolveMetricGroupOnTarget(target Target, group MetricGroup) (MetricGroup, error) {
	groups, err := GetMetricGroupsOnTarget(target)
	if err != nil {
		return group, err
	}
	for _, candidate := range groups {
		if candidate.Handle == group.Handle {
			return candidate, nil
		}
	}
	return group, nil
}

func allocMetricStatistics(flags MetricStatisticsFlags) (*C.astl_metric_statistics_t, func(), error) {
	ptr := (*C.astl_metric_statistics_t)(C.calloc(1, C.size_t(C.sizeof_astl_metric_statistics_t)))
	if ptr == nil {
		return nil, nil, Error{Op: "GetMetricStatisticsOnTarget", Status: StatusOutOfMemory}
	}
	ptr.size = C.size_t(C.sizeof_astl_metric_statistics_t)
	ptr.flags = C.uint32_t(flags)
	return ptr, func() { C.free(unsafe.Pointer(ptr)) }, nil
}

func cTargetHandle(target Target) C.astl_target_handle_t {
	return C.astl_target_handle_t(unsafe.Pointer(target.Handle))
}

func cCounterHandle(counter Counter) C.astl_counter_handle_t {
	return C.astl_counter_handle_t(unsafe.Pointer(counter.Handle))
}

func cMetricHandle(metric Metric) C.astl_metric_handle_t {
	return C.astl_metric_handle_t(unsafe.Pointer(metric.Handle))
}

func cMetricGroupHandle(group MetricGroup) C.astl_metric_group_handle_t {
	return C.astl_metric_group_handle_t(unsafe.Pointer(group.Handle))
}

func decodeValue(value C.astl_value_t, valueType ValueType) any {
	var decoded any
	switch valueType {
	case ValueUInt8:
		decoded = uint8(C.astl_go_value_ui8(value))
	case ValueUInt16:
		decoded = uint16(C.astl_go_value_ui16(value))
	case ValueUInt32:
		decoded = uint32(C.astl_go_value_ui32(value))
	case ValueUInt64:
		decoded = uint64(C.astl_go_value_ui64(value))
	case ValueFloat32:
		decoded = float32(C.astl_go_value_fp32(value))
	case ValueFloat64:
		decoded = float64(C.astl_go_value_fp64(value))
	case ValueBool8:
		decoded = bool(C.astl_go_value_b8(value))
	default:
		decoded = uint64(C.astl_go_value_ui64(value))
	}
	return decoded
}

func makeCounterHandleArray(op string, counters []Counter) (*C.astl_counter_handle_t, func(), error) {
	ptr, release, err := allocHandleArray(op, len(counters), unsafe.Sizeof(C.astl_counter_handle_t(nil)))
	if err != nil {
		return nil, nil, err
	}

	handles := unsafe.Slice((*C.astl_counter_handle_t)(ptr), len(counters))
	for i, counter := range counters {
		handles[i] = cCounterHandle(counter)
	}

	return (*C.astl_counter_handle_t)(ptr), release, nil
}

func makeMetricHandleArray(op string, metrics []Metric) (*C.astl_metric_handle_t, func(), error) {
	ptr, release, err := allocHandleArray(op, len(metrics), unsafe.Sizeof(C.astl_metric_handle_t(nil)))
	if err != nil {
		return nil, nil, err
	}

	handles := unsafe.Slice((*C.astl_metric_handle_t)(ptr), len(metrics))
	for i, metric := range metrics {
		handles[i] = cMetricHandle(metric)
	}

	return (*C.astl_metric_handle_t)(ptr), release, nil
}

func makeMetricGroupHandleArray(op string, groups []MetricGroup) (*C.astl_metric_group_handle_t, func(), error) {
	ptr, release, err := allocHandleArray(op, len(groups), unsafe.Sizeof(C.astl_metric_group_handle_t(nil)))
	if err != nil {
		return nil, nil, err
	}

	handles := unsafe.Slice((*C.astl_metric_group_handle_t)(ptr), len(groups))
	for i, group := range groups {
		handles[i] = cMetricGroupHandle(group)
	}

	return (*C.astl_metric_group_handle_t)(ptr), release, nil
}

func withHandles[T any, H any](op string, items []T, makeHandles func(string, []T) (*H, func(), error), fn func(*H) error) error {
	handles, release, err := makeHandles(op, items)
	if err != nil {
		return err
	}
	defer release()
	return fn(handles)
}

func withCollectionParams(op string, params CollectionParameters, fn func(*C.astl_collection_params_t) error) error {
	collectionParams, releaseParams, err := allocCollectionParams(op, params)
	if err != nil {
		return err
	}
	defer releaseParams()
	return fn(collectionParams)
}

type targetCollectionConfig[T any, H any] struct {
	op          string
	statusOp    string
	makeHandles func(string, []T) (*H, func(), error)
	configure   func(C.astl_target_handle_t, *C.astl_collection_params_t, *H, C.uint32_t) C.astl_status_code
}

type collectionConfig[T any, H any] struct {
	op          string
	statusOp    string
	makeHandles func(string, []T) (*H, func(), error)
	configure   func(*C.astl_collection_params_t, *H, C.uint32_t) C.astl_status_code
}

func configureOnTarget[T any, H any](target Target, params CollectionParameters, items []T, config targetCollectionConfig[T, H]) error {
	targetHandle := cTargetHandle(target)
	return withHandles(config.op, items, config.makeHandles, func(handles *H) error {
		return withCollectionParams(config.op, params, func(collectionParams *C.astl_collection_params_t) error {
			return checkStatus(config.statusOp, config.configure(targetHandle, collectionParams, handles, C.uint32_t(len(items))))
		})
	})
}

func configureCollection[T any, H any](params CollectionParameters, items []T, config collectionConfig[T, H]) error {
	return withHandles(config.op, items, config.makeHandles, func(handles *H) error {
		return withCollectionParams(config.op, params, func(collectionParams *C.astl_collection_params_t) error {
			return checkStatus(config.statusOp, config.configure(collectionParams, handles, C.uint32_t(len(items))))
		})
	})
}

func counterTargetCollectionConfig() targetCollectionConfig[Counter, C.astl_counter_handle_t] {
	return targetCollectionConfig[Counter, C.astl_counter_handle_t]{
		op:          "ConfigureCountersOnTarget",
		statusOp:    "astlConfigureCounterCollectionOnTarget",
		makeHandles: makeCounterHandleArray,
		configure:   configureCounterCollectionOnTargetRequest,
	}
}

func counterCollectionConfig() collectionConfig[Counter, C.astl_counter_handle_t] {
	return collectionConfig[Counter, C.astl_counter_handle_t]{
		op:          "ConfigureCounters",
		statusOp:    "astlConfigureCounterCollection",
		makeHandles: makeCounterHandleArray,
		configure:   configureCounterCollectionRequest,
	}
}

func metricTargetCollectionConfig() targetCollectionConfig[Metric, C.astl_metric_handle_t] {
	return targetCollectionConfig[Metric, C.astl_metric_handle_t]{
		op:          "ConfigureMetricsOnTarget",
		statusOp:    "astlConfigureMetricCollectionOnTarget",
		makeHandles: makeMetricHandleArray,
		configure:   configureMetricCollectionOnTargetRequest,
	}
}

func metricCollectionConfig() collectionConfig[Metric, C.astl_metric_handle_t] {
	return collectionConfig[Metric, C.astl_metric_handle_t]{
		op:          "ConfigureMetrics",
		statusOp:    "astlConfigureMetricCollection",
		makeHandles: makeMetricHandleArray,
		configure:   configureMetricCollectionRequest,
	}
}

func metricGroupTargetCollectionConfig() targetCollectionConfig[MetricGroup, C.astl_metric_group_handle_t] {
	return targetCollectionConfig[MetricGroup, C.astl_metric_group_handle_t]{
		op:          "ConfigureMetricGroupsOnTarget",
		statusOp:    "astlConfigureMetricGroupCollectionOnTarget",
		makeHandles: makeMetricGroupHandleArray,
		configure:   configureMetricGroupCollectionOnTargetRequest,
	}
}

func metricGroupCollectionConfig() collectionConfig[MetricGroup, C.astl_metric_group_handle_t] {
	return collectionConfig[MetricGroup, C.astl_metric_group_handle_t]{
		op:          "ConfigureMetricGroups",
		statusOp:    "astlConfigureMetricGroupCollection",
		makeHandles: makeMetricGroupHandleArray,
		configure:   configureMetricGroupCollectionRequest,
	}
}

func configureCounterCollectionOnTargetRequest(targetHandle C.astl_target_handle_t, collectionParams *C.astl_collection_params_t,
	handles *C.astl_counter_handle_t, count C.uint32_t) C.astl_status_code {
	request := C.astl_configure_counter_collection_on_target_params_t{
		size:              C.size_t(C.sizeof_astl_configure_counter_collection_on_target_params_t),
		flags:             0,
		target_handle:     targetHandle,
		collection_params: collectionParams,
		counter_handles:   handles,
		counter_count:     count,
	}
	return C.astlConfigureCounterCollectionOnTarget(&request)
}

func configureCounterCollectionRequest(collectionParams *C.astl_collection_params_t, handles *C.astl_counter_handle_t,
	count C.uint32_t) C.astl_status_code {
	request := C.astl_configure_counter_collection_params_t{
		size:              C.size_t(C.sizeof_astl_configure_counter_collection_params_t),
		flags:             0,
		collection_params: collectionParams,
		counter_handles:   handles,
		counter_count:     count,
	}
	return C.astlConfigureCounterCollection(&request)
}

func configureMetricCollectionOnTargetRequest(targetHandle C.astl_target_handle_t, collectionParams *C.astl_collection_params_t,
	handles *C.astl_metric_handle_t, count C.uint32_t) C.astl_status_code {
	request := C.astl_configure_metric_collection_on_target_params_t{
		size:              C.size_t(C.sizeof_astl_configure_metric_collection_on_target_params_t),
		flags:             0,
		target_handle:     targetHandle,
		collection_params: collectionParams,
		metric_handles:    handles,
		metric_count:      count,
	}
	return C.astlConfigureMetricCollectionOnTarget(&request)
}

func configureMetricCollectionRequest(collectionParams *C.astl_collection_params_t, handles *C.astl_metric_handle_t,
	count C.uint32_t) C.astl_status_code {
	request := C.astl_configure_metric_collection_params_t{
		size:              C.size_t(C.sizeof_astl_configure_metric_collection_params_t),
		flags:             0,
		collection_params: collectionParams,
		metric_handles:    handles,
		metric_count:      count,
	}
	return C.astlConfigureMetricCollection(&request)
}

func configureMetricGroupCollectionOnTargetRequest(targetHandle C.astl_target_handle_t,
	collectionParams *C.astl_collection_params_t, handles *C.astl_metric_group_handle_t, count C.uint32_t) C.astl_status_code {
	request := C.astl_configure_metric_group_collection_on_target_params_t{
		size:                 C.size_t(C.sizeof_astl_configure_metric_group_collection_on_target_params_t),
		flags:                0,
		target_handle:        targetHandle,
		collection_params:    collectionParams,
		metric_group_handles: handles,
		metric_group_count:   count,
	}
	return C.astlConfigureMetricGroupCollectionOnTarget(&request)
}

func configureMetricGroupCollectionRequest(collectionParams *C.astl_collection_params_t,
	handles *C.astl_metric_group_handle_t, count C.uint32_t) C.astl_status_code {
	request := C.astl_configure_metric_group_collection_params_t{
		size:                 C.size_t(C.sizeof_astl_configure_metric_group_collection_params_t),
		flags:                0,
		collection_params:    collectionParams,
		metric_group_handles: handles,
		metric_group_count:   count,
	}
	return C.astlConfigureMetricGroupCollection(&request)
}

func isToleratedStatus(status Status, tolerated []Status) bool {
	for _, allowed := range tolerated {
		if status == allowed {
			return true
		}
	}
	return false
}

func queryCount(op string, countOp string, tolerated []Status, fn func(*C.uint32_t) C.astl_status_code) (uint32, error) {
	count, releaseCount, err := allocUint32(op)
	if err != nil {
		return 0, err
	}
	defer releaseCount()

	status := Status(fn(count))
	if isToleratedStatus(status, tolerated) {
		return 0, nil
	}
	if err := checkStatus(countOp, C.astl_status_code(status)); err != nil {
		return 0, err
	}
	return uint32(*count), nil
}

type listQuery[T any, R any] struct {
	countOp        string
	toleratedCount []Status
	fetchOp        string
	toleratedFetch []Status
	query          func(*C.uint32_t) C.astl_status_code
	elemSize       C.size_t
	init           func([]T)
	fetch          func([]T, *C.uint32_t) C.astl_status_code
	build          func([]T) []R
}

type targetListQuery[T any, R any] struct {
	op             string
	countOp        string
	toleratedCount []Status
	fetchOp        string
	toleratedFetch []Status
	elemSize       C.size_t
	init           func([]T)
	query          func(C.astl_target_handle_t, *C.uint32_t) C.astl_status_code
	fetch          func(C.astl_target_handle_t, []T, *C.uint32_t) C.astl_status_code
	build          func([]T) []R
}

type fetchContext[T any] struct {
	items       []T
	resultCount *C.uint32_t
	release     func()
}

func fetchList[T any, R any](op string, query listQuery[T, R]) (result []R, err error) {
	count, err := queryCount(op, query.countOp, query.toleratedCount, query.query)
	if err != nil || count == 0 {
		if err == nil {
			result = []R{}
		}
		return
	}

	ctx, err := newFetchContext[T](op, count, query.elemSize, query.init)
	if err != nil {
		return
	}
	defer ctx.release()

	status := Status(query.fetch(ctx.items, ctx.resultCount))
	if isToleratedStatus(status, query.toleratedFetch) {
		result = []R{}
		return
	}
	err = checkStatus(query.fetchOp, C.astl_status_code(status))
	if err == nil {
		result = query.build(ctx.items[:int(*ctx.resultCount)])
	}
	return
}

func fetchTargetList[T any, R any](target Target, query targetListQuery[T, R]) ([]R, error) {
	targetHandle := cTargetHandle(target)
	return fetchList(query.op, listQuery[T, R]{
		countOp:        query.countOp,
		toleratedCount: query.toleratedCount,
		fetchOp:        query.fetchOp,
		toleratedFetch: query.toleratedFetch,
		query: func(count *C.uint32_t) C.astl_status_code {
			return query.query(targetHandle, count)
		},
		elemSize: query.elemSize,
		init:     query.init,
		fetch: func(items []T, count *C.uint32_t) C.astl_status_code {
			return query.fetch(targetHandle, items, count)
		},
		build: query.build,
	})
}

func counterListQuery() targetListQuery[C.astl_counter_props_t, Counter] {
	return targetListQuery[C.astl_counter_props_t, Counter]{
		op:             "GetCountersOnTarget",
		countOp:        "astlGetCounterCountOnTarget",
		toleratedCount: []Status{StatusNoCountersFound},
		fetchOp:        "astlGetCountersOnTarget",
		elemSize:       C.size_t(C.sizeof_astl_counter_props_t),
		init:           initCounterProps,
		query:          queryCounterCount,
		fetch:          fetchCounters,
		build:          buildCounters,
	}
}

func metricListQuery() targetListQuery[C.astl_metric_props_t, Metric] {
	return targetListQuery[C.astl_metric_props_t, Metric]{
		op:             "GetMetricsOnTarget",
		countOp:        "astlGetMetricCountOnTarget",
		toleratedCount: []Status{StatusNoMetricsFound},
		fetchOp:        "astlGetMetricsOnTarget",
		elemSize:       C.size_t(C.sizeof_astl_metric_props_t),
		init:           initMetricProps,
		query:          queryMetricCount,
		fetch:          fetchMetrics,
		build:          buildMetrics,
	}
}

func metricGroupListQueryOnTarget() targetListQuery[C.astl_metric_group_props_t, MetricGroup] {
	tolerated := []Status{StatusNoMetricGroupsFound}
	return targetListQuery[C.astl_metric_group_props_t, MetricGroup]{
		op:             "GetMetricGroups",
		countOp:        "astlGetMetricGroupCountOnTarget",
		toleratedCount: tolerated,
		fetchOp:        "astlGetMetricGroupsOnTarget",
		toleratedFetch: tolerated,
		elemSize:       C.size_t(C.sizeof_astl_metric_group_props_t),
		init:           initMetricGroupProps,
		query:          queryMetricGroupCountOnTarget,
		fetch:          fetchMetricGroupsOnTarget,
		build:          buildMetricGroups,
	}
}

func metricGroupListQuery() listQuery[C.astl_metric_group_props_t, MetricGroup] {
	tolerated := []Status{StatusNoMetricGroupsFound}
	return listQuery[C.astl_metric_group_props_t, MetricGroup]{
		countOp:        "astlGetMetricGroupCount",
		toleratedCount: tolerated,
		fetchOp:        "astlGetMetricGroups",
		toleratedFetch: tolerated,
		elemSize:       C.size_t(C.sizeof_astl_metric_group_props_t),
		init:           initMetricGroupProps,
		query:          queryMetricGroupCount,
		fetch:          fetchMetricGroups,
		build:          buildMetricGroups,
	}
}

func newFetchContext[T any](op string, count uint32, elemSize C.size_t, init func([]T)) (fetchContext[T], error) {
	ptr, release, err := allocArray(op, count, elemSize)
	if err != nil {
		return fetchContext[T]{}, err
	}

	resultCount, releaseResultCount, err := allocResultCount(op, count)
	if err != nil {
		release()
		return fetchContext[T]{}, err
	}

	items := unsafe.Slice((*T)(ptr), int(count))
	init(items)

	return fetchContext[T]{
		items:       items,
		resultCount: resultCount,
		release: func() {
			releaseResultCount()
			release()
		},
	}, nil
}

func initCounterProps(items []C.astl_counter_props_t) {
	items[0].size = C.size_t(C.sizeof_astl_counter_props_t)
}

func queryCounterCount(targetHandle C.astl_target_handle_t, count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_counter_count_params_t{
		size:          C.size_t(C.sizeof_astl_get_counter_count_params_t),
		flags:         0,
		target_handle: targetHandle,
		counter_count: count,
	}
	return C.astlGetCounterCountOnTarget(&params)
}

func fetchCounters(targetHandle C.astl_target_handle_t, items []C.astl_counter_props_t, count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_counters_params_t{
		size:          C.size_t(C.sizeof_astl_get_counters_params_t),
		flags:         0,
		target_handle: targetHandle,
		counters:      &items[0],
		counter_count: count,
	}
	return C.astlGetCountersOnTarget(&params)
}

func initMetricProps(items []C.astl_metric_props_t) {
	items[0].size = C.size_t(C.sizeof_astl_metric_props_t)
}

func queryMetricCount(targetHandle C.astl_target_handle_t, count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_metric_count_params_t{
		size:          C.size_t(C.sizeof_astl_get_metric_count_params_t),
		flags:         0,
		target_handle: targetHandle,
		metric_count:  count,
	}
	return C.astlGetMetricCountOnTarget(&params)
}

func fetchMetrics(targetHandle C.astl_target_handle_t, items []C.astl_metric_props_t, count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_metrics_params_t{
		size:          C.size_t(C.sizeof_astl_get_metrics_params_t),
		flags:         0,
		target_handle: targetHandle,
		metrics:       &items[0],
		metric_count:  count,
	}
	return C.astlGetMetricsOnTarget(&params)
}

func initMetricGroupProps(items []C.astl_metric_group_props_t) {
	items[0].size = C.size_t(C.sizeof_astl_metric_group_props_t)
}

func queryMetricGroupCount(count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_metric_group_count_params_t{
		size:               C.size_t(C.sizeof_astl_get_metric_group_count_params_t),
		flags:              0,
		metric_group_count: count,
	}
	return C.astlGetMetricGroupCount(&params)
}

func queryMetricGroupCountOnTarget(targetHandle C.astl_target_handle_t, count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_metric_group_count_on_target_params_t{
		size:               C.size_t(C.sizeof_astl_get_metric_group_count_on_target_params_t),
		flags:              0,
		target_handle:      targetHandle,
		metric_group_count: count,
	}
	return C.astlGetMetricGroupCountOnTarget(&params)
}

func fetchMetricGroups(items []C.astl_metric_group_props_t, count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_metric_groups_params_t{
		size:               C.size_t(C.sizeof_astl_get_metric_groups_params_t),
		flags:              0,
		metric_groups:      &items[0],
		metric_group_count: count,
	}
	return C.astlGetMetricGroups(&params)
}

func fetchMetricGroupsOnTarget(targetHandle C.astl_target_handle_t, items []C.astl_metric_group_props_t, count *C.uint32_t) C.astl_status_code {
	params := C.astl_get_metric_groups_on_target_params_t{
		size:               C.size_t(C.sizeof_astl_get_metric_groups_on_target_params_t),
		flags:              0,
		target_handle:      targetHandle,
		metric_groups:      &items[0],
		metric_group_count: count,
	}
	return C.astlGetMetricGroupsOnTarget(&params)
}

func allocArray(op string, count uint32, elemSize C.size_t) (unsafe.Pointer, func(), error) {
	ptr := C.calloc(C.size_t(count), elemSize)
	if ptr == nil {
		return nil, nil, Error{Op: op, Status: StatusOutOfMemory}
	}
	return ptr, func() { C.free(ptr) }, nil
}

func allocResultCount(op string, count uint32) (*C.uint32_t, func(), error) {
	ptr, release, err := allocUint32(op)
	if err != nil {
		return nil, nil, err
	}
	*ptr = C.uint32_t(count)
	return ptr, release, nil
}

func allocHandleArray(op string, count int, elemSize uintptr) (unsafe.Pointer, func(), error) {
	if count == 0 {
		return nil, nil, Error{Op: op, Status: StatusBadArgument}
	}

	ptr := C.malloc(C.size_t(count) * C.size_t(elemSize))
	if ptr == nil {
		return nil, nil, Error{Op: op, Status: StatusOutOfMemory}
	}
	return ptr, func() { C.free(ptr) }, nil
}

func buildTargets(items []C.astl_target_props_t) []Target {
	targets := make([]Target, 0, len(items))
	for _, item := range items {
		targets = append(targets, Target{
			Handle:       uintptr(item.handle),
			ParentHandle: uintptr(item.parent_handle),
			Name:         goString(item.name),
			Description:  goString(item.description),
			ID:           goString(item.id),
		})
	}
	return targets
}

func buildCounters(items []C.astl_counter_props_t) []Counter {
	return buildSlice(items, func(item C.astl_counter_props_t) Counter {
		return Counter{
			Handle:              uintptr(item.handle),
			Name:                goString(item.name),
			Description:         goString(item.description),
			MinSamplingInterval: uint32(item.min_sampling_interval),
			Units:               Units(item.units),
			Formula:             goString(item.formula),
			ValueType:           ValueType(item.value_type),
			CounterType:         CounterType(item.counter_type),
		}
	})
}

func buildMetrics(items []C.astl_metric_props_t) []Metric {
	return buildSlice(items, func(item C.astl_metric_props_t) Metric {
		return Metric{
			Handle:              uintptr(item.handle),
			Name:                goString(item.name),
			Description:         goString(item.description),
			MinSamplingInterval: uint32(item.min_sampling_interval),
			Units:               Units(item.units),
			ValueType:           ValueType(item.value_type),
			MetricType:          MetricType(item.metric_type),
			Identifier:          MetricIdentifier(item.identifier),
		}
	})
}

func buildMetricGroups(items []C.astl_metric_group_props_t) []MetricGroup {
	groups := make([]MetricGroup, 0, len(items))
	for _, item := range items {
		groups = append(groups, MetricGroup{
			Handle:      uintptr(item.handle),
			Name:        goString(item.name),
			Description: goString(item.description),
		})
	}
	return groups
}

func buildSlice[T any, R any](items []T, mapItem func(T) R) []R {
	out := make([]R, 0, len(items))
	for _, item := range items {
		out = append(out, mapItem(item))
	}
	return out
}
