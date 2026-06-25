// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"fmt"
	"log"

	"github.com/Arm-Debug/ASTL/Go/astl"
)

func main() {
	fmt.Println("ASTL version:", astl.VersionString())

	systemInfo, err := astl.GetSystemInfo()
	if err != nil {
		log.Fatal(err)
	}

	fmt.Printf("system: %s (%s)\n", systemInfo.SoCName, systemInfo.Architecture)

	targets, err := astl.GetTargets()
	if err != nil {
		log.Fatal(err)
	}

	fmt.Printf("targets discovered: %d\n", len(targets))
	for _, target := range targets {
		fmt.Printf("- %s: %s\n", target.Name, target.Description)
	}
}
