# ASTL Go Wrapper

This directory provides a Go wrapper for the Arm SoC Telemetry Library (ASTL),
similar in spirit to the Python wrapper under [`python/`](../python/).

## Experimental Status

This Go interface is experimental.

The current wrapper is intended for early adoption and evaluation. Its API
surface, packaging details, and supported-platform story may still evolve as
the binding matures, so it should not yet be treated as a long-term stable Go
SDK.

The wrapper is implemented with cgo and talks directly to the public ASTL C API
from [`include/astl/`](../include/astl/).

## Layout

- `astl/`: Go package exposing ASTL discovery, configuration, collection, and
  sample-retrieval APIs
- `examples/basic/`: small end-to-end enumeration example

## Prerequisites

The Go package links against the ASTL shared library built from this repo. The
current cgo setup expects the debug build layout:

- headers from `../include/`
- generated headers from `../build/debug/include/`
- library from `../build/debug/lib/libastl-0d.so`

Build ASTL first from the repo root:

```bash
just build
```

## Running Tests

From the repo root:

```bash
cd Go
go test ./...
```

A `go.mod` file is included in the `Go/` directory to enable module mode. Ensure
you're using Go 1.11 or later, which supports modules by default.

These tests are lightweight smoke tests today. They are mainly there to catch
linking, cgo, and basic-discovery regressions while the Go wrapper remains
experimental.

## Minimal Example

```go
package main

import (
	"fmt"
	"log"

	"github.com/Arm-Debug/ASTL/Go/astl"
)

func main() {
	fmt.Println("ASTL version:", astl.VersionString())

	targets, err := astl.GetTargets()
	if err != nil {
		log.Fatal(err)
	}

	fmt.Printf("targets discovered: %d\n", len(targets))
	for _, target := range targets {
		fmt.Printf("- %s: %s\n", target.Name, target.Description)
	}
}
```
