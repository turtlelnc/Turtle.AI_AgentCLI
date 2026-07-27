---
name: debug-issue
description: Reproduce and diagnose software failures, crashes, incorrect output, build errors, API errors, and flaky behavior. Use when the user reports a bug or asks for root-cause analysis, with implementation only when explicitly requested.
---

# Debug Issue

1. Restate the observed and expected behavior in testable terms.
2. Reproduce with the smallest safe command or fixture available.
3. Capture the exact error, environment, inputs, and relevant state.
4. Follow evidence from the failing boundary toward the root cause; avoid guessing fixes first.
5. Check adjacent paths for the same defect pattern.
6. Explain the root cause and confidence level.
7. If a fix is requested, implement the smallest coherent change and add a regression test.
8. Re-run the reproduction and relevant tests after the change.

Never hide a failed reproduction. State what was and was not verified.
