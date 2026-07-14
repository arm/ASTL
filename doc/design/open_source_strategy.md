<!--
SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Open-Source Split Strategy

## Summary

Use a **clean internal canonical repo** for day-to-day development, and mirror that repo exactly to the public repo. Keep all confidential workflows, secrets, hardware tests, NDA fixtures, and internal release logic in a separate **private CI/control repo** or **private wrapper repo**. Do not directly mirror the existing contaminated repo or its history.

## Recommended Architecture

- Rewrite ASTL history to create a new public-safe history. Day-to-day work proceeds in `Arm-Debug/ASTL`, with Github App to trigger CI from `Arm-Debug/ASTL-confidential`
- The canonical repo contains only code, docs, tests, configs, and public-safe `.github` files intended to appear in open source. (see current .ossmosis.json)
- Mirror canonical `main` and release branches to the public GitHub repo using a simple exact mirror.
- Move confidential workflows like BlackDuck, Coverity, internal release, self-hosted hardware tests, AI review, and private dispatchers out of the mirrored repo.
- Store confidential CI definitions in a private `Arm-Debug/ASTL-confidential` repo, not in `.github/workflows` of the mirrored repo.

## Confidential CI On Internal PRs

- Configure a GitHub App, webhook service, or private CI controller to listen for PR events on the internal canonical repo.
- On each internal PR, the private controller checks out the PR head SHA, overlays or fetches private-only assets, and runs confidential workflows on private/self-hosted runners.
- The controller reports results back to the PR as GitHub commit statuses or Checks API results, so branch protection can require them.
- Keep secrets only in the private CI/control repo or runner environment, never in the mirrored source repo.
- Avoid `pull_request_target` for untrusted public contributions unless the job never checks out or executes contributor code with secrets.

## Handling Existing History

- Treat the current repo as contaminated. Use `ossmosis export-history` and `ossmosis scan-history` to create a new history. force push that.
- Remove confidential paths, filenames, commit messages, tags, branches, refs, artifacts, and generated files.
- Preserve attribution where possible, but prefer a clean initial import if history review cannot be completed confidently.

## Repo Split

- Public/canonical repo: library, public headers, samples, public tests, public docs, public CI.
- Private wrapper repo: NDA configs, confidential platform data, internal test fixtures, internal release scripts, private runner workflows.
- Private CI repo: GitHub Actions workflows or CI orchestration that must never be mirrored.
- If private tests need public code, they should checkout the PR SHA from the canonical repo and then layer private inputs beside it.

## Assumptions

- Recommended default: developers PR into a private clean canonical repo, not directly into the old contaminated repo.
- Public mirroring should be exact only after the canonical repo is clean.
- Internal confidential CI results need to appear as required PR checks on the internal repo.

## Action Items

### Platform_lookup.json and repometa.json

These files include references to all UUIds and product codenames in the SCMI config directory and metrics.
We need to restructure this so the lookup tables are split and we can exclude whole product directories without modifying individual lines of files.

### Make CMakeLists optionally build ATX and turbostat-arm only if present

These projects are going to be NDA at first, so we can't have CMake fall on its face if they're not present

### Remove Coverity

This service is going away for us in Arm, and it has pointers to internal servers, so we should remove all Coverity config and workflows.

### Ossmosis manifest

This should be present in the Arm-Debug/ASTL repo so we can have CI and pre-commit hooks check compliance. but we can't mirror this file directly public.
So we can either : keep it as a (long) repo secret, or add it as an encrypted file and make the key a repo secret. (i recommend the latter, so we can revision control it with review)
Have some automation to deal with encrypting/unencrypting it on checkout / pre-commit?

### Copy ASTL repo to ASTL-confidential, as-is

For starters, so other work can proceed. Later on, we'll remove the public stuff and add scripts to check out the public repo

### Scrub ASTL history with ossmosis to Arm-Debug/ASTL with force-push

### Link CI between Arm-Debug/ASTL adn Arm-Debug/ASTL-confidential

- blackduck
- create-release
- functional-dispatch
- mirror-arm collab
- out-of-box-testing
- integration - need to rework to use github coverage measurement instead of qlty? make quality upload optional?
