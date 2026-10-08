# Djinn v3 — Verification Ideas & Improvements

**Status:** Companion catalog to `VERIFICATION-SPEC.md`. Nothing here supersedes the spec until promoted; every section names the spec sections it extends.
**Source:** Phase 0 audit of this repository + design review (2026-10). Phase 0 of the spec roadmap ("audit current require/ensure + enforcement levels + diagnostics codes") is discharged by §5 of this document.
**Companion scaffolding:** `verification/` (IR types + pass shell), `--verify off|report|trace` (CLI and `compiler.verify` in `djinn.proj`), IR carried in `CompilerResult::verification`, diagnostic range 95xx, `tests/verification/`. The pass shell now includes the value-level slice over the decidable linear fragment (`Fragment`/`Facts`/`BodyAnalyzer`): ensure claim/inference diffing (9502), call-site require checking (9501), obligations with real `proven`/`checked` statuses. On top of it, the whole-flow slice is in: interprocedural postcondition propagation and method/extern-stub call-site checking, the §5.1 semantic surface (modes, sections, invariants, uow) with static claim-vs-inference diffing, uow checks (atomic coverage, retry-safety, placement, ordering, access), and entity invariants proven at method exits — see the delivery notes in VERIFICATION-SPEC.md §15. Everything remains static; generated binaries are unchanged.

---

## 1. Purpose

`VERIFICATION-SPEC.md` defines what the verifier does for Djinn. This document:

1. lifts the language-independent parts into a **portable kernel** — formal artifacts other languages can emit and consume, making the system language-agnostic *by construction* rather than by aspiration (§2);
2. proposes **compile-time business-rule features** beyond function in/out contracts (§3);
3. hardens the **agent loop** — the verifier's actual customer (§4);
4. records **audit findings** from the real compiler that the spec must absorb when promoted (§5);
5. proposes a **roadmap delta** and decisions for promotion into the spec (§6, §7).

The thesis for §2–§4: for an agent, the *diagnostic + obligation stream* is the product. The source language is an implementation detail. Everything an agent (or a CI gate, or another toolchain) needs to participate in the verify-fix loop should be defined once, in a language-neutral format, so Djinn becomes the first implementation of a protocol — not a monolith.

---

## 2. Language-agnostic kernel

### 2.1 Layer split

| Layer | Content | Defined in |
| --- | --- | --- |
| **Kernel** | obligation model, expression fragment, diagnostic envelope, contract manifest | this §2 (normative) |
| **Surface** | grammar (`require`/`ensure`/`uow`/semantic sections), mode syntax | spec §5 |
| **Implementation** | `verification/` pass, generator hooks, diagnostics plumbing | spec §14 |

The kernel has no syntax of its own. A host language (Djinn now; others later) translates its surface constructs into kernel artifacts; consumers (agents, CI, other-language verifiers) read only kernel artifacts.

### 2.2 Obligation model (normative)

One obligation = one checkable claim with its discharge status. Every `require`, `ensure`, `invariant`, semantic section claim, and strategy rule becomes an obligation.

```json
{
  "id": "ob-0042",
  "kind": "require",
  "mode": "prove",
  "status": "proven",
  "subject": "payments::uow/Transfer",
  "expression": { "t": "bin", "op": ">=",
                  "lhs": { "t": "field", "base": { "t": "param", "name": "from" }, "name": "balance" },
                  "rhs": { "t": "param", "name": "amount" } },
  "justification": null,
  "evidence": { "declared": null, "inferred": null },
  "source": { "language": "djinn", "file": "payments/account.djinn", "line": 31, "column": 5 },
  "resolutions": []
}
```

| Field | Rules |
| --- | --- |
| `id` | stable within one manifest; regenerated deterministically per compile |
| `kind` | `require` \| `ensure` \| `invariant` \| `effect-claim` \| `access-claim` \| `atomicity-claim` \| `ordering-claim` \| `retry-claim` \| `strategy-rule` |
| `mode` | `prove` \| `check` \| `assume` \| `ignore` (spec §5.2) |
| `status` | `proven` \| `checked` \| `assumed` \| `unverified` (spec §5.3; `unverified` exists only mid-compile) |
| `subject` | symbol path owning the claim (`ns::fn`, `ns::Struct::method`, `ns::uow/Name`) |
| `expression` | neutral fragment AST (§2.3) or `null` for structural claims (access/atomic/retry) |
| `justification` | mandatory non-empty string when `mode == assume` (§3.6), else `null` |
| `source` | `language`-tagged location; `line`/`column` are 1-based |

