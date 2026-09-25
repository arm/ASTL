// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

package astl

import (
	"reflect"
	"testing"
)

func TestVersionStringNonEmpty(t *testing.T) {
	if VersionString() == "" {
		t.Fatal("VersionString returned an empty string")
	}
}

func TestStatusNameNonEmpty(t *testing.T) {
	if StatusName(StatusSuccess) == "" {
		t.Fatal("StatusName(StatusSuccess) returned an empty string")
	}
}

func TestGetSystemInfo(t *testing.T) {
	info, err := GetSystemInfo()
	if err != nil {
		t.Fatalf("GetSystemInfo failed: %v", err)
	}
	if info.Flags != SystemInfoFlagHost && info.Flags != SystemInfoFlagLoadedSession {
		t.Fatalf("SystemInfo.Flags = %#x, want host or loaded-session", info.Flags)
	}
}

func TestSystemInfoFieldsMatchCPlatformProps(t *testing.T) {
	gotType := reflect.TypeOf(SystemInfo{})
	gotFields := make([]string, 0, gotType.NumField())
	for index := 0; index < gotType.NumField(); index++ {
		gotFields = append(gotFields, gotType.Field(index).Name)
	}

	wantFields := []string{
		"Flags",
		"SoCName",
		"VendorID",
		"OSName",
		"KernelName",
		"KernelVersion",
		"KernelRelease",
		"FirmwareVersion",
		"Hostname",
		"Architecture",
		"CPUType",
		"CPUFeatures",
		"CacheInfo",
		"CoreCount",
		"NUMANodeCount",
		"SocketCount",
		"CacheLineSizeBytes",
		"MemoryTotalBytes",
		"LibcVersion",
		"BootInfo",
		"HugePagesTotal",
		"HugePageSizeKB",
		"TransparentHugePages",
	}

	if !reflect.DeepEqual(gotFields, wantFields) {
		t.Fatalf("SystemInfo fields = %#v, want %#v", gotFields, wantFields)
	}
}

func TestGetTargets(t *testing.T) {
	targets, err := GetTargets()
	if err != nil {
		if astlErr, ok := err.(Error); ok && astlErr.Status == StatusBadConfiguration {
			t.Skipf("GetTargets requires initialized ASTL configuration in this environment: %v", err)
		}
		t.Fatalf("GetTargets failed: %v", err)
	}
	if len(targets) == 0 {
		t.Log("no targets discovered on this host")
	}
}

func TestCounterPreservesQualifiedSCMIName(t *testing.T) {
	counter := Counter{Name: "SOC.0.ENERGY_COUNTER"}

	if counter.Name != "SOC.0.ENERGY_COUNTER" {
		t.Fatalf("Counter.Name = %q, want qualified SCMI counter name", counter.Name)
	}
}

func TestEnumAlignment(t *testing.T) {
	if UnitsRPM != 11 {
		t.Fatalf("UnitsRPM = %d, want 11", UnitsRPM)
	}
	if UnitsCount != 12 {
		t.Fatalf("UnitsCount = %d, want 12", UnitsCount)
	}
	if UnitsUnknown != 0xFFFFFFFF {
		t.Fatalf("UnitsUnknown = %#x, want 0xFFFFFFFF", UnitsUnknown)
	}

	if MetricIdentifierBandwidth != 11 {
		t.Fatalf("MetricIdentifierBandwidth = %d, want 11", MetricIdentifierBandwidth)
	}
	if MetricIdentifierFanSpeed != 12 {
		t.Fatalf("MetricIdentifierFanSpeed = %d, want 12", MetricIdentifierFanSpeed)
	}
	if MetricIdentifierUnknown != 0xFFFFFFFF {
		t.Fatalf("MetricIdentifierUnknown = %#x, want 0xFFFFFFFF", MetricIdentifierUnknown)
	}
}

func TestStoppedCollectionRequiresReconfiguration(t *testing.T) {
	targets, err := GetTargets()
	if err != nil {
		t.Fatal(err)
	}
	for _, target := range targets {
		counters, err := GetCountersOnTarget(target)
		if err != nil {
			t.Fatal(err)
		}
		if len(counters) == 0 {
			continue
		}
		params := CollectionParameters{SamplingInterval: 10, Mode: CollectionModeImmediate}
		configureAndStart := func() {
			t.Helper()
			if err := ConfigureCountersOnTarget(target, params, counters[:1]); err != nil {
				t.Fatal(err)
			}
			if err := StartCollectionOnTarget(target); err != nil {
				t.Fatal(err)
			}
		}
		configureAndStart()
		if err := StopCollectionOnTarget(target); err != nil {
			t.Fatal(err)
		}
		for _, check := range []struct {
			name   string
			action func(Target) error
			want   Status
		}{
			{"stop", StopCollectionOnTarget, StatusCollectionAlreadyStopped},
			{"pause", PauseCollectionOnTarget, StatusCollectionAlreadyStopped},
			{"resume", ResumeCollectionOnTarget, StatusCollectionAlreadyStopped},
			{"start", StartCollectionOnTarget, StatusInvalidStateTransition},
		} {
			err := check.action(target)
			statusErr, ok := err.(Error)
			if !ok || statusErr.Status != check.want {
				t.Fatalf("%s on stopped collection returned %v, want %v", check.name, err, check.want)
			}
		}
		configureAndStart()
		if err := StopCollectionOnTarget(target); err != nil {
			t.Fatal(err)
		}
		return
	}
	t.Skip("No target with counters available")
}
