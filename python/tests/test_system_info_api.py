# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import astl


def test_get_system_info_api_exists_and_returns_mapping():
    assert hasattr(astl, "get_system_info")
    assert callable(astl.get_system_info)

    info = astl.get_system_info()
    assert isinstance(info, dict)

    expected_keys = {
        "flags",
        "soc_name",
        "vendor_id",
        "os_name",
        "kernel_name",
        "kernel_version",
        "kernel_release",
        "firmware_version",
        "hostname",
        "architecture",
        "cpu_type",
        "cpu_features",
        "cache_info",
        "core_count",
        "numa_node_count",
        "socket_count",
        "cache_line_size_bytes",
        "memory_total_bytes",
        "libc_version",
        "boot_info",
        "huge_pages_total",
        "huge_page_size_kb",
        "transparent_huge_pages",
    }
    assert set(info.keys()) == expected_keys
