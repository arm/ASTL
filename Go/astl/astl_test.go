package astl

import "testing"

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
	if _, err := GetSystemInfo(); err != nil {
		t.Fatalf("GetSystemInfo failed: %v", err)
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
