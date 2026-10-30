// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

package astl

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"
)

func TestNumaDiscoveryMetadata(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("NUMA discovery is Linux-only")
	}
	if os.Getenv("ASTL_GO_NUMA_TEST_CHILD") != "1" {
		root := t.TempDir()
		for _, node := range []string{"node0", "node2"} {
			path := filepath.Join(root, node)
			if err := os.Mkdir(path, 0700); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(filepath.Join(path, "numastat"), []byte("numa_hit 100\nnuma_miss 20\ninterleave_hit 0\nlocal_node 100\nother_node 20\n"), 0600); err != nil {
				t.Fatal(err)
			}
		}
		config, err := filepath.Abs("../../config")
		if err != nil {
			t.Fatal(err)
		}
		// Isolate ASTL's global session from the rest of the Go suite.
		cmd := exec.Command(os.Args[0], "-test.run=^TestNumaDiscoveryMetadata$")
		cmd.Env = append(os.Environ(), "ASTL_GO_NUMA_TEST_CHILD=1", "ASTL_COLLECTORS=numa", "ASTL_NUMA_ROOT="+root, "ASTL_CONFIG_DIR="+config)
		if output, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("NUMA child: %v\n%s", err, output)
		}
		return
	}
	targets, err := GetTargets()
	if err != nil {
		t.Fatal(err)
	}
	if len(targets) != 1 || targets[0].Name != "numa" {
		t.Fatalf("unexpected targets: %#v", targets)
	}
	metrics, err := GetMetricsOnTarget(targets[0])
	if err != nil {
		t.Fatal(err)
	}
	if len(metrics) != 9 {
		t.Fatalf("got %d metrics, want 9", len(metrics))
	}
	names := map[string]bool{}
	for _, metric := range metrics {
		names[metric.Name] = true
		if metric.Name == "Linux_numa_local_percent" && (metric.Units != UnitsPercent || metric.ValueType != ValueFloat64) {
			t.Fatalf("incorrect percentage metadata: %#v", metric)
		}
	}
	for _, name := range []string{"Linux_numa_hit", "Linux_numa_miss", "Linux_numa_interleave_hit", "Linux_numa_local_node", "Linux_numa_other_node", "Linux_numa_local_percent", "Linux_numa_remote_percent", "numa_node0_allocations", "numa_node2_allocations"} {
		if !names[name] {
			t.Errorf("missing NUMA metric %s", name)
		}
	}
	counters, err := GetCountersOnTarget(targets[0])
	if err != nil {
		t.Fatal(err)
	}
	if len(counters) != 9 {
		t.Fatalf("got %d counters, want 9", len(counters))
	}
	for _, counter := range counters {
		if counter.CounterType != CounterTypeValue {
			t.Errorf("%s must have VALUE semantics, got %v", counter.Name, counter.CounterType)
		}
	}
}
