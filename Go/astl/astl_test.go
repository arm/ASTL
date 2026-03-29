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

	if CategoryBandwidth != 6 {
		t.Fatalf("CategoryBandwidth = %d, want 6", CategoryBandwidth)
	}
	if CategoryFanSpeed != 7 {
		t.Fatalf("CategoryFanSpeed = %d, want 7", CategoryFanSpeed)
	}
	if CategoryUncategorized != 0xFFFFFFFF {
		t.Fatalf("CategoryUncategorized = %#x, want 0xFFFFFFFF", CategoryUncategorized)
	}
}
