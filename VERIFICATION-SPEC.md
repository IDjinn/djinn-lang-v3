# Djinn v3 — Semantic Verification Spec

**Status:** Draft for future implementation.
**Scope:** Compile-time verification of business semantics — contracts, effects, concurrency, transactions, UOW.
**Source document:** design draft at `C:\Users\lucas\.zcode\workspace\default\djinn-doc.md` (file `djinn-semantic-concurrency-transactions-uow.md`; this spec consolidates it with final design decisions — see [References](#references)).
**Companion:** extends `GRAMMAR.md` § *Contracts (require / ensure)*.

---

## 1. Summary

The compiler becomes a **semantic verifier for business rules**, designed first for
AI-agent-generated code: the author declares **what must be true**; the compiler
proves it, injects a check for it, or records it as a trusted assumption — never
silently. Contracts are **ghost by default**: zero runtime code is emitted unless the
author explicitly selects an injected check.

The core loop:

```text
Agent generates code + contracts
        |
        v
Djinn verifier (static: structural checks + SMT over a decidable fragment)
        |
        +--> structured error with enumerated resolutions --> agent fixes --> resubmit
        |
        +--> verified (proof report + obligations report)
```

Central principle: **semantics before mechanism**. The contract says
`exclusive from`, `isolation: snapshot`, `retry: allowed` — never `mutex`,
`SELECT FOR UPDATE`, or `BEGIN TRANSACTION`. The selected strategy maps the
semantics onto a mechanism.

---

## 2. Goals and Non-Goals (v1)

### Goals

- Structural verification: reads/writes sets, effects, access-mode conflicts,
  atomicity coverage, ordering-graph acyclicity, retry-safety.
- Value-level proofs for `require` / `ensure` / `invariant` over a **decidable
  fragment** (quantifier-free linear arithmetic) via an SMT backend.
- **Claim-vs-inference**: explicit declarations are checked against what the
  compiler infers from the body. Mismatch = hard error.
- UOW as a first-class construct — **static lifecycle checks only** in v1.
- Strategies as **verification contexts** (assumed guarantees + enforced rules).
- Agent-loop tooling: machine-readable diagnostics with resolutions, obligations
  report, spec-first stub verification, modular cross-module checking.

### Non-Goals (v1)

- No STM backend, no distributed transactions / sagas / compensation.
- No runtime UOW engine (no commit protocol execution) — static checks only.
- No effect polymorphism in the type system (Koka/Unison style) — rejected for v1.
- No ownership/capability integration in the type system — a v2 **design document
  only** (see §7.2).
- Full SQL isolation ladder — only `snapshot` and `serializable` in v1.

---

## 3. Design Principles

1. **Semantics before mechanism.** Contracts name properties; strategies name
   mechanisms. Continuity model: *validate → enforce → execute* — the same
   strategy name first configures the verifier, later enforcement, finally
   code generation.
2. **Claims are checkable.** Any explicit declaration (effects, reads, writes,
   access) is compared against compiler inference. A claim that contradicts the
   body is a compile error, not documentation drift.
3. **No silent downgrades.** A rule the verifier cannot prove is a hard error;
   the author must pick a mode explicitly (`prove` / `check` / `assume` /
   `ignore`).
4. **Ghost by default.** Contract expressions are erased from the binary. The
   only runtime code in this system is injected by an explicit `in mode check`.
5. **Agent-first diagnostics.** Every error carries a stable code, the evidence,
   and an enumerated list of resolutions — machine-readable, fixable without
   human creativity.

---

## 4. Relationship to Existing Contracts

Today's `require(cond)` / `ensure(cond)` clauses throw `ContractViolation` at
runtime, with compile-time verification of constant-decidable clauses.

Migration (backwards compatible):

| Today | Under this spec |
| --- | --- |
| `require(cond)` | `require(cond) in mode check` (default unchanged: runtime check + throw) |
| constant-folded `require` verified at compile time | subsumed by `in mode prove` where the fragment is decidable; otherwise stays `check` |
| `ensure(return == expr)` | unchanged; additionally participates in proof when `in mode prove` |

No existing code changes behavior. New semantics are opt-in through modes and
sections.

---

## 5. Language Surface

### 5.1 Grammar extensions

```ebnf
contract_clause   = ( "require" | "ensure" ) "(" expression ")" [ mode_clause ]
                  | ( "require" | "ensure" ) block [ mode_clause ]
                  | uow_attachment
                  | semantic_section ;

mode_clause       = "in" "mode" ( "prove" | "check" | "assume" | "ignore" ) ;

uow_attachment    = "uow" "(" IDENTIFIER [ "." uow_phase ] ")" ;
uow_phase         = "before_commit" | "after_commit" ;

semantic_section  = section_kind ( ":" section_body | "(" section_body ")" ) ;
(* Parenthesized sections are only valid on function/method signatures;
   inside a uow declaration, sections always use `key: value`. *)
section_kind      = "reads" | "writes" | "access" | "atomic" | "isolation"
                  | "ordering" | "retry" | "effects" | "strategy" | "propagation" ;

access_list       = access_mode operand { "," access_mode operand } ;
access_mode       = "shared" | "exclusive" ;

retry_policy      = "allowed" | "disabled" ;
isolation_level   = "snapshot" | "serializable" ;

invariant_decl    = "invariant" expression [ mode_clause ] ;   (* on entities/structs *)

catch_suffix      = { "catch" "(" error_type [ IDENTIFIER ] ")" block } [ "finally" block ] ;
commit_stmt       = "commit" ";" ;   (* ends the transaction window *)
uow_phase_block   = ( "before_commit" | "after_commit" ) block ;   (* statement in a member body *)
rollback_stmt     = "rollback" [ "(" path { "," path } ")" ] ";" ; (* catch arms of uow members *)
lock_stmt         = "lock" "(" expression { "," expression } ")" block ;

uow_decl          = "uow" IDENTIFIER "{" { uow_member } "}" ;
uow_member        = contract_clause | semantic_section | invariant_decl | uow_decl ;
```

Functions, methods and UOWs all accept semantic sections — on signatures in
either surface form, inside uows always `key: value`. A UOW is a **pure
specification**: contracts, sections, invariants — nothing else. Functions and
methods implement it by attaching `uow (Name[.phase])` in the contract area
between the signature and the body: the bare name is the transaction body,
the suffix runs the whole body in that lifecycle phase (the compact form of
the matching inline block), and lifecycle code can also be written inline as
`before_commit { ... }` / `after_commit { ... }` blocks. The **catch suffix**
after a body handles the member's own errors: handled types leave the
effective throws set, so callers need no `try`. `ensures` is the existing
`ensure` clause family. Inside `ensure` (and uow claims), bare field paths
(`this.balance`) denote the **exit** value and `old(expr)` denotes the value
its field paths had **at entry** (§6.3): `ensure(return == old(this.balance)
- amount)` claims the result equals the entry balance minus the amount.
`old()` accepts field paths (`this.x` / `self.x` / `name.x`) and plain
parameters (identity). It is also valid **in member code** — transaction
bodies, including after `commit;`, and catch/finally arms — where it reads
the same entry snapshot the checked claims use, so pre-state stays
observable for the whole member lifetime.

**Reference parameters.** A uow member's struct-typed parameters are passed
**by reference**: the member mutates the caller's objects, which is what
makes `commit`/`rollback` meaningful (a rollback restores the caller's
objects to their entry state; a commit leaves the mutations in place).
Arguments must be mutable (`mut`) variables or field chains; reference
parameters auto-deref at every use site like `this` does. Parameters of
non-uow functions and scalar parameters everywhere keep value semantics.

### 5.2 Verification modes

| Mode | Meaning | Binary impact |
| --- | --- | --- |
| `prove` | Verifier must prove it (structural or SMT). Unprovable → **error** | none (ghost) |
| `check` | Inject a runtime check; failure raises `ContractViolation` | instrumented |
| `assume` | Trusted, not verified; recorded as an obligation | none (ghost) |
| `ignore` | Documentation only, excluded from verification | none |

Default per declaration kind: `require`/`ensure` → `check` (compatibility);
`invariant` → `prove`; semantic sections → structural verification (no mode
needed; they are claims, always checked against inference).

### 5.3 Obligation status taxonomy

Every obligation in the program ends in exactly one state, reported by
`djinn verify --report obligations`:

```text
proven     discharged statically (structural or SMT)
checked    runtime-injected by explicit mode choice
assumed    trusted (mode assume), listed for human audit
unverified none permitted in CI; state exists only mid-compile
```

### 5.4 Keyword decisions

| Concept | Keyword | Notes |
| --- | --- | --- |
| Unit of work | `uow` | kept from design doc; beat `transaction`, `unit`, `work`, `saga` |
| Effects section | `effects` | standard effect-system term |
| Strategy selector | `strategy` | `backend` rejected (leaks compiler internals) |
| Retry | `retry: allowed \| disabled` | enum, extensible; bool and numeric forms rejected |
| Ordering | `ordering:` | kept; no `lock` in the name (mechanism-neutral) |
| Access modes | `shared`, `exclusive` | `owned`/`isolated`/`transactional` reserved for v2 |
| Isolation | `snapshot`, `serializable` | theory names, not SQL-only names |
| Nesting | `propagation: join` (default) | `independent`, `savepoint` reserved |
| Atomicity | section `atomic:` **declares**; members realize it | discharged by `strategy: database`, otherwise by `lock` scopes |
| Rollback | bare `rollback;` (whole objects); `rollback (paths)` lists fields | entry snapshots, catch arms only |
| Commit | `commit;` ends the transaction window | the rest of the body is after-commit content |
| Locking | `lock (a, b) { ... }` statement | ordered acquisition, unwind-safe release; await/spawn rejected inside |
| Member handlers | function-level `catch` suffix (+ `finally`) | handled types leave the effective throws set |

---

## 6. Verification Model

### 6.1 Verification IR

The intermediate representation (renamed from the design doc's "Concurrency IR"
— scope is now all business-rule verification):

```text
VerificationIR {
    claims          declared sets: reads, writes, access, atomic, effects
    inferences      compiler-derived sets from the body
    obligations     require/ensure/invariant + status + mode
    ordering_graph  declared orderings over resource keys
    retry_policy    per uow/atomic region
    strategy_ctx    assumed guarantees + enforced rules per strategy
    lifecycle       uow structure: body, before_commit, after_commit, nesting
}
```

Pipeline position (existing compiler phases unchanged):

```text
lexer -> parser -> binder -> [ verification/ : claims vs inferences, IR, checks ] -> generator
```

### 6.2 Claim vs. inference

For every function/UOW with semantic sections, the compiler infers
reads/writes/effects from the body (transitively through callees) and compares:

```text
inferred ⊆ declared  for effects and reads   (undeclared effect = hidden behavior)
inferred == declared for writes              (claim must match exactly)
```

Violation is a **hard error** (`E-CONTRACT-011`). Explicit sections are
checkable claims; omitted sections are simply inferred (agents may generate
without annotations and still get intra-body checks).

### 6.3 Decidable fragment and the SMT layer

Value-level proving is restricted to a decidable fragment: quantifier-free
linear arithmetic over numeric fields and booleans, comparisons, `old()`
references in `ensure`. Non-goals: quantifiers, uninterpreted function theories
beyond basics, arbitrary calls.

**Purity rule:** only pure expressions may appear in any spec position
(`require`, `ensure`, `invariant`). Purity is the entry ticket to contracts.

The SMT backend sits behind an interface (`verification/smt/`) so all
structural checks of Phase 2 do not depend on a solver. Unprovable-in-fragment
rules under `mode prove` are **hard errors** (`E-CONTRACT-046`) — the author
must choose `check` or `assume` explicitly. Never a silent downgrade.

### 6.4 Ghost erasure

Contract expressions, semantic sections, and `assume` declarations are erased
before code generation. `mode check` is the only construct that emits code
(a guard + `ContractViolation` throw, reusing today's mechanism). A verified
program's binary is byte-identical to the same program with contracts stripped,
except for explicitly injected checks.

---

## 7. Concurrency Checks

### 7.1 Access modes (v1: `shared` / `exclusive`)

Access sections declare reader/writer semantics per resource for the duration
of a UOW/atomic region. The verifier rejects incompatible overlap between
operations that the IR knows can run concurrently (declared parallelism:
`spawn`, concurrent UOWs).

### 7.2 Race-freedom honesty (important)

Static **proof** of data-race freedom requires aliasing/ownership information;
Rust and Pony put capabilities in the type system for exactly this reason.
Djinn v1 therefore:

- checks access modes at IR level, with honest best-effort semantics
  (advisory checks are labeled as such in reports), and
- defers **type-system reference capabilities** to a v2 design document.
  No v1 diagnostic may claim "no data races" as a proven property.

The compiler still distinguishes and detects, at IR level:

- **Data race** — two flows access memory incompatibly (`counter += 1`).
- **Business race** — each access is individually protected but the logical
  sequence is not atomic (check-then-act on `balance`).

### 7.3 Ordering graph

`ordering: Account.id ascending` contributes an edge to a program-wide graph
keyed by resource ordering keys. The verifier rejects cycles at compile time:

```text
uow A orders x -> y ; uow B orders y -> x
=> E-CONTRACT-043: potential circular wait, cycle x -> y -> x
```

Static check: the ordering keys exist and orderings are mutually consistent.
Runtime acquisition order is the strategy's responsibility.

---

## 8. Strategies as Verification Contexts

A strategy tells the verifier (a) which guarantees to **assume** from the
environment and (b) which rules to **enforce** on user code. v1 ships `database`;
`lock`, `stm`, `optimistic`, `actor`, `distributed` are reserved names.

| Strategy | Assumed guarantees | Enforced rules | Materialization (later phases) |
| --- | --- | --- | --- |
| `database` | declared `isolation` provided by the DB transaction; retry on serialization failure | non-idempotent effects (`external_io`, `process`, `filesystem`) only in `after_commit`; retry-safety of body | `BEGIN/COMMIT`, row locks, guarded updates |
| `lock` | exclusive/shared honored by lock protocol | ordering compliance | lock acquisition/release from access + ordering |
| `stm` | atomicity via read/write-set validation | retry-safety, idempotency | speculative execution + retry |

Continuity: in v1 the strategy column "materialization" is **documentation of
the intended mapping** (the verifier only records that a mapping exists and is
consistent); later phases turn it into enforcement, then code generation.
Example mapping recorded for `strategy database`:

```text
isolation snapshot            -> REPEATABLE READ + SELECT FOR UPDATE
requires from.balance >= amount -> UPDATE ... WHERE balance >= :amount  (guarded update)
```

This is why `requires from.balance >= amount` needs no injected `if`: the
strategy guarantees the proposition by mechanism, and the verifier records the
mapping as consistent.

---

## 9. Retry Semantics

- Default: `retry: disabled` (single execution). Retry is opt-in per contract.
- `retry: allowed` enables retry-safety checking of the body: every effect in
  the body must be idempotent or pure. Non-idempotent `external_io` inside a
  retryable region is `E-CONTRACT-042`.
- `idempotent(fn)` declares an operation retry-safe (mode-checked like other
  declarations; stdlib ships core entries as `assume` obligations).
- `after_commit` is the designated home for non-idempotent external effects:
  it runs exactly once, after durability; abort/rollback/retry never reach it.

---

## 10. UOW Semantics

A UOW is *a unit of work and consistency that completes or aborts according to
its contract*. It is a **pure specification**: contracts, semantic sections and
invariants — no body, no generated code. Real functions implement it by
attaching `uow (Name)` next to their body; the analyzer verifies each member
against the spec, and the transaction's lifecycle is the model the member's
inline `before_commit` / `after_commit` blocks project onto.

```text
OPEN -> ACTIVE -> VALIDATING -> COMMITTING -> COMMITTED -> AFTER_COMMIT
                    |                |
                    +--> ABORTED <---+
```

v1 checks (static only, per attached member and across members):

- Every member's inferred writes/reads/effects (through its calls) stay inside
  the UOW's claims, anchored at the offending line (E-CONTRACT-011). All three
  claim sections are always explicit — `atomic:` never implies `writes:`.
  Inline `before_commit` blocks and statements before `commit;` join the
  transaction window; `after_commit` blocks and statements after `commit;`
  join the post-commit phase (claims apply, the retry window does not).
- The transaction members jointly realize exactly the declared `atomic` set
  (no more, no less).
- Atomicity discharge (E-CONTRACT-053): `strategy: database` delegates to the
  DB transaction (coverage + ordering). Any other strategy — or none —
  requires every atomic path (and every `exclusive` operand's writes) to be
  written only inside `lock` scopes naming the path's root object.
- All-or-nothing (E-CONTRACT-054): a member that can fail mid-transaction (a
  call to a throwing callee) and writes an atomic path must roll it back —
  bare `rollback;` restores whole-object entry snapshots of every struct
  parameter and `self`; the listed form names exact paths.
- ContractViolation coverage (E-CONTRACT-055): a member whose body can violate
  a check-mode contract (its own or a callee's) must catch it in its handler
  suffix, or the clause must be `in mode prove`.
- Retry safety: with `retry: allowed`, non-idempotent effects
  (`external_io`, `process`, `filesystem`) cannot appear in the transaction
  window (E-CONTRACT-042) — they belong in `after_commit` blocks.
- Effect placement: `strategy: database` confines non-idempotent effects to
  `after_commit` blocks (E-CONTRACT-045).
- The UOW's `require`/`ensure` clauses are inherited by every member whose
  parameters can name the clause's operands — proven at compile time, never
  injected at runtime; unresolvable clauses are recorded as assumed
  obligations for the audit trail.
- Ordering claims across UOWs must not conflict (E-CONTRACT-043); access
  modes must not contradict themselves per operand (E-CONTRACT-011).
- Attaching to an unknown UOW is a hard error (E-CONTRACT-051); a UOW with
  no participating functions warns (E-CONTRACT-052).
- Nesting: default `propagation: join` (inner joins the outer's lifecycle,
  one commit). `independent` / `savepoint` reserved for later phases.
- `requires uow` (a function demanding an active UOW context) is reserved.

---

## 11. Effects

Implemented vocabulary (v1): `memory`, `filesystem`, `time`, `random`,
`process`, `external_io`. `database`, `network` and `thread` are reserved
names the parser rejects until their strategies land.

- Effects are **inferred** from bodies (transitively); explicit `effects:`
  sections are claims verified by claim-vs-inference, used at API boundaries to
  restrict or document.
- `external_io` subsumes non-idempotent outside interaction (email, HTTP POST,
  payments). The **non-retry-safe set** is `external_io`, `process` and
  `filesystem`: none of them can be safely re-run by a retry, and under
  strategy `database` they belong in `after_commit`. `memory`, `time` and
  `random` are retry-safe — a retry allocates fresh resources and re-observes
  the clock/RNG, duplicating nothing observable.
- Effects gate composability: what may run inside atomic regions, UOW bodies,
  retryable regions, and parallel code.

---

## 12. Diagnostics and the Agent Loop

### 12.1 Error registry (initial)

| Code | Meaning |
| --- | --- |
| `E-CONTRACT-007` | precondition not established at call site |
| `E-CONTRACT-011` | declared claim contradicts inferred behavior (reads/writes/effects) |
| `E-CONTRACT-042` | retryable region contains non-retry-safe effect |
| `E-CONTRACT-043` | ordering cycle (potential circular wait) |
| `E-CONTRACT-044` | entity invariant not preserved by writes target |
| `E-CONTRACT-045` | effect placement violates strategy rules (e.g. `external_io` outside `after_commit`) |
| `E-CONTRACT-046` | obligation unprovable in `mode prove` (author must choose `check`/`assume`) |
| `E-CONTRACT-047` | function attaches to an unknown uow |
| `E-CONTRACT-048` | uow has no participating functions (warning) |
| `E-CONTRACT-053` | atomic claim not discharged by a lock scope (strategy provides no atomicity) |
| `E-CONTRACT-054` | member can fail mid-transaction without rolling back an atomic path |
| `E-CONTRACT-055` | uow member does not handle `ContractViolation` |
| `E-CONTRACT-051` | function attaches to an unknown uow |
| `E-CONTRACT-052` | uow has no participating functions (warning) |

### 12.2 Diagnostic shape

Every diagnostic: stable code, evidence (declared vs inferred values with
locations), and **enumerated resolutions**. Machine-readable via `--json`
(reuse/extend the existing diagnostics infrastructure; contract codes are a new
`DiagnosticCode` range emitted from the verification pass).

```text
E-CONTRACT-042: uow `Transfer` is retryable
  retry: allowed + strategy database (retries on serialization conflicts)
  detected effect: external_io   operation: send_transfer_email (line 31)
  resolutions:
    1. move operation to after_commit
    2. declare operation idempotent
    3. set retry: disabled
    4. provide compensation
```

### 12.3 Agent-loop features (delivery order)

1. **fix hints** — the structured diagnostics above (first deliverable).
2. **spec-first stubs** — verify signature + contract with empty body; agent
   generates the contract first, body second; each step verified.
3. **modular verification** — callers checked against library contracts without
   seeing bodies (scales to packages).
4. **obligations report** — CI summary of proven / checked / assumed; `assume`
   is always visible for human audit.

---

## 13. Worked Example (normative)

Payments service, PostgreSQL backend. The agent's first draft contains two
real bugs; the verifier rejects; the agent applies listed resolutions.

Domain:

```md
entity Account {
    id: Uuid
    owner_id: Uuid
    balance: Money                                  // decimal(19,4)
    currency: Currency

    invariant balance >= 0 in mode prove            // compiler PROVES this on every write
    invariant currency.is_supported() in mode prove // contract fns must be pure
}

idempotent(audit.record) in mode assume             // trusted, tracked as CI obligation
```

Draft (rejected):

```md
uow Transfer {
    requires amount > 0
    requires from.currency == to.currency

    access:                                      // what we need, not how
        exclusive from
        exclusive to

    atomic:                                      // together or not at all
        from.balance
        to.balance

    writes:                                      // claim vs inference: checked
        from.balance
        to.balance                               // BUG: audit_events missing

    ordering: Account.id ascending               // deadlock prevention, graph-checked
    isolation: snapshot
    retry: allowed                               // body must be retry-safe
    strategy: database                           // verification context
}
```

```cpp
// transfer_money.djinn — transaction body member
void transferMoney(Account from, Account to, Money amount)
    uow (Transfer)
{
    from.balance -= amount;
    to.balance += amount;
    audit.record(TransferCompleted, receipt);    // ok: stdlib idempotent (assume)

    // BUG: external_io inside a retryable uow — email sent twice on retry
    send_transfer_email(from.owner.email, receipt);
}
```

Verifier output:

```text
E-CONTRACT-042  impure call inside a retryable uow
                note: uow 'Transfer' defined at bank.djinn:10:5
                note: external_io may run twice when the uow retries
                help: move it to after_commit, or set retry: disabled
E-CONTRACT-011  write to 'audit_events.balance' is not declared in uow 'Transfer'
                note: uow 'Transfer' defined at bank.djinn:10:5
                note: declared at bank.djinn:12:5: from.balance, to.balance
                help: add it to the writes section
```

Verified version:

```md
uow Transfer {
    requires amount > 0                          // proven (Z3)
    requires from.currency == to.currency        // proven
    requires from.balance >= amount              // proven; implies Account invariant

    ensures:                                     // callers rely on THIS, not the body
        to.balance   == old(to.balance) + amount
        from.balance == old(from.balance) - amount
        from.balance >= 0                        // corollary of requires

    reads:  from.balance, to.balance, from.currency, from.id, to.id
    writes: from.balance, to.balance, audit_events  // claim == body now

    access: exclusive from, exclusive to
    atomic: from.balance, to.balance
    ordering: Account.id ascending               // acyclic across all program uows
    isolation: snapshot
    retry: allowed                               // sound now: no external_io in body
    strategy: database
}
```

```cpp
// Transaction body member: pure transition + idempotent audit,
// provable and retry-safe. The handler suffix frees callers from `try`;
// a bare `rollback;` restores every parameter if anything fails.
void transferMoney(Account from, Account to, Money amount)
    uow (Transfer)
{
    from.balance -= amount;
    to.balance += amount;
    audit.record(TransferCompleted, receipt);

    // After-success block: runs exactly once, after durability;
    // abort/retry never reach it.
    after_commit {
        send_transfer_email(from.owner.email, receipt);
    }
} catch (ContractViolation violation) {
    rollback;
} catch (BalanceModifyException balanceException) {
    rollback;
}
```

Caller — verified against the contract only (modular):

```cpp
fn handle_transfer(req: TransferRequest) -> Response {
    let from = find_account(req.from_id)?;       // effects { database }: fine outside a uow
    let to   = find_account(req.to_id)?;

    // req.amount is network input — unprovable statically.
    // mode `check` = the ONLY injected runtime code in this flow (422).
    check req.amount > 0 in mode check else reject(422, "amount must be positive");

    Transfer { from, to, amount: req.amount };   // race -> guarded UPDATE -> 409, strategy-mapped
    ok(202);
}
```

Final report:

```text
djinn verify
  Transfer ........ PROVEN   requires/ensures (Z3) | ordering acyclic | retry-safe
  strategy mapping  snapshot -> REPEATABLE READ + FOR UPDATE
                    requires balance >= amount -> UPDATE ... WHERE balance >= :amount
  runtime emitted .. none (contracts are ghost)

obligations: 14 proven | 1 checked (req.amount > 0) | 1 assumed (audit.record) | 0 unverified
```

---

## 14. Implementation Notes (this repository)

- New top-level pass/directory `verification/` (sibling of `binder/`,
  `generator/`): IR types, inference, checks. The binder supplies declared
  claims; the verifier infers from AST and diffs.
- Diagnostics: new `DiagnosticCode` range `E_CONTRACT_*` emitted through the
  existing error macros from the verification pass; extend `--json` output and
  add `--report obligations`.
- Ghost erasure: the generator already skips contract nodes today except for
  runtime `require`/`ensure`; preserve that path for `mode check` only.
- SMT: interface first (`verification/smt/`), Z3 integration later; no
  structural check may depend on the solver.
- Tests: `tests/verification/` mirroring `tests/` group conventions; the
  worked example in §13 is the first end-to-end fixture.
- v1 touches nothing in `runtime/` — the only emitted code is the existing
  `ContractViolation` guard path, reused for `mode check`.

## 15. Roadmap

| Phase | Deliverable |
| --- | --- |
| 0 | Audit current `require`/`ensure` + enforcement levels + diagnostics codes; map where claims/inference hook in |
| 1 | Verification IR schema (types only, no passes) — **spec-first**, so backends never couple to mechanisms |
| 2 | Structural MVP vertical slice: effects inference, reads/writes claim-diff, access conflicts, atomic coverage, `strategy database` context rules, `uow` parse+lower (static), validated end-to-end on the §13 Transfer example |
| 3 | Ordering graph + cycle detection; retry-safety; `before_commit`/`after_commit` placement; `propagation: join` |
| 4 | SMT layer behind interface; `mode prove` for `require`/`ensure`/`invariant` over the decidable fragment |
| 5 | Agent tooling: `--json` diagnostics with resolutions, obligations report, spec-first stub verification, modular callsite checks |
| 6 | `strategy lock` (runtime protocol) + `mode check` injection framework |
| 7+ | `stm` backend; distributed/sagas (design doc first); type-system reference capabilities (design doc first) |

**Delivered ahead of schedule (no SMT):** value-level claim/inference diffing
over the decidable linear fragment shipped with Phase 1's scaffolding —
`verification/Fragment.h/.cpp` (affine values + condition translation),
`verification/Facts.h/.cpp` (equalities, integer intervals, relational facts,
branch joins, ghost renames for self-referential updates),
`verification/BodyAnalyzer.h/.cpp` (return-path inference with if/else joins,
ternary path splitting, loop invalidation, try/catch joins). `ensure` clauses
are proven against inferred return paths or rejected as `E-CONTRACT-011`
(9502); `require` clauses are decided at call sites over translatable
arguments and rejected as `E-CONTRACT-007` (9501) with evaluated evidence.
Undecidable checks stay `checked` (runtime).

**Also delivered (Phase 2/3 structural slice + Phase 5 partial, static only):**

- Interprocedural verification (§12.3): proven postconditions propagate
  through call chains (`transfer → debit → credit`), method call sites are
  checked against their contracts, and contract-bearing extern stubs are
  verified spec-first.
- Grammar + AST for §5.1: `in mode` clauses, all ten semantic sections (both
  `kind:` and `kind(...)` surface forms), struct/uow `invariant` declarations,
  and `uow (Name)` attachment on functions/methods (parser-side, zero lexer
  changes). The uow pseudo-body (`body`/`before_commit`/`after_commit` blocks)
  was removed: a uow is a pure specification, and the lifecycle lives inline in
  the member bodies as `before_commit { ... }` / `after_commit { ... }`
  blocks (deferred to the member's success path).
- Claim-vs-inference over semantic sections (§6.2): reads/effects
  `inferred ⊆ declared`, writes `inferred == declared`, hard error 9502.
- uow checks over attached members (§9/§10): member inference (callee paths
  expanded into the member's operand terms, receiver included) diffed against
  the claims anchored at the offending line, atomic coverage realized jointly
  by the transaction members, retry-safety (9503), `strategy database` effect
  placement (9506), access-mode conflicts, program-wide ordering
  contradictions (9504). Unknown uow attachment is a hard error (9512,
  E-CONTRACT-051); a memberless uow warns (9513, E-CONTRACT-052). The uow's
  require/ensure clauses are inherited by members whose parameters can name
  the operands — compile-time only, never injected; unresolvable clauses stay
  assumed obligations.
- Entity invariants (§10) default `prove`: seeded on method entry, decided at
  every return path over tracked field values; 9505 on violation, 9507 when
  unprovable. `in mode check` on invariants is rejected (no runtime injection
  in this phase).
- Modes (§5.2): `prove` demands proof (9507 otherwise), `assume` records
  trusted obligations, `ignore` drops them; `check` stays the default.
- Mode gating in the generator: only `in mode check` clauses reach the binary
  — `assume`/`prove`/`ignore` contracts emit no runtime code, so a
  compile-time-proven contract costs nothing at runtime.
- Transactional member surface (§10): the function-level catch suffix
  (handled types leave the effective throws set), bare `rollback;` with
  whole-object entry snapshots and the all-or-nothing rule (9515,
  E-CONTRACT-054), the `lock` statement lowered to
  `__djinn_object_lock`/`__djinn_object_unlock` with the atomic-discharge
  check (9514, E-CONTRACT-053), and the ContractViolation coverage rule
  (9516, E-CONTRACT-055).
- Generator/runtime neutrality: uows live outside `Program::acceptAll`, and
  sections/invariants are symbol-level metadata only — verified binaries are
  byte-identical to contract-stripped ones (except today's `check` guards).

Remaining from §15: SMT-backed `prove` (Phase 4), full `mode check`
injection framework (Phase 6), `strategy lock` and beyond, uow runtime
protocol, `--report obligations`/`--json` tooling (Phase 5 leftovers).
Delivered early from Phase 6: the `lock` statement with runtime lowering
(`__djinn_object_lock`/`__djinn_object_unlock`) and the atomic-discharge
verification it enables; the uow runtime protocol (inter-member commit
engine) remains future work — all-or-nothing is currently per member.

## 16. Decision Log

| # | Decision | Rationale |
| --- | --- | --- |
| 1 | Compile-time-first verifier; runtime only via explicit `check` mode | addresses the top risk of the original proposal ("static overpromise"); zero-cost default |
| 2 | IR named **Verification IR** (was Concurrency IR) | scope is all business-rule verification; concurrency is one domain |
| 3 | Modes `prove`/`check`/`assume`/`ignore`; unverifiable rule = hard error | no silent downgrades; C++26/SPARK-inspired but verifier-first naming |
| 4 | Claim-vs-inference mismatch = hard error | declarations become checkable claims; keystone of the agent loop |
| 5 | Ghost by default; purity required in spec position | "no runtime effects" is literal; proofs need pure specs |
| 6 | Race-freedom: IR-level best-effort v1; type-system capabilities = v2 design doc | honesty: static race proofs need aliasing info (Rust/Pony lesson) |
| 7 | `uow` keyword; hybrid construct (syntax now, runtime protocol later) | continuity with design doc; deferred execution engine |
| 8 | `shared`/`exclusive` only in v1 | reader/writer theory is enough to start; rest reserved |
| 9 | Isolation `snapshot`/`serializable` only in v1 | covers real needs; SQL ladder later as compatibility |
| 10 | Effects inferred, explicit at boundaries | agents annotate nothing; annotations become audited claims |
| 11 | `retry: allowed\|disabled`, default `disabled` | fail-closed for imperative/agent code; STM-style retry is opt-in |
| 12 | `strategy` kept; strategies = verification contexts | validate → enforce → execute continuity on the same IR |
| 13 | Nesting default `propagation: join` | simplest mental model (Spring `REQUIRED`); alternatives opt-in |
| 14 | v1 excludes STM, distributed, runtime UOW engine | prove the IR on the database strategy before multiplying backends |
| 15 | `atomic` dual form kept: section declares, block realizes | checked coverage ties them together; not two syntaxes for one thing |
| 16 | Agent feature order: fix hints → spec-first → modular → obligations | diagnostics are the compiler's UX; everything else builds on them |
| 17 | Function-level `catch` suffix; handled types leave the effective throws set | members own their failures ("runs or does not run"); callers keep plain calls |
| 18 | Attachment suffix + inline lifecycle blocks are both first-class | `uow (Name.after_commit)` is the compact form of the inline block; after-commit logic is part of the transaction, not a separate transaction |
| 19 | Rollback = explicit statement + whole-object entry snapshots (bare form) | explicit syntax lowers to real code; the spec never injects; no per-path bookkeeping for the common case |
| 20 | `lock` statement discharges `atomic` only where no strategy does | `strategy: database` stays zero-verbosity; in-memory atomicity demands visible synchronization |
| 21 | ContractViolation must be caught in uow members (or clauses move to `prove`) | an escaping violation un-verifies the all-or-nothing property |

## 17. Open Questions

- UOW across `await` / thread boundaries — semantics of context propagation.
- Entity-to-table mapping rules for `strategy database` (who owns the schema).
- Savepoints (`propagation: savepoint`) — interaction with `before_commit` failure.
- Deadlocked/conflicted database transactions — retry vs `E-CONTRACT-042` surface.
- `external_io` classification: per-call annotation vs per-function effect.
- Reference capabilities in the type system (v2 design document).
- Distributed UOW / sagas / compensation (later-phase design document).

---

## References

### Primary

- **Design draft (source of this spec):** `C:\Users\lucas\.zcode\workspace\default\djinn-doc.md`
  — *Djinn — Semantic Concurrency, Transactions & UOW* (`djinn-semantic-concurrency-transactions-uow.md`).
  Sections most relevant per topic: §3–§5 (contract extension), §6–§10 (access, reads/writes, atomicity, isolation, ordering), §11–§14 (race taxonomy, invariants, effects, retry), §15–§22 (UOW lifecycle, nesting, before/after commit), §23–§25 (strategies, Concurrency IR, static vs runtime), §26 (prior art), §28 (agent-first), §29 (original roadmap — superseded by §15 here), §30 (open questions — partially resolved by §16/§17 here).

### In this repository

- `GRAMMAR.md` § *Contracts (require / ensure)* — the existing contract clauses this spec extends.
- `README.md` — compiler architecture (lexer → parser → binder → generator) referenced in §6.1 and §14.

### Prior art

- Rust — ownership, borrowing, `Send`/`Sync`, data-race prevention: <https://doc.rust-lang.org/book/>
- Pony — reference capabilities: <https://tutorial.ponylang.io/reference-capabilities/guarantees.html>
- Haskell STM — `atomically`, `retry`, transaction composition: <https://ghc.gitlab.haskell.org/ghc/doc/users_guide/exts/stm.html>
- Clojure — STM and transactional references: <https://clojure.org/about/concurrent_programming>
- Ada / SPARK — contracts, invariants, proof with VC + Z3, `Ghost` specs: <https://docs.adacore.com/spark2014/docs/spark2014.toolchains.html>
- C++26 contracts — `pre`/`post`/`contract_assert`, semantic modes: <https://wg21.link/p2900>
- Dafny — verification conditions over a decidable fragment: <https://dafny.org/>
