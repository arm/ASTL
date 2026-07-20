<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

This config dir is a shallow version of top-level config. It's meant only to keep the astl::AstlConfiguration::CreateConfig() function from returning errors.
Unit tests should direct ASTL_CONFIG_DIR environment variable to this path, unless specific metric specification files are needed.


