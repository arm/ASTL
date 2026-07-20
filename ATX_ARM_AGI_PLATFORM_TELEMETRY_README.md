<!--
SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ATX ARM_AGI_CPU Telemetry Boot Setup

ATX has been validated on an ARM_AGI_CPU CRB with the following configuration:

- **OS**: Ubuntu 24.04.4 LTS (Noble Numbat)
- **Firmware**: ARM_AGI_CPU firmware `0.5.2`
- **Kernel**: `7.0.3-arm-agi-cpu-20260519-0.4.1-gf6d5b6032322` (`aarch64`)

  ```text
  Linux ubuntu 7.0.3-arm-agi-cpu-20260519-0.4.1-gf6d5b6032322 #2 SMP PREEMPT Tue May 19 15:07:00 UTC 2026 aarch64 aarch64 aarch64 GNU/Linux
  ```

**This build works exclusively on the `0.5.2` firmware and should not be used on
earlier or later versions of the firmware.**

> **Note:** This guide and the ATX user guide included in this package are from
> a development branch and are not part of an official release. An official
> version will be available after the formal release. For the latest information
> and source, see the [ASTL GitHub repository](https://github.com/Arm-Debug/ASTL).

> **⚠ Warning:** The metric counter values reported by this build have **not been
> validated**. Validation of the counters against the ARM_AGI_CPU hardware
> specification will be carried out by the SLC team at a later time. ASTL and
> ATX are reporting data as reported by the firmware.

## Dependencies

The `atx` binary in this package is prebuilt, so the items below are **runtime**
prerequisites. For the full build-time dependency list, see the
[ATX README](tools/ATX/README.md).

### Required

- **GLIBC 2.38 or newer**: The prebuilt `atx` binary links against GLIBC 2.38.
  Verify the system version with:

  ```sh
  ldd --version
  ```

### Optional

- **gnuplot**: Required only for the plotting report formats (terminal, PNG, and
  SVG plots). Metric collection and CSV/summary output work without it.

  ```sh
  # Ubuntu/Debian
  sudo apt install gnuplot
  ```

- **lm-sensors / libsensors**: Required for the libsensors metrics listed below
  (`nic-temp`, `nic-temp-limit`, `nic-temp-alarm`). Without it, only the SCMI and
  procfs metrics are available.

  ```sh
  # Ubuntu/Debian
  sudo apt install lm-sensors
  ```

ARM_AGI_CPU telemetry needs a one-time setup after each boot before ASTL can read
SCMI telemetry. The setup script automatically uses the SCMI ioctl interface when
it is available and falls back to the legacy `stlmfs` sysfs interface. Until
platform startup handles it, run `setup_arm_agi_cpu_telemetry.sh` manually after
boot (see [Command](#command)).

## When to Run

Run this once after boot and before starting ASTL telemetry collection. Rerun it
if the telemetry device disappears, the driver is reloaded, or descriptors are
reset.

## Command

Run the setup script with privileges to load the telemetry driver and prepare the
selected interface:

```sh
sudo ./scripts/setup_arm_agi_cpu_telemetry.sh
```

## What the Script Does

The script:

1. Loads the `scmi_system_telemetry` driver.
2. Uses `/dev/scmi/tlm_*` ioctl devices when they are available.
3. Otherwise mounts `stlmfs` at `/sys/fs/arm_telemetry` and enables all
   descriptors for each present telemetry instance.
4. Applies permissive device or filesystem access for bring-up.

Pass `--interface ioctl` or `--interface sysfs` only when automatic selection
must be overridden.

A successful run ends with:

```text
ARM AGI CPU telemetry boot setup complete (<interface> interface).
```

## Firmware-Specific Workarounds

The following changes were made to enable ASTL on ARM_AGI_CPU firmware `0.5.2`.
They may not apply to other firmware versions and should be revisited when the
firmware is updated.

### SCP SCMI Descriptor Layout Adjustment

On this firmware, per-core temperature (`TEMP_PRESENT`) and frequency
(`FREQUENCY_PRESENT`) metrics are served through the **SCP** SCMI telemetry
descriptor rather than exclusively through the LCP cluster descriptor.
`scp_patched_fw_0_5_2.json` provides a 70-core `CORE` component block per SCP
telemetry instance with both metrics to match the actual descriptor layout.
Correspondingly, `scp_metrics_patched_fw_0_5_2.json` adds the
`FREQUENCY_PRESENT` metric definition for the SCP channel. The default
`scp.json` and `scp_metrics.json` files remain unchanged.

These changes reflect the actual telemetry layout of this firmware and may differ
from the upstream specification files. They should be reverted once SCMI driver
and firmware updates are available that correctly reflect the intended JSON layout.

### Voltage Rail Descriptor and Alias Mapping

The `VOLTAGE_RAIL` component in `scp_patched_fw_0_5_2.json` matches the rails
actually exposed by this firmware, enabling the per-rail voltage metrics
(`vsoc-c-*`, `vsys-c-*`, `vcpu-c-*`, `vcpu-m-c-*`). The mapping reflects the
actual telemetry layout of this firmware and should be revisited once SCMI driver
and firmware updates align the descriptor with the SCP JSON specification.

## Known Issues

While testing ASTL with ARM_AGI_CPU firmware `0.5.2`, two intermittent
configuration errors were observed:

- `ETIMEDOUT` (`errno=110`, `Connection timed out`)
- `ENOBUFS` (`errno=105`, `No buffer space available`)

If this happens, ASTL collection may complete without data. Reboot before
retrying the setup and ASTL collection.

## Available SCMI Metrics

Pass any of these names to the `--metric` option of `atx`. The main metric
selector (e.g. `core-temp`) collects all discovered instances; use the indexed
form (e.g. `core0-temp`) to target specific instances.

| Metric             | Description                                                                                               | Unit |
| ------------------ | --------------------------------------------------------------------------------------------------------- | ---- |
| `core-temp`        | Core temperature for all discovered cores. Use `coreN-temp` (N=0–139) for individual cores.               | °C   |
| `core-frequency`   | Core frequency for all discovered cores. Use `coreN-frequency` (N=0–139) for individual cores.            | MHz  |
| `chiplet-temp`     | Chiplet temperature for all discovered chiplets. Use `chipletN-temp` (N=0–1) for individual chiplets.     | °C   |
| `d2d-temp`         | D2D temperature for all discovered D2D instances. Use `d2dN-temp` (N=0–15) for individual instances.      | °C   |
| `pss-temp`         | PSS temperature for all discovered PSS instances. Use `pssN-temp` (N=0–5) for individual instances.       | °C   |
| `vsoc-c-temp`      | VSOC_C temperature for all discovered instances. Use `vsoc-cN-temp` (N=0–1) for individual instances.     | °C   |
| `vsoc-c-current`   | VSOC_C current for all discovered instances. Use `vsoc-cN-current` (N=0–1) for individual instances.      | A    |
| `vsoc-c-voltage`   | VSOC_C voltage for all discovered instances. Use `vsoc-cN-voltage` (N=0–1) for individual instances.      | V    |
| `vsoc-c-power`     | VSOC_C power for all discovered instances. Use `vsoc-cN-power` (N=0–1) for individual instances.          | W    |
| `vsys-c-temp`      | VSYS_C temperature for all discovered instances. Use `vsys-cN-temp` (N=0–1) for individual instances.     | °C   |
| `vsys-c-current`   | VSYS_C current for all discovered instances. Use `vsys-cN-current` (N=0–1) for individual instances.      | A    |
| `vsys-c-voltage`   | VSYS_C voltage for all discovered instances. Use `vsys-cN-voltage` (N=0–1) for individual instances.      | V    |
| `vsys-c-power`     | VSYS_C power for all discovered instances. Use `vsys-cN-power` (N=0–1) for individual instances.          | W    |
| `vcpu-c-temp`      | VCPU_C temperature for all discovered instances. Use `vcpu-cN-temp` (N=0–1) for individual instances.     | °C   |
| `vcpu-c-current`   | VCPU_C current for all discovered instances. Use `vcpu-cN-current` (N=0–1) for individual instances.      | A    |
| `vcpu-c-voltage`   | VCPU_C voltage for all discovered instances. Use `vcpu-cN-voltage` (N=0–1) for individual instances.      | V    |
| `vcpu-c-power`     | VCPU_C power for all discovered instances. Use `vcpu-cN-power` (N=0–1) for individual instances.          | W    |
| `vcpu-m-c-temp`    | VCPU_M_C temperature for all discovered instances. Use `vcpu-m-cN-temp` (N=0–1) for individual instances. | °C   |
| `vcpu-m-c-current` | VCPU_M_C current for all discovered instances. Use `vcpu-m-cN-current` (N=0–1) for individual instances.  | A    |
| `vcpu-m-c-voltage` | VCPU_M_C voltage for all discovered instances. Use `vcpu-m-cN-voltage` (N=0–1) for individual instances.  | V    |
| `vcpu-m-c-power`   | VCPU_M_C power for all discovered instances. Use `vcpu-m-cN-power` (N=0–1) for individual instances.      | W    |

## Available Libsensors Metrics

These metrics are read from hardware sensors via the `libsensors` interface. The
available sensors may vary depending on your board hardware.

| Metric           | Description                                                           | Unit |
| ---------------- | --------------------------------------------------------------------- | ---- |
| `nic-temp`       | NIC (`bnxt_en`) temperatures for both NICs.                           | °C   |
| `nic-temp-limit` | NIC temperature thresholds (high, critical, emergency) for both NICs. | °C   |
| `nic-temp-alarm` | NIC thermal alarms for both NICs.                                     | —    |

## Available Procfs Metrics

These metrics are collected from `/proc` and are available on any Linux system.

| Metric                | Description                                                                             | Unit |
| --------------------- | --------------------------------------------------------------------------------------- | ---- |
| `avg-cpu-utilization` | Average CPU utilization across all cores.                                               | %    |
| `cpu-utilization`     | Per-CPU utilization for all discovered CPUs. Use `cpuN-utilization` for a specific CPU. | %    |
| `avg-mem-utilization` | Average memory utilization.                                                             | %    |
