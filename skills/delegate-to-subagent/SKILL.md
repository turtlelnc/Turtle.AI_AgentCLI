---
name: delegate-to-subagent
description: Delegate a focused, self-contained task to the configured sub-agent model. Use when a task benefits from an independent context, specialist role, parallelizable investigation, or a second-pass review, and a sub-agent is configured.
---

# Delegate to Sub-agent

1. Keep ownership of the user's overall goal in the main agent.
2. Delegate one bounded task with a concrete deliverable.
3. Pass only the context needed to complete that task.
4. Set `role` when a specialist viewpoint such as reviewer, debugger, or researcher helps.
5. Use `delegate_task` once per independent task; never ask a sub-agent to delegate again.
6. Treat the returned response as evidence, not as automatically correct.
7. Reconcile the result with the main conversation and report one coherent answer.
8. If delegation is unavailable, continue in the main agent without inventing a result.
