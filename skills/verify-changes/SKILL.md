---
name: verify-changes
description: Validate implemented changes with builds, focused tests, regression tests, static checks, and realistic smoke tests. Use after code changes, before handoff, or when the user asks whether an implementation actually works.
---

# Verify Changes

1. Identify the changed behavior and its highest-risk failure modes.
2. Read the project build and test instructions before choosing commands.
3. Run the narrowest relevant test first, then broader tests proportional to risk.
4. Add or improve a regression test when behavior changed and coverage is missing.
5. Exercise the user-facing path with a realistic smoke test when feasible.
6. Separate passing evidence, skipped checks, warnings, and unverified external dependencies.
7. Report exact commands and outcomes concisely.

Do not claim success from compilation alone when runtime behavior changed.
