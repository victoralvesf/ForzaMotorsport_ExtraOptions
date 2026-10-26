---
name: release
description: Release a new version of Azimuth Motorsport - bump the last published tag (major, minor or fix), move the CHANGELOG's unreleased items into the new version, commit, tag and push (CI builds and publishes the GitHub release).
argument-hint: "[major|minor|fix]"
disable-model-invocation: true
---

# Release a new version

Argument: `$ARGUMENTS` (one of `major`, `minor`, `fix`; `patch` is accepted as an alias of `fix`).

Run every step from the repository root (`E:\Azimuth For FM5`), using the Bash tool. Stop and report to the user
at the first failure; never force anything (`--force`, amend, deleting/moving tags).

## 0. Preconditions

- `git status --porcelain` must be empty and the current branch must be `main`. Otherwise stop and tell the user.
- `git fetch origin --tags` and check that `main` is not behind `origin/main`
  (`git rev-list --count main..origin/main` must be 0). If it is behind, stop and tell the user.

## 1. Find the last published version

- Last tag: `git tag --list 'v*' --sort=-v:refname | head -1` (tags are `vMAJOR.MINOR.PATCH`, e.g. `v1.4.1`).
- If there is no tag, the base is `v0.0.0`.
- Show the user the last version and the commits since it (`git log --oneline <last>..HEAD`). If there are no
  commits since the last tag, stop: there is nothing to release.

## 2. Compute the new version

- `major` -> `X+1.0.0`; `minor` -> `X.Y+1.0`; `fix`/`patch` -> `X.Y.Z+1`.
- If `$ARGUMENTS` is empty, ask the user with AskUserQuestion which type of version it is (`major`, `minor`,
  `fix`), showing the resulting version number in each option's description.
- If the argument is anything else, stop and say the valid values.
- Make sure the new tag does not exist yet (`git rev-parse -q --verify refs/tags/vNEW` must fail).

## 3. Update `CHANGELOG.md`

- Find the pending section: a `## ` heading named `Unreleased` or `Next version` (case-insensitive, with or
  without brackets, e.g. `## [Unreleased]`). It ends at the next `## ` heading.
- If there is no such section, or it has no items, tell the user the changelog has nothing pending and ask
  whether to release anyway (if yes, add a short section for the version based on the commits since the last
  tag, following the style of the previous entries) or abort.
- Rename that heading to `## vNEW — <short title>`, following the style of the previous releases
  (e.g. `## v1.4.1 — FM6 tuning setup name fix`, `## v1.3.0 — FM6 shadows, smooth edges and stability fixes`):
  a few words summarizing the main changes, game prefixed where it helps. Keep the section's content
  (`### Improvements`, `### Fixes`, ...) exactly as it is; drop empty subsections.
- Do not leave a new empty `Unreleased` section behind (the previous releases don't keep one).
- Keep the section in its place (right after `## Current support` and before the previous version). Change
  nothing else in the file.
- Show the user the resulting heading.

## 4. Commit

```sh
git add CHANGELOG.md
git commit -m "docs: release vNEW in the changelog" -m "Co-Authored-By: <attribution line from the system reminder>"
```

Use the commit attribution line given in the conversation's system reminder (if any). Commit only
`CHANGELOG.md`.

## 5. Tag and push

```sh
git tag -a vNEW -m "vNEW"
git push origin main
git push origin vNEW
```

The tag push triggers `.github/workflows/ci.yml`, which runs the tests, builds `winmm.dll` and publishes the
GitHub release with `azimuth-motorsport-vNEW.zip`. Only `main` and the tag are pushed (never `historico-local`).

## 6. Report

Tell the user: previous -> new version, the changelog heading, the commit hash, and that the release workflow
started (`gh run list --workflow ci.yml --limit 1` shows it; the release appears at
https://github.com/victoralvesf/azimuth-motorsport/releases/tag/vNEW once CI finishes).
