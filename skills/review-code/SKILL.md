---
name: review-code
description: Review code changes for correctness, regressions, security risks, maintainability, and missing tests. Use for pull-request reviews, working-tree reviews, patch inspection, or requests to find bugs without implementing unrelated changes.
---

# Review Code

1. Inspect repository guidance and the exact diff or files in scope.
2. Trace changed behavior through callers, error paths, and provider/platform variants.
3. Run focused read-only checks when they materially increase confidence.
4. Report findings before summaries, ordered by severity.
5. For every finding, name the file/location, concrete failure mode, and user impact.
6. Distinguish confirmed defects from questions or speculative risks.
7. If no actionable defect is found, say so and list residual test gaps.

Do not modify files unless the user also asks for fixes.
