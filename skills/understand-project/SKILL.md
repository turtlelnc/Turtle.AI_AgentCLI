---
name: understand-project
description: Map an unfamiliar repository, its architecture, build system, entry points, data flow, conventions, and current gaps. Use before broad feature work, when locating an implementation, or when the user asks how the project works.
---

# Understand Project

1. Read repository guidance and top-level documentation.
2. Inventory build files, source roots, tests, configuration, and executable entry points.
3. Trace the requested behavior from input through orchestration to side effects and output.
4. Prefer targeted searches over reading every file.
5. Note generated, vendored, reference-only, and production code separately.
6. Summarize the architecture around the user's goal, not the entire repository.
7. Identify concrete extension points, constraints, and unknowns before proposing changes.

Do not modify code during orientation unless the user explicitly requests implementation.
