#!/bin/env bash
set -eu -o pipefail
find . -type f | sort -u
