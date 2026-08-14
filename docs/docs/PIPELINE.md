# Engine Analysis Pipeline — Claude Code Prompt Kit

Run these stages **in order, as separate invocations/sessions** (or via subagent
dispatch inside one session). Each stage writes files to disk so later stages
read *summaries*, not raw source — this is what keeps token usage low.

Create this structure first:
```
docs/
  MODULE_MAP.md
  modules/          # per-module reference docs (Stage 1)
  critique/         # per-module red-team notes (Stage 3)
  ARCHITECTURE.md   # Stage 2 output
  ROADMAP.md        # Stage 4 output
```

---

## Execution order & gates

Run stages strictly in this order. Each stage's inputs are the previous
stage's *outputs on disk* — don't start a stage until the previous one's
files exist and you've spot-checked them (see below).

| Stage | Model | Depends on |
|---|---|---|
| 0 — Repo map | sonnet (or haiku) | nothing |
| 1 — Module docs | sonnet | Stage 0 |
| 2 — Architecture synthesis | sonnet | Stage 1 (all modules) |
| 3 — Red team | fable | Stage 1 (own module) |
| 4 — Roadmap synthesis | fable | Stage 2 + Stage 3 (all modules) |

**Hard gate — do not skip this:** Stage 4 produces `docs/ROADMAP.md` and
then **stops**. Nothing past this point — no Tier 1/2/3/4 item, no
refactor, no "while I'm at it" fix beyond Tier 0 trivial cleanup — gets
implemented unless the user has explicitly named which roadmap item(s) to
work on. If you're Claude Code reading this file: after writing
ROADMAP.md, report that it's ready and wait. Do not proceed to
implementation on your own initiative, even if the next item seems
obvious or low-risk. Tier 0 quick fixes are the only exception, and only
if the user has separately said to apply them.

When the user does pick an item, treat it as its own fresh, narrowly
scoped subagent dispatch — never resume implementation inside the
analysis pipeline's context.

---

## STAGE 0 — Repo Map (single agent, no deep reads)
**Model: sonnet or haiku. Do not proceed to Stage 1 until MODULE_MAP.md exists and you've skimmed it.**

```
Do NOT read full file contents yet. Walk the repository structure and build
docs/MODULE_MAP.md containing:

1. A tree of source files grouped by logical subsystem (renderer, ECS, asset
   loading, editor, platform layer, build system, etc.) — infer grouping from
   filenames/dirs, not from reading bodies.
2. For each file: line count, and a one-line guess at its purpose based on
   filename + a `grep` of top-level function signatures / struct names only
   (use grep/ctags-style extraction, not full reads).
3. A rough dependency list per file: what headers it includes / what other
   modules it likely touches, from #include lines only.
4. Flag any file >600 lines as "needs subagent split" for Stage 1.

Output only the map file. Do not analyze quality yet.
```

---

