# Coverity URL commit-msg Hook

This directory contains a commit-msg hook script that automatically:

- adds Coverity query URLs to commit messages when Coverity IDs (CIDs) are mentioned.
  (TODO - later extend to format, do cppcheck (clang-tidy might be too lengthy))

## Overview

### add_coverity_url.py

When you commit code that fixes Coverity issues and mention the CIDs in your commit message, this hook will automatically generate and insert a clickable URL that links directly to those defects in Coverity.

## Installation

To install the hooks, run:

```bash
./scripts/hooks/install_hooks.sh
```

This will create a `commit-msg` hook in your `.git/hooks/` directory.

## Usage

Once installed, the hook works automatically. Just write your commit message as usual, mentioning CIDs:

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
```
