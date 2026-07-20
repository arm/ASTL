# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

$ErrorActionPreference = "Stop"

$VcpkgDir = $env:VCPKG_DIR
if (-not $VcpkgDir) { $VcpkgDir = "external/vcpkg" }

$VcpkgRepo = "https://github.com/microsoft/vcpkg.git"
$VcpkgJson = $env:VCPKG_JSON
if (-not $VcpkgJson) { $VcpkgJson = "vcpkg.json" }

if (-not (Test-Path "$VcpkgDir/.git")) {
    Write-Host "Cloning vcpkg into $VcpkgDir"
    git clone $VcpkgRepo $VcpkgDir
}

$BuiltinBaseline = (Get-Content $VcpkgJson | ConvertFrom-Json).'builtin-baseline'

Write-Host "Checking out vcpkg commit $BuiltinBaseline"
Push-Location $VcpkgDir
git fetch --quiet
git checkout $BuiltinBaseline
Pop-Location

Write-Host "Bootstrapping vcpkg..."
& "$VcpkgDir/bootstrap-vcpkg.bat"

Write-Host "✅ vcpkg is ready"