### 2.3 Neutral expression fragment (normative)

The decidable fragment of spec §6.3, as a JSON AST. Totality rule: a host either translates a spec-position expression into this fragment or reports a translation failure (→ `E-CONTRACT-046` under `prove`); partial translation is forbidden.

| Node | Shape | Notes |
| --- | --- | --- |
| literal | `{ "t": "num", "v": "42" }` / `{ "t": "real", "v": "1.5" }` / `{ "t": "bool", "v": true }` | decimal strings, not JSON numbers (precision) |
| param | `{ "t": "param", "name": "amount" }` | function/uow parameter by name |
| field | `{ "t": "field", "base": <expr>, "name": "balance" }` | resource paths like `from.balance` |
| old | `{ "t": "old", "inner": <expr> }` | pre-state reference (spec §5.1) |
| pure call | `{ "t": "call", "callee": "ns::fn", "args": [<expr>…] }` | callee must be spec-pure (§5.6) |
| binary | `{ "t": "bin", "op": "+", "lhs": <e>, "rhs": <e> }` | ops: `+ - * / % < <= > >= == != && \|\|` |
| unary | `{ "t": "un", "op": "-", "inner": <e> }` | ops: `- !` |

Exactly quantifier-free linear arithmetic + booleans. Non-goals inherited from spec §6.3: quantifiers, arbitrary calls, uninterpreted theories.

### 2.4 Diagnostic envelope (normative)

The machine-readable diagnostic any language's verifier emits. One shape; the human rendering is the host's business.

```json
{
  "code": "E-CONTRACT-042",
  "severity": "error",
  "message": "uow `Transfer` is retryable; send_transfer_email is external_io, not retry-safe",
  "evidence": {
    "declared": { "retry": "allowed", "strategy": "database" },
    "inferred": { "effects": ["external_io"] },
    "locations": [ { "file": "payments/account.djinn", "line": 31, "column": 9 } ],
    "witness": null
  },
  "resolutions": [
    { "rank": 1, "action": "move-to-after-commit", "target": "send_transfer_email" },
    { "rank": 2, "action": "declare-idempotent", "target": "send_transfer_email" },
    { "rank": 3, "action": "set-retry-disabled" },
    { "rank": 4, "action": "provide-compensation" }
  ],
  "smtlib": null
}
```

`witness` is a counterexample assignment (`{"amount": "-5"}`) when a solver produced one (§4.1). `smtlib` carries the SMT-LIB query for `prove` failures (reproducibility; debuggable proofs). `resolutions` are sorted by `rank` (§4.2).

### 2.5 Contract manifest (normative)

A per-package JSON artifact — the interchange format between *any* producer and *any* consumer. For Djinn it ships inside `.djlib` (the `lib/` reader/writer already carries metadata; this extends it).

```json
{
  "format": "djinn.contracts/1",
  "package": "payments",
  "language": "djinn",
  "symbols": [
    {
      "path": "payments::uow/Transfer",
      "kind": "uow",
      "signature": { "params": ["from", "to", "amount"], "returns": "void", "throws": ["ContractViolation"] },
      "effects": ["database"],
      "reads":  ["from.balance", "to.balance", "from.currency"],
      "writes": ["from.balance", "to.balance", "audit_events"],
      "access": [ { "mode": "exclusive", "resource": "from" }, { "mode": "exclusive", "resource": "to" } ],
      "ordering": [ { "key": "Account.id", "direction": "ascending" } ],
      "retry": "allowed",
      "strategy": "database",
      "obligations": { "proven": 14, "checked": 1, "assumed": 1, "unverified": 0 },
      "specCoverage": { "contracted": true, "effectsAnnotated": true }
    }
  ]
}
```

Consumers:

- **Agent tooling** — the library's contract surface without bodies (spec §12.3 "modular verification", made concrete).
- **CI gates** — obligations summary + coverage + baseline diff (§4.5, §4.6).
- **Other-language verifiers** — cross-language calls checked against the same manifest shape.

