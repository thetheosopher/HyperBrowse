# Comprehensive Code Review Prompt
## For GitHub Copilot (Claude Opus) in VS Code — `@workspace` context

> **How to use:**
> 1. Open GitHub Copilot Chat in VS Code.
> 2. Ensure the model is set to **Claude Opus** (or the most capable available).
> 3. Paste the prompt below (everything after the horizontal rule) into the chat.
> 4. The `@workspace` agent will index your project before the review begins.
> 5. The model will produce artifacts — instruct it to save them per the output instructions at the bottom.

---

## THE PROMPT

```
@workspace

You are about to conduct a deep, comprehensive review of this entire codebase. You will perform this review simultaneously from two expert perspectives:

**Perspective 1 — Expert Software Optimization Engineer**
You have 20+ years of experience in systems performance, algorithmic complexity, scalability, and production reliability. You have deep expertise in the language(s), frameworks, and infrastructure patterns present in this codebase. You think in terms of latency, throughput, memory footprint, CPU efficiency, I/O patterns, concurrency, caching strategies, and technical debt.

**Perspective 2 — Expert Product Manager (domain-native)**
You are a senior PM with deep domain expertise in whatever this software does. You think in terms of user value, feature gaps, competitive differentiation, UX friction, business impact, and the product roadmap. You read code to understand what the product actually does vs. what it should do, and you identify where engineering effort is misaligned with user outcomes.

---

### PHASE 1 — PROJECT RECONNAISSANCE

Before forming any opinions, conduct a thorough survey of the repository:

1. Identify the project's purpose, domain, and intended users by reading READMEs, docs, config files, and top-level structure.
2. Catalog the tech stack: languages, frameworks, libraries, infrastructure, build system, CI/CD.
3. Map the architecture: identify major modules, services, data flows, API surfaces, and integration points.
4. Locate any existing design artifacts (docs/, ADRs, architecture diagrams, specs, CHANGELOG, wiki files, etc.) — note their locations, as your output must be stored consistently with them.
5. Assess codebase size, age signals (commit patterns if visible, deprecated deps), and test coverage posture.

Summarize your findings in a **Project Context** section at the top of your output document.

---

### PHASE 2 — PERFORMANCE & TECHNICAL REVIEW

From the Software Optimization Engineer perspective, conduct a systematic review covering:

**2A. Algorithmic & Computational Efficiency**
- Identify O(n²) or worse loops, redundant computation, hot paths lacking memoization or caching.
- Flag unnecessary re-renders, recomputation, or repeated DB/API calls that could be batched or cached.

**2B. Database & Data Layer**
- Review query patterns: N+1 problems, missing indexes (inferred from query shapes), over-fetching, lack of pagination.
- Assess schema design for normalization issues or denormalization opportunities.
- Identify missing connection pooling, transaction misuse, or locking risks.

**2C. Concurrency, Async & I/O**
- Find blocking calls in async contexts, race conditions, missing parallelization opportunities.
- Review queue/worker patterns for bottlenecks.

**2D. Memory & Resource Management**
- Identify memory leaks (unclosed resources, unbounded caches, event listener accumulation).
- Flag over-allocation patterns.

**2E. Network & API Efficiency**
- Review payload sizes, missing compression, chatty APIs that should be consolidated.
- Assess retry/backoff logic, timeout handling, circuit breakers.

**2F. Build, Bundle & Startup**
- Flag large bundle sizes, missing code splitting, slow startup paths, heavy synchronous initialization.

**2G. Observability & Reliability Gaps**
- Identify missing logging, metrics, tracing, error boundaries, and health checks.
- Flag unhandled edge cases or failure modes that would cause silent data corruption.

**2H. Security Performance Intersections**
- Note any patterns that are both a security risk and a performance risk (e.g., synchronous crypto, regex DoS exposure).

**2I. Dependency Health**
- Flag outdated, abandoned, or unnecessarily heavy dependencies where lighter alternatives exist.
- Note any duplicate functionality across deps.

---

### PHASE 3 — PRODUCT & FEATURE REVIEW

From the Expert Product Manager perspective, analyze the codebase for:

**3A. Feature Completeness vs. User Needs**
- Based on what this software does, identify obvious missing features that users in this domain would expect.
- Flag features that appear half-implemented or abandoned (dead code, TODO-heavy areas, feature flags that are always off).

**3B. UX & Developer Experience Gaps**
- Identify flows with unnecessary friction (too many steps, missing defaults, poor error messages, absent loading states).
- For APIs or SDKs: assess ergonomics — are common tasks easy? Are error messages actionable?

**3C. Data & Analytics Gaps**
- Note where the product collects insufficient data to make product decisions, or where valuable signals are not being captured.

**3D. Competitive & Domain Gaps**
- Based on the domain, call out capabilities that are standard in this space but absent here.

**3E. Technical Investment vs. Value Misalignment**
- Identify areas where significant engineering complexity exists but delivers low user value.
- Identify high-value features that appear underinvested relative to their impact.

**3F. Monetization & Growth Hooks** (if applicable)
- Note missing instrumentation for conversion, retention, or growth analysis.
- Flag opportunities for tiering, upsell, or partnership integration points.

---

### PHASE 4 — SYNTHESIS & PRIORITIZED ROADMAP

Produce a consolidated, prioritized enhancement plan:

1. **Quick Wins** (< 1 day effort, high impact) — list at least 5
2. **High-Impact Medium Projects** (1–2 weeks, clearly scoped) — list at least 5, with enough detail that another engineer can begin immediately
3. **Strategic Initiatives** (multi-week, architectural or product-level) — list 3–5, each with:
   - Problem statement
   - Proposed approach
   - Key risks and open questions
   - Success metrics
4. **Debt Retirement Candidates** — code or patterns that should simply be removed or replaced
5. **Dependency Upgrade Path** — ordered list of dep upgrades by risk/value

For each item include:
- **Category**: Performance | Reliability | Feature | UX | Observability | Security | DX
- **Perspective**: Engineering | Product | Both
- **Effort estimate**: XS / S / M / L / XL
- **Impact**: Low / Medium / High / Critical
- **File(s) / area(s) affected** (with relative paths)
- **Specific recommendation** (concrete, not vague)

---

### OUTPUT INSTRUCTIONS

1. **Locate existing documentation/design artifacts** in this repo (docs/, .github/, architecture/, ADRs, etc.). Your output files must be placed in the same directory or follow the same naming convention.

2. If no design artifact directory exists, create `docs/` at the repo root.

3. Produce the following files:

   **`[artifact_dir]/CODE_REVIEW_[YYYY-MM-DD].md`**
   The full review document: Project Context, all Phase 2 and Phase 3 findings, organized by category with severity labels (🔴 Critical / 🟠 High / 🟡 Medium / 🔵 Low).

   **`[artifact_dir]/ENHANCEMENT_ROADMAP_[YYYY-MM-DD].md`**
   The Phase 4 synthesis only — the actionable roadmap formatted as a clean planning document with a summary table at the top (item | category | effort | impact | area) followed by full detail sections. This is the handoff document for implementing models or engineers.

   **`[artifact_dir]/REVIEW_EXECUTIVE_SUMMARY_[YYYY-MM-DD].md`**
   A 1-page (< 600 word) summary for non-technical stakeholders: what the software does, top 3 performance risks, top 3 product opportunities, and the single highest-leverage action to take first.

4. In each file, include a header block:
   ```
   Review conducted: [date]
   Reviewer: Claude Opus (via GitHub Copilot)
   Perspectives: Software Optimization Engineering + Product Management
   Codebase: [project name inferred from repo]
   ```

5. At the end of the roadmap, include a **"Prompt Handoff"** section with a ready-to-use prompt another model can use to implement each workstream, referencing the specific files and line numbers identified in the review.

Begin with Phase 1 reconnaissance now. Think carefully before writing — read broadly before concluding anything.
```

---

## TIPS FOR BEST RESULTS

- **Attach key files explicitly** if Copilot's workspace indexing misses deep paths: drag them into the chat or use `#file:path/to/file`.
- **Run in segments** on very large repos: split into Phase 1–2 and Phase 3–4 in separate prompts, passing the Phase 1 context summary into the second run.
- **Re-run with domain context** if the PM perspective feels generic: add a sentence like *"This is a B2B SaaS product for logistics dispatchers"* before the prompt.
- **Use the Roadmap as input to Claude Code**: the Prompt Handoff section at the end of the roadmap is designed to be pasted directly into a new Claude Code or Copilot session to begin implementation.
- The output files will be placed consistently with your repo's existing docs — check `[artifact_dir]` after the run and commit them like any other design artifact.
