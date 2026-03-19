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
