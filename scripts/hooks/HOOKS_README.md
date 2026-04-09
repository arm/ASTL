<!--
SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Git Hooks

This directory contains hook scripts that automatically:

- adds Coverity query URLs to commit messages when Coverity IDs (CIDs) are mentioned.
- formats code before a commit is finalized and re-stages any staged files that changed.
- runs license lint checks before a commit is finalized.

## Overview

### add_coverity_url.py

When you commit code that fixes Coverity issues and mention the CIDs in your commit message, this hook will automatically generate and insert a clickable URL that links directly to those defects in Coverity.

### pre-commit formatting and license lint

The installed `pre-commit` hook invokes `scripts/format.sh` before each commit. If formatting changes any file that was already staged, the hook stages the updated version again automatically. After formatting, the hook invokes `scripts/license_lint.sh`. If either step fails, the commit is blocked.

## Installation

To install the hooks, run:

```bash
./scripts/hooks/install_hooks.sh
```

This will create both a `commit-msg` hook and a `pre-commit` hook in your `.git/hooks/` directory.

## Usage

Once installed, the hooks work automatically:

- `pre-commit`: runs `scripts/format.sh`, re-stages any staged files it changed, then runs `scripts/license_lint.sh`.
- `commit-msg`: appends a Coverity URL when CIDs are detected.

For the `commit-msg` hook, write your commit message as usual, mentioning CIDs:

### Example 1: CIDs on separate lines

```markdown
[ASTL-342] Fix some Coverity issues

CID:
123412
123532
122143

Fixed memory leaks
```

The hook will automatically transform this to:

```markdown
[ASTL-342] Fix some Coverity issues

CID:
123412
123532
122143
[View in Coverity](https://coverity.geo.arm.com/query/defects.htm?cid=123412&cid=123532&cid=122143&stream=ASTL-main)

Fixed memory leaks
```

### Example 2: CID with number on same line

```markdown
[ASTL-343] Resolve coverity problem

CID: 5678123

Fixed the issue
```

Becomes:

```markdown
[ASTL-343] Resolve coverity problem

CID: 5678123
[View in Coverity](https://coverity.geo.arm.com/query/defects.htm?cid=5678123&stream=ASTL-main)

Fixed the issue
```

### Example 3: Comma-separated CIDs

```markdown
[ASTL-400] Fix multiple issues

CID: 111, 222, 333

All resolved
```

Becomes:

```markdown
[ASTL-400] Fix multiple issues

CID: 111, 222, 333
[View in Coverity](https://coverity.geo.arm.com/query/defects.htm?cid=111&cid=222&cid=333&stream=ASTL-main)

All resolved
```

## Uninstallation

To remove the hook:

```bash
rm .git/hooks/commit-msg
rm .git/hooks/pre-commit
```
