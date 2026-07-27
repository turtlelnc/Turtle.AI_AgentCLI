---
name: git-workflow
description: Inspect Git state, review diffs, organize commits, and prepare safe branch or pull-request workflows. Use for status summaries, commit planning, conflict guidance, or Git operations; require explicit user intent before destructive or publishing actions.
---

# Git Workflow

1. Inspect status, branch, remotes, and the scoped diff before proposing mutations.
2. Preserve unrelated user changes and untracked files.
3. Group changes into coherent commits with concise imperative messages.
4. Review staged content before committing.
5. Treat reset, checkout-discard, clean, force push, and history rewriting as destructive.
6. Do not push, publish, or open a pull request unless the user requests it.
7. When conflicts exist, explain both sides and verify the resolved result.
8. Summarize the final branch state and any remaining changes.