## STAGE 1 — Per-Module Reference Docs (parallel subagents)
**Model: sonnet (set in module-doc-writer.md frontmatter — don't override up).
Do not proceed to Stage 2 until every module in MODULE_MAP.md has a doc in
docs/modules/ and you've spot-checked one or two against source.**

**Pilot first:** run this on one small, well-understood module alone and
read the output before fanning out to the rest. Cheap to fix a template
problem after one file; expensive after fifteen parallel subagents already
wrote docs in a format you'll want to redo.

Dispatch **one subagent per module** (or per file if a module is large), each
scoped to that module's source files only, using MODULE_MAP.md to know what to
read. Give each subagent this template:

```
You are documenting ONE module of a C game engine: {MODULE_NAME}.
Read only these files: {FILE_LIST}. Do not read the rest of the repo.

Write docs/modules/{MODULE_NAME}.md with:

## Synopsis
2-4 sentences: what this module does, what owns it, what depends on it.

## Public API Reference
For every exported function/macro: signature, one-line purpose, params,
return, side effects, who calls it (search only within given files for
call sites; note if a caller is likely external).

## Data Structures
For every struct/enum/global: fields, purpose, lifetime/ownership,
who allocates/frees it, and whether it's ECS-managed or plain heap/stack.

## Internal (static) functions
Brief list — name + purpose only, no need for full signatures.

## Open questions / unclear intent
Anything whose purpose you couldn't determine from this file alone.

Be concrete and reference actual function/variable names. No filler.
```

---

## STAGE 2 — Architecture Synthesis (single agent, reads docs/modules/* only)
**Model: sonnet. Dispatch as a one-off subagent, not inline in the main
session. Do not proceed to Stage 3 until ARCHITECTURE.md exists.**

```
Read every file in docs/modules/ (do NOT re-read source code). Using only
these summaries, write docs/ARCHITECTURE.md with:

1. High-level system diagram in text/mermaid: how subsystems call each other
   (init order, per-frame call order, shutdown order).
2. ECS deep-dive: how components/entities/systems are stored, how a system
   queries data, how you add/remove a component at runtime, and every
   distinct pattern currently used to access ECS data (list them even if
   inconsistent — that inconsistency is useful signal for the red team pass).
3. A "how do I..." cookbook: add a new component, add a new system, add a
   new render pass, load an asset — step by step, referencing real
   function names from the module docs.
4. Cross-module coupling: which modules reach into others' internals
   instead of going through a clean API (note file:function where visible).

If something is genuinely unclear from the summaries alone, say so rather
than guessing — flag it for a targeted follow-up read.
```

---

## STAGE 3 — Red Team Pass (parallel subagents, per module)
**Model: fable (set in module-red-teamer.md frontmatter — this is the
judgment-heavy pass, worth spending it here). Do not proceed to Stage 4
until every module has a critique doc and you've spot-checked one or two
against source.**

Dispatch one subagent per module again, this time WITH source access plus
its own Stage 1 doc as context:

```
You are red-teaming ONE module: {MODULE_NAME}.
Context: docs/modules/{MODULE_NAME}.md (already-generated reference).
Source: {FILE_LIST}.

Write docs/critique/{MODULE_NAME}.md covering only what you find, with
each item tagged by severity [trivial | moderate | structural]:

- Dead/unreachable/redundant code
- Suspicious "AI-slop" patterns: over-genericized code with no real
  use-case, defensive checks against impossible states, copy-pasted
  near-duplicate functions, inconsistent naming/style vs rest of file
- Memory allocation issues: unnecessary heap churn, missing pooling for
  hot-path allocations, unclear ownership, missing frees, alloc patterns
  that fight the ECS's data-oriented design
- Obvious perf wins: cache-hostile struct layout, unnecessary indirection,
  missing SoA where it'd matter, per-frame allocs
- API ergonomics: places where using this module's data/functions from
  outside is awkward, error-prone, or requires knowing internals
- Missing error handling / silent failure paths

Tag every item with how confirmable it is:
- [READ-VERIFIED]  — confirmed purely by reading source (dead code,
  naming, obvious logic errors, missing frees you can see statically).
- [NEEDS-BUILD]     — plausible from source but needs compiling to
  confirm (e.g. "this alloc is probably hot-path", suspected UB).
- [NEEDS-RUNTIME]   — needs the engine actually running to confirm
  (perf claims, behavior-only bugs like the editor render pipeline).

Be specific: file + function/line reference for every item. No vague
generalities. Skip praise — this pass is only for problems. Keep this
doc under ~500 words unless findings genuinely need more room.
```

---

## STAGE 4 — Roadmap Synthesis (single agent, reads docs/critique/* + ARCHITECTURE.md)

```
Read docs/critique/*.md and docs/ARCHITECTURE.md (not raw source). Also do a
lightweight top-level check for what's entirely MISSING from the engine
needed to ship a game (e.g. UI, audio, networking, animation, input
abstraction, serialization, editor gizmos, asset pipeline, build system
issues, editor/runtime render pipeline mismatch) — note presence/absence
per item, don't deep-dive ones that are simply absent.

Write docs/ROADMAP.md as a tiered work list:

## Tier 0 — Quick fixes (< 1hr each)
Trivial issues, one-liners, dead code removal, naming, obvious leaks.

## Tier 1 — Local refactors (single module, no API break)
Moderate issues fixable inside one module without touching callers.

## Tier 2 — API/ergonomics rework
Changes to how modules are called (esp. ECS access patterns) that ripple
to callers but don't change engine architecture.

## Tier 3 — Structural rework
Things requiring architecture-level redesign (e.g. editor render pipeline,
build system, ECS storage model).

## Tier 4 — Missing systems
Net-new subsystems needed (UI, audio, networking, animation, etc.), each
with a one-paragraph scope estimate.

For each item: short title, one-line description, which module(s), which
critique doc it came from, and its [READ-VERIFIED]/[NEEDS-BUILD]/
[NEEDS-RUNTIME] tag — carry this forward from the critique doc, don't
re-derive it. This file is the menu the user will pick from to dispatch
future focused subagents — keep entries scannable, not essays.

Within each tier, list [READ-VERIFIED] items first — those are actionable
immediately, no laptop/toolchain required. Put a short "Verification
pass" checklist at the bottom of the file: one line per [NEEDS-BUILD]/
[NEEDS-RUNTIME] item, phrased as a yes/no check to run once back on the
real machine, so that session is a fast confirm-and-fix pass instead of
re-analysis.

Also scan the critique docs for any "Open questions" carried over from
Stage 1 that never got resolved, and list them separately at the bottom
under "Needs a targeted follow-up read" — don't let unresolved unknowns
silently disappear between stages.
```

---

## How this actually runs in Claude Code

**Setup — two subagent definitions.** Create `.claude/agents/
module-doc-writer.md` and `.claude/agents/module-red-teamer.md` in your
repo (contents provided as companion files). Both are restricted to
Read/Grep/Glob/Write — no Edit, no Bash — so they can produce docs but
cannot modify your source during the analysis phase; that's enforced by
the tool allowlist, not just requested in the prompt text. If your Claude
Code version exposes directory-scoped permissions (`permissionMode` or
hooks in the frontmatter), it's worth tightening Write further to only
allow paths under `docs/` — the tool allowlist alone stops in-place edits
but doesn't stop Write from targeting an unexpected path.

**Keep the interactive session itself nearly empty.** Every stage,
including the "single agent" synthesis stages (0, 2, 4), should run as a
dispatched subagent — not inline in your main session. Stages 2 and 4
each read a lot of files (all of docs/modules/*, all of docs/critique/*);
if that reading happens inline, your main session's context fills up
with content you don't need to keep around afterward. Dispatched as a
one-off subagent instead, only a short "done, wrote docs/ARCHITECTURE.md"
comes back to the main session. These one-off stages don't need a saved
`.claude/agents/` file — just tell the main session explicitly to
delegate rather than do it itself, e.g.:

  "Don't do this yourself — dispatch a subagent to read docs/modules/*.md
  and write docs/ARCHITECTURE.md per the Stage 2 spec in
  docs/PIPELINE.md. Report back only when it's done."

For Stage 1/3, reference the saved subagent by name and let the main
session fan it out:

  "Read docs/MODULE_MAP.md. For each module listed, dispatch a parallel
  module-doc-writer subagent with that module's exact file list. Wait
  for all to finish, then report which docs were written."

**Where things live:**
- `docs/PIPELINE.md` — this whole doc, saved as a plain file. Not
  CLAUDE.md — CLAUDE.md auto-loads into every session, including future
  unrelated ones, so a multi-stage kit like this would tax sessions that
  have nothing to do with it. Point to it by path when a stage needs it.
- `CLAUDE.md` — keep this for durable project facts every session
  benefits from: naming/style conventions, build commands, coordinate/
  unit conventions, "never touch vendored third-party code." Small and
  load-bearing, unlike the pipeline doc.
- `.claude/agents/` — the two subagent files. Commit these to git along
  with docs/ as you go, so a future session — yours or a fresh one — can
  pick up mid-pipeline or redo one module without redoing the analysis.

**Model assignment.** Both subagent files pin a lighter default model,
overridable per invocation — Stage 1 doc-writing is fairly mechanical
and doesn't need your most capable model. Since Fable 5 is borrowed and
sits above even Opus, treat it as the scarce resource: point it at Stage
3 (judgment-heavy critique) and Stage 4 (synthesis), where reasoning
quality actually changes the output, and let a cheaper model carry
Stage 0 and most of Stage 1.

**Concurrency limits exist.** Claude Code caps how many subagents run at
once and how many run in total per session, both configurable. Fanning
out across a lot of modules at once may queue rather than truly
parallelize past that cap — and `/clear` resets the per-session count if
you hit a ceiling mid-pipeline.

**Spot-check between stages.** Each stage trusts the previous stage's
output completely — that's what keeps tokens low, but it also means an
error in one module's Stage 1 doc propagates silently into Stage 2's
architecture doc and beyond. After Stage 1, skim one or two generated
docs against their actual source before letting Stage 2 run on all of
them. Same idea after Stage 3, before Stage 4 synthesizes the roadmap.

**One outlier file.** If Stage 0 flags a file too large for one subagent
pass (a 3000-line renderer.c), split its Stage 1 pass into two dispatches
by symbol range (e.g. functions A–M / N–Z, from the ctags list) rather
than lowering the quality bar for that one doc.

- Don't let any subagent `grep -r` or `cat` the whole repo "just to be
  safe" — scope its reads explicitly in the prompt, as above.
- After Stage 4, you drive follow-up work by picking Roadmap items and
  spinning up a fresh, narrowly-scoped subagent per item — never reopen
  the whole analysis.

## Extra levers for quality + speed + token savings together

These help all three at once, not just one at the other's expense:

- **Pre-filter with grep/ctags before dispatching a subagent.** For any
  file over ~400 lines, run `ctags`/`grep -n "^[A-Za-z].*("` yourself
  first and hand the subagent a symbol list alongside the file — it reads
  faster and is less likely to skim/miss things in a wall of code.
- **Batch small related files into one subagent, don't go one-per-file.**
  Every subagent pays fixed overhead (system prompt, instructions,
  template). Five 80-line files as one dispatch beats five dispatches.
  Reserve one-per-file for genuinely large/complex files.
- **Persist conventions in CLAUDE.md once, not per-prompt.** Naming
  style, build commands, coordinate/unit conventions, "don't touch
  vendored third-party code" — put it in the project's CLAUDE.md so every
  subagent inherits it for free instead of you repeating it in each
  stage prompt.
- **Order Stage 1 by dependency, not file order.** Doc leaf modules
  (math, containers, allocators) before modules that depend on them —
  later subagents can then be told "assume these primitives work as
  documented in docs/modules/X.md" instead of re-deriving them, which
  also improves accuracy since they're not guessing at foundation code.
- **Cap output length explicitly per subagent** (e.g. "critique doc under
  500 words unless findings genuinely require more"). Without a cap,
  agents pad with restated context, which costs tokens and dilutes signal
  in the roadmap synthesis step.
- **Do Tier 0 (quick fixes) before re-running any analysis.** Landing the
  trivial fixes shrinks the codebase and removes noise, so if you ever
  re-run Stage 3 on a module later, it's cheaper and cleaner than the
  first pass.
- **Skip boilerplate findings.** Tell Stage 3 subagents explicitly: if a
  module has no real findings in a category, write "none" — don't
  manufacture minor style nitpicks to fill the section. Keeps the
  roadmap dense with real signal instead of padded with trivia.
