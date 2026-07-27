---
name: refactor-safely
description: Improve code structure while preserving externally observable behavior. Use for extraction, deduplication, API cleanup, modularization, dependency reduction, or maintainability work where regression control matters.
---

# Refactor Safely

1. Define the behavior and interfaces that must remain unchanged.
2. Establish focused tests or characterization checks before structural edits.
3. Choose the smallest boundary that removes the identified design pressure.
4. Keep policy, I/O, parsing, and presentation concerns separate where practical.
5. Avoid unrelated renames and formatting churn.
6. Make incremental changes that continue to compile and test.
7. Compare behavior and relevant output before and after.
8. Report structural improvement, compatibility impact, and remaining debt.

If behavior must change, call it out explicitly instead of labeling the work a pure refactor.
