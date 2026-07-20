<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Git Hooks

This directory contains hook scripts that automatically:

- formats code before a commit is finalized and re-stages any staged files that changed.
- runs license lint checks before a commit is finalized.

## Overview

### pre-commit formatting and license lint

The installed `pre-commit` hook invokes `scripts/format.sh` before each commit. If formatting changes any file that was already staged, the hook stages the updated version again automatically. After formatting, the hook invokes `scripts/license_lint.sh`. If either step fails, the commit is blocked.

## Installation

To install the hooks, run:

```bash
./scripts/hooks/install_hooks.sh
```

This will create a `pre-commit` hook in your `.git/hooks/` directory.

## Usage

Once installed, the hooks work automatically:

- `pre-commit`: runs `scripts/format.sh`, re-stages any staged files it changed, then runs `scripts/license_lint.sh`.

## Uninstallation

To remove the hook:

```bash
rm .git/hooks/pre-commit
```