### 2.6 Porting requirements

What a host language must provide to join the protocol:

| Requirement | Meaning |
| --- | --- |
| Fragment translator | spec-position AST → §2.3 fragment (or `E-CONTRACT-046`) |
| Effect inference | per-function effect sets (spec §11 vocabulary) |
| Purity checker | decides spec-pure (§5.6 gives Djinn's definition) |
| Manifest emitter | package symbols + claims + obligations summary → §2.5 |
| Location mapper | kernel locations ↔ host source spans |

---

## 3. Compile-time business-rule features

Ranked by value/cost. All of them are **claims checked by the verifier**, not new runtime machinery — they ride the claim-vs-inference model of spec §6.2.

### 3.1 Refinement types (generalizes the existing non-zero mechanism)

The compiler already has the seed of refinements: `require(p != 0)` is *upgraded* into a `nonZero` property on the parameter type (`binder/FunctionBinder.cpp` `apply_non_zero_contract_upgrades`, and `i32n`), propagated to call sites, and checked at the generator. Refinements are the general form:

```djinn
type Positive = i32 where value > 0;
type Age      = i32 where value >= 0 && value < 150;
type Money    = decimal where value >= 0;
```

Semantics:

- **Construction**: literal/implicit conversion into a refinement is checked — proven at compile time when the fragment decides it, injected under `mode check` otherwise (the same mode machinery as spec §5.2).
- **Subtyping**: `R1 <: R2` iff `R1.predicate ⇒ R2.predicate` is provable over the fragment. Assignment/argument passing requires `R_arg <: R_param`.
- **Widening**: refinement → base type is free.
- **Entity invariants unify with refinements**: `invariant balance >= 0` on `Account` is an anonymous refinement on the field; `E-CONTRACT-044` ("invariant not preserved") is then just the assignment check for that refinement. One mechanism, two syntaxes.

This turns a large class of business rules ("amounts are non-negative", "ages are bounded") into type errors that agents inherit for free at every call site — no per-call reasoning required. The non-zero path stays as-is (it predates and initializes this design).

*Extension (later)*: parameter-level refinements reusing the existing `where` syntax — `fn pay(acct: Account & acct.balance > 0)` desugars to a `require`. The type alias form is enough for v1.

### 3.2 Entity state machines

The most business-rule-shaped feature that stays compile-time checkable: legal transitions declared on the state field.

```djinn
entity Order {
    state: OrderState

    transitions {
        created -> paid;
        paid    -> shipped, cancelled;
    }
}
```

- Any assignment to a `transitions`-annotated field is checked against the table: if the source state is statically known on that path (literal assignment history, §3.4 fact tracking), the pair must be a declared edge (`E-CONTRACT-048`, proposed); if unknown, the assignment becomes an obligation under the entity's mode.
- Interacts with UOWs naturally: a UOW whose `writes` include `order.state` must not leave the entity in a non-terminal hole — later refinement; v1 checks assignments only.
- The "business race" of spec §7.2 (check-then-act) has a state-machine special case (act-then-act on `state`); the table gives the verifier ground truth for it.

### 3.3 Module effect policies (sandboxing agent code)

Effects are already planned to be *inferred* (spec §11). A deny/allow policy at namespace level turns that inference into a security boundary — the cheapest high-value feature on this list once inference exists:

```djinn
namespace payments::sandbox {
    effects: deny  [network, process, filesystem]
    effects: allow [memory, random, time]     // optional; when present, inferred ⊆ allow
}
```

- Any function whose inferred effect set intersects `deny` → error (`E-CONTRACT-047`, proposed). Policies apply to *inferred* effects — no annotation needed, so agent code cannot opt out by staying silent.
- Use cases: sandboxing agent-written modules, pure-domain layers, test doubles, restricting what generated code may touch.

### 3.4 Callsite fact-tracking (the missing mechanism behind `E-CONTRACT-007`)

The spec's registry lists "precondition not established at call site" but defines no mechanism. Today's compiler proves `require`s only against *constant* arguments (`Binder::check_compile_time_call`, diagnostic 9007) — constant-args checking is the degenerate case of a general idea:

**Track established facts per path.** At each call, the callee's `require` predicates must be *entailed* by the facts known on the current path:

```djinn
fn handle(req: Request) -> Response {
    if (req.amount <= 0) {
        return reject(422);
    }
    // fact established here: req.amount > 0
    Transfer { from, to, amount: req.amount };   // require(amount > 0): DISCHARGED
}
```

Facts are established by:

- branch guards with early exit (return/throw/continue on the negation);
- the enclosing function's own `require`s (params are entry facts);
- refinement-typed bindings (§3.1 predicates);
- a preceding `check … in mode check` that passed (the fact survives until the variable is reassigned).

Entailment: structural for linear comparisons against literals (covers the vast majority of agent-written guards), SMT for the general fragment (spec §6.3). When entailment cannot be decided, the obligation stays `checked` (runtime guard still injected) — **no error** unless the callsite is explicitly under `prove` (policy or UOW context).

This is the highest-leverage feature for the agent loop: the defensive code agents already write *pays off* by discharging obligations, and `E-CONTRACT-007` becomes real instead of aspirational. v1 structural only; no SMT dependency.

### 3.5 `old()` for functions (not only UOWs)

Spec §5.1 grants `old(expr)` inside `ensure` on UOWs. Extending it to plain functions is cheap and completes `ensure`:

- `old(p)` for parameters = the parameter's entry value. Under `prove` this is the initial symbolic version of `p` (reassignment creates a new version — SSA semantics, no snapshot needed). Under `check`, identical (entry snapshot or direct use for immutable params).
- `old(expr)` over heap fields (e.g. `old(from.balance)`) = value at entry. Under `check` this injects an entry snapshot alloca (reusing the generator's existing contract alloca pattern); under `prove` it is an uninterpreted entry-state symbol.

### 3.6 `assume` justification

Trusted obligations are the audit surface for human review of agent-generated code (spec §5.3 lists `assumed` for "human audit" — but with no justification string there is nothing to audit). Proposal:

```djinn
idempotent(audit.record) in mode assume because "stdlib guarantee (std/audit.djinn)";
```

- `because STRING` is mandatory with `mode assume` (syntax addition to the mode clause).
- The justification flows into the obligation model (§2.2) and the obligations report; the manifest carries it.
- CI can pattern-match justifications (e.g. require an issue/reference link).

---

## 4. Agent-loop hardening

Extensions to spec §12.3 (fix hints → spec-first → modular → obligations).

### 4.1 Counterexamples from solver models

When `prove` fails with `Violated`, the SMT model is the best possible fix hint: the diagnostic carries `evidence.witness` (§2.4), e.g. `{"amount": "-5"}`. Optional `--counterexamples <dir>` additionally emits a failing test skeleton per witness (input that violates the obligation) — agents get a regression anchor, humans get a reproduction. Witness stability across solver versions is a real concern (§8).

### 4.2 Resolution ranking

Every resolution carries a `rank`; lists are sorted. Taxonomy:

| Rank | Class | Example |
| --- | --- | --- |
| 1 | local edit | move a statement, fix an expression, extend a claim set |
| 2 | declaration edit | add a clause, change a mode, annotate idempotent |
| 3 | signature change | introduce a refinement type, add an effects claim |
| 4 | design change | change strategy/propagation, restructure the UOW |

For agents this is iteration-count control: try rank 1 first, escalate only on repeated failure. The JSON envelope (§2.4) makes the ordering contractual.

### 4.3 LSP integration

The repository already ships an LSP (`lsp/`). Verification diagnostics should flow through it on `didOpen`/`didChange` — the editor loop is the lowest-latency verification loop an agent or human has. Requires incremental verification (§4.4) to stay responsive.

### 4.4 Incremental verification

- Obligations keyed by subject symbol path; cache discharge results keyed by (fragment hash + relevant types) (§4.8).
- On edit: re-verify the edited symbol + transitive callers (the binder's call graph supplies reverse edges).
- Report the **diff**: added / removed / discharge-changed obligations — "what did my edit break" is the question agents ask most.

### 4.5 Spec coverage metrics + CI gate

Agents optimize what is measured. `djinn verify --report coverage`:

- `contract_coverage` — % of exported functions with ≥1 `require`/`ensure`
- `invariant_coverage` — % of fields written anywhere covered by an entity invariant or refinement
- `boundary_coverage` — % of exported functions with an explicit `effects` claim

Gate: `--min-spec-coverage 75` exits non-zero below the threshold. Per-symbol coverage lives in the manifest (§2.5). This is what turns "the agent generated compiling code" into "the agent generated verified code".

### 4.6 Baseline diff (contract semver)

`djinn verify --baseline payments.djlib` compares the new manifest against the shipped one:

| Change | Caller-visible breaking? |
| --- | --- |
| `require` added | **yes** (callers may fail to establish it) |
| `require` removed | no (widening) |
| `ensure` weakened/removed | **yes** (callers relied on the postcondition) |
| `ensure` added | no |
| `effects` added | **yes** |
| `access` `shared → exclusive` | **yes**; `exclusive → shared` no |
| `invariant` added | **yes** (writers must discharge) |
| refinement tightened | **yes** |

Exit non-zero on any breaking change → a CI gate like API-compat checkers. For agents: a dependency update yields a precise list of *which* obligations broke and where.

### 4.7 Property-based test generation

From `require` + `ensure` (later): generate inputs satisfying the requires, use ensures (+ `old()`, + entity invariants) as the oracle, shrink on failure. Effect inference (spec §11) excludes non-deterministic bodies unless `random` is seeded. Output lands in `tests/property/`. This bridges ghost proofs and the runtime boundary where unprovable network input crosses `mode check`.

### 4.8 Proof caching

Obligations are pure functions of program text + types → cache keyed by (SMT-LIB hash + relevant symbol types + mode), stored in `.djinn/cache`, invalidated by content hash (never timestamps). The agent loop re-verifies constantly; this keeps it fast.

### 4.9 Determinism and performance budget

- Stable diagnostic order (sort by location) — identical input, identical report, always.
- Budget: structural checks < 50 ms/file; per-obligation SMT timeout (default 1000 ms) → `Unknown` → under `prove` that is `E-CONTRACT-046` (choose `check`/`assume`), never a hang. A verifier that occasionally wedges the loop loses agent trust permanently.

### 4.10 Violation corpus

`tests/verification/corpus/NNN-name/{bug.djinn, expected.json}` — each fixture seeds exactly one business-rule bug; `expected.json` lists the expected code + resolutions. Double duty: regression-tests the verifier, and benchmarks **agent fix-rate** (agent sees only the diagnostic; measure whether it applies a listed resolution correctly). The spec's worked example (§13) is corpus fixture 001.

---

## 5. Audit findings (Phase 0 — to absorb into the spec on promotion)

Findings from reading the actual compiler. These are constraints on the spec, not proposals.

### 5.1 Async bodies skip contract emission (critical)

`generator/GeneratorFunction.cpp` (~line 331): *"contracts are not emitted in async bodies yet (same as explicit require clauses)"* — and non-zero entry checks skip coroutine bodies too. Since async is the language default, **`mode check` is broken by omission for most code**: `require`/`ensure` silently vanish in async bodies, so `ContractViolation` never throws there. The spec's central runtime mechanism ("the only emitted code is the existing ContractViolation guard path", §14) does not exist in coroutine bodies yet. Emitting entry checks at coroutine entry and ensure checks on every return path is a **prerequisite pulled into Phase 2** (or earlier), with `tests/verification/async_contracts.cpp`.

### 5.1a Method bodies cannot call same-namespace free functions (codegen ordering)

The demos exposed a pre-existing codegen gap: struct methods are generated in PASS 5b (`generate_struct_methods`), but free functions are only *forward-declared* in PASS 6a and their bodies generated in 6b — so a method body that calls a same-namespace free function fails with `E3002: function not found` (the binder accepts the call; the generator's `functions` map has no entry yet). The "contracts on pure helpers, called from methods" pattern is therefore unavailable until PASS 6a moves before PASS 5b (or free functions get declared alongside struct methods). The demos work around it by calling contract functions directly from `main`. Also affects contract-bearing methods that delegate to free contract helpers.

### 5.2 Diagnostic code numbering

`DiagnosticCode` is a numeric `uint32_t` registry (`diagnostics/Diagnostic.h`), 9xxx in use through 9010. The spec's `E-CONTRACT-*` are stable strings. Resolution: **reserve 95xx for the verification pass**; the string aliases live only in the JSON envelope (§2.4):

| Numeric | Constant | Alias |
| --- | --- | --- |
| 9501 | `E_CONTRACT_PRECONDITION_NOT_ESTABLISHED` | E-CONTRACT-007 |
| 9502 | `E_CONTRACT_CLAIM_INFERENCE_MISMATCH` | E-CONTRACT-011 |
| 9503 | `E_CONTRACT_RETRY_UNSAFE_EFFECT` | E-CONTRACT-042 |
| 9504 | `E_CONTRACT_ORDERING_CYCLE` | E-CONTRACT-043 |
| 9505 | `E_CONTRACT_INVARIANT_NOT_PRESERVED` | E-CONTRACT-044 |
| 9506 | `E_CONTRACT_EFFECT_PLACEMENT` | E-CONTRACT-045 |
| 9507 | `E_CONTRACT_UNPROVABLE` | E-CONTRACT-046 |
| 9508 | `E_CONTRACT_DENIED_EFFECT` | E-CONTRACT-047 (proposed, §3.3) |
| 9509 | `E_CONTRACT_ILLEGAL_TRANSITION` | E-CONTRACT-048 (proposed, §3.2) |
| 9510 | `E_CONTRACT_REFINEMENT_VIOLATED` | E-CONTRACT-049 (proposed, §3.1) |
| 9511 | `E_CONTRACT_BASELINE_BREAK` | E-CONTRACT-050 (proposed, §4.6) |

### 5.3 No `--json` diagnostics exist

The only `--json` today is `--inspect <file.djlib>` (djlib metadata). Machine-readable diagnostics are net-new — and the agent loop's first deliverable (spec §12.3 "fix hints") depends on them. Proposal: a `--diagnostics-format text|json` flag emitting the §2.4 envelope for both compile and verification diagnostics. Spec §12.2/§14 said "reuse/extend the existing diagnostics infrastructure" — the extension is substantial enough to be its own roadmap item, **pulled earlier than spec §15 Phase 5**.

### 5.4 Two pipeline entry points

The pipeline lives twice: `DjinnCompiler.cpp::compileFromDirectory` (~L779, binder-success check → Generator) and `DjinnCompiler.cpp::run` (~L1151, same seam). Tests and the LSP route through `run()`. The verification pass must hook **both**, ideally through one shared helper so they cannot drift.

### 5.5 Binder boundary for compile-time checking

`Binder::check_compile_time_call` (`binder/Binder.cpp` ~L410–538) already proves constant-arg `require`s (9007) and constexpr-always-throws (9006), gated by `ErrorEnforcement`, plus the non-zero upgrade path. For v1 this **stays in the binder** (it is interwoven with call binding and non-zero propagation). Rules to avoid drift:

- the verification pass must not re-emit 9007 for a call the binder already diagnosed (dedupe by code+location);
- promotion path: callsite fact-tracking (§3.4) *generalizes* the binder's const-arg check; once it lands, the 9007 path can delegate to it rather than coexist.

### 5.6 Purity made concrete

Spec §6.3 requires purity in spec positions but does not define it. Djinn's definition (aligned with the planned effect system):

> **spec-pure(fn)** ⟺ inferred effects(fn) = ∅ — every mutation is local; no I/O, no globals/statics, no `spawn`.

Pre-effects-inference, the syntactic approximation: no assignments to non-local lvalues, no I/O intrinsics, calls only to `constexpr`/`consteval` or transitively spec-pure functions (recursion allowed, depth-capped like `ConstEvalConfig`). The `evaluator/` ConstEvaluator already executes the constant subset of the fragment; symbolic parameters go through the §2.3 fragment translator instead. Async does *not* break purity (coroutines are resumable functions, not effects).

### 5.7 Integration checklist (from the audit)

- `CMakeLists.txt` — add `verification/*.cpp` to `DJINN_SOURCES`, `verification/*.h` to `DJINN_HEADERS` (globs are recursive).
- New pass defines `VERIFIER_ERROR`/`VERIFIER_WARNING` macros following the `BINDER_ERROR` pattern (`AGENTS.md` convention: emit + throw `CompileError`).
- Hook both entry points (§5.4) inside `summary.phase("verification")` where a `BuildSummary` exists.
- `CompilerOptions` field + `main.cpp` branch + `printUsage` line; `djinn.proj` parity via `compiler.verify` (`config/ProjectConfig.h::applyTo`; a key present in the project file overrides the CLI flag, matching `build-mode`). `CompilerResult::verification` carries the collected IR for tooling and tests; `--verify trace` logs every collected obligation plus a summary (the debug view of the pass). The build cache keys on the verification mode — toggling `--verify` forces a recompile, so the `verification` phase row (its check time) shows in the build summary whenever the pass runs.
- `tests/verification/` is auto-globbed by the tests target — no build changes needed for tests.
- Runtime untouched; `mode check` reuses the existing `ContractViolation` throw path (after §5.1 is fixed).

---

## 6. Roadmap delta

Deltas against spec §15, marked **[new]**. Everything else unchanged.

| Spec phase | Delta | Why |
| --- | --- | --- |
| 0 | done (this document §5) | |
| 1 — IR schema | **[new]** manifest JSON schema + diagnostic-code table are normative artifacts, shipped with the IR types | agents consume the format early; backends never couple to mechanisms |
| 2 — structural MVP | **[new]** async contract emission fix (§5.1); **[new]** module effect policies (§3.3) — rides inference | `mode check` unusable in async until fixed; policies are nearly free once inference exists |
| 3 — ordering/retry | **[new]** callsite fact-tracking v1, structural only (§3.4) | makes `E-CONTRACT-007` real; generalizes 9007 without SMT |
| 4 — SMT | **[new]** counterexamples (§4.1); **[new]** refinement types over the fragment (§3.1) | solver already present; refinements are the highest-value business-rule feature |
| 5 — agent tooling | **[new]** `--diagnostics-format json` (§5.3); **[new]** baseline diff (§4.6); **[new]** coverage gate (§4.5); **[new]** LSP integration (§4.3) | agent-loop needs these earlier than spec §15 implied |
| 6 | **[new]** entity state machines (§3.2); **[new]** incremental verification (§4.4) | needs writes-inference (Ph2) + fact tracking (Ph3) |
| 7+ | unchanged | |

---

## 7. Proposed decisions (for promotion into spec §16, continuing its numbering)

| # | Decision | Rationale |
| --- | --- | --- |
| 17 | Kernel artifacts (obligation model, fragment, diagnostic envelope, contract manifest) are normative and language-agnostic; Djinn is the first implementation | the agent loop is the product; the source language is an implementation detail |
| 18 | Numeric 95xx `DiagnosticCode` range internally; `E-CONTRACT-*` strings as JSON aliases | fits the existing numeric registry; stable strings for machines |
| 19 | Refinement types generalize the non-zero mechanism; entity invariants are field refinements | one mechanism instead of three (`nonZero`, invariants, future refinements) |
| 20 | Resolutions carry `rank` (1 = cheapest); lists sorted | agent iteration-count control |
| 21 | `mode assume` requires a `because` justification string | `assumed` obligations are the human-audit surface |
| 22 | Module-level effect policies (`deny`/`allow`) checked against inferred effects | sandboxing agent code; cheap once inference exists |
| 23 | Async contract emission is a prerequisite for `mode check` (pulled into Phase 2) | found in audit; without it the only injected code path silently does not exist for async bodies |

---

## 8. New open questions (for promotion into spec §17)

- Refinement surface: type-alias form only, or also parameter `where` shorthand? And does `Money` need a `decimal` type first?
- State-machine transitions across `await` points — is a coroutine suspended mid-transition "mid-state"?
- Manifest versioning policy: who bumps `djinn.contracts/N`, and what is the compat promise per bump?
- Counterexample (witness) stability across solver versions — pin witness format, not values?
- Effect-policy granularity: namespace vs file vs individual symbols?
- `old()` over mutable parameters under `prove` — SSA versioning requirements on the verification IR?
- Callsite fact-tracking: how do facts interact with the default-async model (facts across suspension points)?
