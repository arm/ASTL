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

The Go package uses cgo and expects ASTL's public headers plus a built ASTL
shared library to be available.

For a repo-local debug build:

```bash
just build
export CGO_LDFLAGS="-L$PWD/build/debug/lib -Wl,-rpath,$PWD/build/debug/lib -lastl-0d"
export LD_LIBRARY_PATH="$PWD/build/debug/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

The Go wrapper already adds repo-local ASTL header search paths via cgo, so
only the linker path needs to be provided for the default in-repo workflow.

For an installed ASTL, point cgo directly at the install prefix:

````bash
export CGO_CFLAGS="-I/path/to/prefix/include"
```bash
export CGO_LDFLAGS="-L/path/to/prefix/lib -Wl,-rpath,/path/to/prefix/lib -lastl-0d"
````

````

## Running Tests

From the repo root:

```bash
export CGO_LDFLAGS="-L$PWD/build/debug/lib -Wl,-rpath,$PWD/build/debug/lib -lastl-0d"
export LD_LIBRARY_PATH="$PWD/build/debug/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd Go
go test ./...
````

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
