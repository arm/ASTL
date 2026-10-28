// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

package astl

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// Configuration is cached by the native library; each case needs a fresh process.
func TestConfigurationSources(t *testing.T) {
	if mode := os.Getenv("ASTL_TEST_CONFIG_MODE"); mode != "" {
		_, err := GetTargets()
		if mode == "default" {
			if err != nil {
				t.Fatalf("default config discovery: %v", err)
			}
		} else {
			status, ok := err.(Error)
			if !ok || status.Status != StatusBadConfiguration {
				t.Fatalf("invalid explicit config must not fall back: got %v", err)
			}
		}
		return
	}
	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	for _, mode := range []string{"default", "replacement"} {
		t.Run(mode, func(t *testing.T) {
			command := exec.Command(executable, "-test.run=^TestConfigurationSources$")
			for _, entry := range os.Environ() {
				name := strings.SplitN(entry, "=", 2)[0]
				if name != "ASTL_CONFIG_DIR" && name != "ASTL_TEST_CONFIG_MODE" {
					command.Env = append(command.Env, entry)
				}
			}
			command.Env = append(command.Env, "ASTL_TEST_CONFIG_MODE="+mode)
			missing := filepath.Join(t.TempDir(), "missing")
			if mode == "replacement" {
				command.Env = append(command.Env, "ASTL_CONFIG_DIR="+missing)
			}
			if output, err := command.CombinedOutput(); err != nil {
				t.Fatalf("%v: %s", err, output)
			}
		})
	}
}
