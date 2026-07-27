#ifndef HOOKS_HPP
#define HOOKS_HPP

#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

/// Lifecycle points where hooks may fire.
enum class HookEvent {
    SessionStart,
    SessionEnd,
    PreToolExecution,
    PostToolExecution,
    PreModelRequest,
    PostModelResponse,
    TurnStart,
    TurnEnd,
    ApprovalRequested,
    FileChanged
};

/// Result of a hook invocation — whether to proceed, block, or modify.
struct HookResult {
    bool allow = true;               // false blocks the operation
    std::string reason;              // human-readable reason for blocking
    nlohmann::json modifications;    // optional payload modifications
};

/// A registered hook with its metadata.
struct Hook {
    std::string name;
    std::string description;
    HookEvent event;
    std::function<HookResult(const nlohmann::json& context)> handler;
    std::chrono::milliseconds timeout{5000};  // max execution time
    bool requires_approval = false;            // true for destructive hooks
};

/// Hook registry.  Hooks are called in registration order; a blocking
/// hook stops the chain and prevents the operation.
class HookRegistry {
public:
    void registerHook(Hook hook);
    bool unregisterHook(const std::string& name);

    /// Run all hooks for @p event.  Returns false if any hook blocks.
    bool run(HookEvent event, const nlohmann::json& context = {});

    /// List registered hook names for inspection.
    std::vector<std::string> list() const;

    /// Remove all hooks.
    void clear();

private:
    std::vector<Hook> hooks_;
};

// ─── Plugin marketplace threat model (design doc) ──────────────────────
//
// Before any plugin marketplace is implemented, the following must be
// in place:
//
// 1. Code Signing — Every plugin MUST be signed with a developer
//    certificate.  Turtle verifies the signature against a trusted root
//    before loading.  Unsigned plugins are rejected.
//
// 2. Provenance — Plugin metadata includes source repository, maintainer
//    identity, and audit trail.  Users see provenance before install.
//
// 3. Capability Declaration — Plugins declare required permissions
//    (filesystem, network, process execution) in a manifest.  Turtle
//    enforces these at runtime; capability escalation triggers re-auth.
//
// 4. Sandboxing — Plugins run with the same sandbox policy as tools:
//    workspace-only writes, no network by default, process isolation.
//
// 5. Update Verification — Updates MUST be signed by the same developer
//    key.  First-time key changes require explicit user approval.
//    Rollback to previous version must be possible.
//
// 6. Review Process — Marketplace submissions require: (a) automated
//    static analysis, (b) capability justification, (c) human review
//    for elevated-capability plugins.
//
// 7. Revocation — Compromised plugins can be remotely revoked.
//    Turtle checks a revocation list on startup (with offline grace
//    period).

// ─── Remote execution environment evaluation ──────────────────────────
//
// Prerequisites (all now satisfied):
//   ✅ Local sandbox backend (macOS Seatbelt + Linux bwrap)
//   ✅ Stable JSONL event protocol (Thread/Turn/Item)
//   ✅ Structured AgentSession with cancellation
//   ✅ Provider abstraction and tool registry
//
// Recommended approach (not yet implemented):
//   1. SSH-based remote executor — simplest, works with existing sandboxes
//   2. Container-based (Docker/Podman) — better isolation, reproducible
//   3. Cloud VM ephemeral — strongest isolation, highest cost
//
// For all approaches: the remote side runs a thin agent that receives
// JSONL events over a secure channel, executes sandboxed commands, and
// streams results back.  The local Turtle CLI remains the control plane.
// Network transit must be encrypted (TLS/SSH) and both sides must
// verify peer identity before accepting commands.

// ─── JSONL protocol for SDK consumers ────────────────────────────────
//
// The Turtle event stream uses newline-delimited JSON (JSONL).  Each
// line is a self-contained event object with these common fields:
//
//   {"type":"<event_type>","thread_id":"<uuid>","turn_id":"<uuid>",
//    "payload":{...}}
//
// Event types and their payloads:
//
//   thread_created  — {title, provider, model, workspace, created_at,
//                       forked_from?}
//   turn            — {id, state, user_input, assistant_content,
//                       tool_requests[], tool_results[], file_changes[],
//                       usage{input_tokens,output_tokens,cost_usd},
//                       provider, model, started_at, completed_at,
//                       error_message}
//
// Consumers (TypeScript/Python SDKs) can:
//   1. Read JSONL from `turtle exec --json` stdout line by line
//   2. Parse each line as a JSON object
//   3. Dispatch on `type` to update UI / log events / trigger callbacks
//
// Thread files (.turtle/sessions/<id>.jsonl) use the same format,
// readable by any JSONL-capable tool.

} // namespace opencode

#endif
