package astl

import "testing"

func assertUint32EnumParity[T ~uint32](t *testing.T, name string, got, want T) {
	t.Helper()
	if got != want {
		t.Fatalf("%s: got %d, want %d", name, got, want)
	}
}

func TestStatusEnumABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  Status
		want Status
	}{
		{"success", StatusSuccess, 0},
		{"bad_argument", StatusBadArgument, 1},
		{"bad_configuration", StatusBadConfiguration, 2},
		{"invalid_target_handle", StatusInvalidTargetHandle, 3},
		{"invalid_counter_handle", StatusInvalidCounterHandle, 4},
		{"invalid_metric_handle", StatusInvalidMetricHandle, 5},
		{"invalid_metric_group_handle", StatusInvalidMetricGroupHandle, 6},
		{"not_supported", StatusNotSupported, 8},
		{"deprecated_api", StatusDeprecatedAPI, 9},
		{"no_target_found", StatusNoTargetFound, 10},
		{"old_struct_version", StatusOldStructVersion, 11},
		{"new_struct_version", StatusNewStructVersion, 12},
		{"no_counters_found", StatusNoCountersFound, 13},
		{"no_metrics_found", StatusNoMetricsFound, 14},
		{"no_metric_groups_found", StatusNoMetricGroupsFound, 15},
		{"buffer_too_small", StatusBufferTooSmall, 16},
		{"metric_received_invalid_sample", StatusMetricReceivedInvalidSample, 17},
		{"metric_overflow_detected", StatusMetricOverflowDetected, 18},
		{"invalid_sampling_interval", StatusInvalidSamplingInterval, 19},
		{"sampling_interval_ignored", StatusSamplingIntervalIgnored, 20},
		{"invalid_collection_mode", StatusInvalidCollectionMode, 21},
		{"invalid_flag_value", StatusInvalidFlagValue, 22},
		{"counter_not_supported_on_target", StatusCounterNotSupportedOnTarget, 23},
		{"metric_not_supported_on_target", StatusMetricNotSupportedOnTarget, 24},
		{"metric_group_not_supported_on_target", StatusMetricGroupNotSupportedOnTarget, 25},
		{"collection_not_configured", StatusCollectionNotConfigured, 26},
		{"collection_not_running", StatusCollectionNotRunning, 27},
		{"collection_not_stopped", StatusCollectionNotStopped, 28},
		{"collection_not_paused", StatusCollectionNotPaused, 29},
		{"collection_already_running", StatusCollectionAlreadyRunning, 30},
		{"collection_already_stopped", StatusCollectionAlreadyStopped, 31},
		{"collection_already_paused", StatusCollectionAlreadyPaused, 32},
		{"no_data_collected", StatusNoDataCollected, 33},
		{"buffer_larger_than_needed", StatusBufferLargerThanNeeded, 34},
		{"unsupported_collector_type", StatusUnsupportedCollectorType, 35},
		{"file_open_failed", StatusFileOpenFailed, 36},
		{"file_error", StatusFileError, 37},
		{"out_of_memory", StatusOutOfMemory, 38},
		{"invalid_value_type", StatusInvalidValueType, 40},
		{"invalid_state_transition", StatusInvalidStateTransition, 41},
		{"pause_unsupported", StatusPauseUnsupported, 42},
		{"resume_unsupported", StatusResumeUnsupported, 43},
		{"internal_error", StatusInternalError, 127},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestValueTypeEnumABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  ValueType
		want ValueType
	}{
		{"unknown", ValueUnknown, ValueType(^uint32(0))},
		{"uint8", ValueUInt8, 0},
		{"uint16", ValueUInt16, 1},
		{"uint32", ValueUInt32, 2},
		{"uint64", ValueUInt64, 3},
		{"float32", ValueFloat32, 4},
		{"float64", ValueFloat64, 5},
		{"bool8", ValueBool8, 6},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestCounterTypeEnumABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  CounterType
		want CounterType
	}{
		{"unknown", CounterTypeUnknown, CounterType(^uint32(0))},
		{"value", CounterTypeValue, 0},
		{"count", CounterTypeCount, 1},
		{"event", CounterTypeEvent, 2},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestMetricTypeEnumABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  MetricType
		want MetricType
	}{
		{"unknown", MetricUnknown, MetricType(^uint32(0))},
		{"value", MetricValue, 0},
		{"finite_set_value", MetricFiniteSetValue, 1},
		{"event", MetricEvent, 2},
		{"delta", MetricDelta, 3},
		{"residency", MetricResidency, 4},
		{"rate", MetricRate, 5},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestUnitsEnumABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  Units
		want Units
	}{
		{"unknown", UnitsUnknown, Units(^uint32(0))},
		{"none", UnitsNone, 0},
		{"ticks", UnitsTicks, 1},
		{"seconds", UnitsSeconds, 2},
		{"celsius", UnitsCelsius, 3},
		{"joules", UnitsJoules, 4},
		{"watts", UnitsWatts, 5},
		{"volts", UnitsVolts, 6},
		{"amps", UnitsAmps, 7},
		{"bytes", UnitsBytes, 8},
		{"mbytes_per_sec", UnitsMBytesPerSec, 9},
		{"mhertz", UnitsMHz, 10},
		{"rpm", UnitsRPM, 11},
		{"count", UnitsCount, 12},
		{"percent", UnitsPercent, 13},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestMetricIdentifierEnumABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  MetricIdentifier
		want MetricIdentifier
	}{
		{"unknown", MetricIdentifierUnknown, MetricIdentifier(^uint32(0))},
		{"count", MetricIdentifierCount, 0},
		{"temperature", MetricIdentifierTemperature, 1},
		{"thermal_limit", MetricIdentifierThermalLimit, 2},
		{"thermal_throttle", MetricIdentifierThermalThrottle, 3},
		{"energy", MetricIdentifierEnergy, 4},
		{"power", MetricIdentifierPower, 5},
		{"power_limit", MetricIdentifierPowerLimit, 6},
		{"power_throttle", MetricIdentifierPowerThrottle, 7},
		{"frequency", MetricIdentifierFrequency, 8},
		{"voltage", MetricIdentifierVoltage, 9},
		{"current", MetricIdentifierCurrent, 10},
		{"bandwidth", MetricIdentifierBandwidth, 11},
		{"fan_speed", MetricIdentifierFanSpeed, 12},
		{"humidity", MetricIdentifierHumidity, 13},
		{"status", MetricIdentifierStatus, 14},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestCollectionModeEnumABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  CollectionMode
		want CollectionMode
	}{
		{"sampling", CollectionModeSampling, 0},
		{"immediate", CollectionModeImmediate, 1},
		{"snapshot", CollectionModeSnapshot, 2},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestCollectionParameterFlagsABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  CollectionParameterFlags
		want CollectionParameterFlags
	}{
		{"none", CollectionParameterFlagNone, 0},
		{"optimize_overhead", CollectionParameterFlagOptimizeOverhead, 1 << 0},
		{"optimize_memory", CollectionParameterFlagOptimizeMemory, 1 << 1},
		{"optimize_interference", CollectionParameterFlagOptimizeInterference, 1 << 2},
		{"no_caching", CollectionParameterFlagNoCaching, 1 << 3},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestMetricStatisticsFlagsABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  MetricStatisticsFlags
		want MetricStatisticsFlags
	}{
		{"regular_average", MetricStatisticsFlagRegularAverage, 1 << 0},
		{"time_weighted_average", MetricStatisticsFlagTimeWeightedAverage, 1 << 1},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}

func TestSystemInfoFlagsABIParity(t *testing.T) {
	testCases := []struct {
		name string
		got  SystemInfoFlags
		want SystemInfoFlags
	}{
		{"host", SystemInfoFlagHost, 1 << 0},
		{"loaded_session", SystemInfoFlagLoadedSession, 1 << 1},
	}

	for _, testCase := range testCases {
		assertUint32EnumParity(t, testCase.name, testCase.got, testCase.want)
	}
}
