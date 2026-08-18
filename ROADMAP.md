# Eidolon — Roadmap (Sequential Phases)

Phased build plan. Every phase ends with the same gate:

> **Compile (Release, -Wall -Wextra) → C++ unit tests → Python integration tests → run an
> actual autonomous simulation → inspect the event log for anomalous behaviour → fix root
> causes → commit (per AGENTS.md SOP).**

Each phase has explicit acceptance criteria. Nothing is considered done until its gate
passes.

---

## Phase 0 — Repository bootstrap (done in this commit)
- [x] Git repo configured (user `Clark Dale`, `clarkdale123@yahoo.com`, remote `origin` = `git@github.com:cdale11/eidolon.git`)
- [x] Documents: README, DESIGN, ROADMAP, AGENTS (with SOP), MISTAKES, CHANGELOG
- [x] `.gitignore`, `third_party/` (cpp-httplib vendored), CMake skeleton, directory layout
- [x] Verify toolchain: g++ 14.3.1, CMake 3.30.8, ninja, sqlite3 dev headers, conda env `eidolon` (Python 3.12), llama.cpp + GGUF models, ROCm 6.2.1 iGPU runtime
- Gate: `cmake -B build && cmake --build build` succeeds; empty unit-test binary passes.

## Phase 1 — Core runtime skeleton
- RNG (`xoshiro256++` + per-subsystem streams, seedable, persisted)
- Adaptive simulation clock (fine/coarse/sleep step sizes, event queue)
- Serialization primitives (versioned binary snapshot, atomic rename)
- Minimal world: grid, terrain, day/night + weather stats
- Minimal body: energy/hunger/thirst/fatigue/sleepPressure/temperature
- Minimal headless loop: world → body → simple heuristic decision → action → log
- `eidolon-sim --seed N --deterministic --days D` reproduces identical logs
- Gate: deterministic replay test passes; save/load round-trips identical state.

## Phase 2 — Minimal end-to-end organism (first living milestone)
- Perception: sight/hearing radii → compact feature vector (no attention yet)
- Drives → simple action selection (survive, forage, drink, sleep)
- Memory: hot ring of compact episodes (bounded), importance score
- SQLite archive (WAL) + schema versioning
- Web server (`cpp-httplib`) + minimal ChatGPT-like chat UI (sidebar, chat, input, send)
- LLM bridge: `parse` (message → JSON semantics) + `respond` (snapshot → reply), llama.cpp
  compatible endpoint, timeouts, fallback replies when offline
- `eidolon-server` runs sim + UI; browser disconnect does not stop it
- Gate: user can start a conversation; organism replies grounded in real state; killing the
  LLM doesn't stop the sim; save/load preserves the conversation's individual.

## Phase 3 — Learning core (the mind starts)
- ValueNet (TD), ThreatNet (aversive + extinction), policy bandit with temperature
- Attention model (top-k salience), novelty/curiosity, surprise-gated learning
- Neuromodulator couplings (stress→threat learning, valence→encoding, PE→learning bursts)
- Personality latent vector (16-d) updated by life statistics; drive weights evolve
- `Learner` interface + metrics (inference/update counts)
- Gate: seeded test shows repeated scenario → success rate rises; two identical seeds with
  different experiences produce different latent vectors; metrics increment.

## Phase 4 — Memory systems & sleep
- Full episodic encoding (time, location, participants, action, outcome, state, prediction,
  PE, importance, emotional/social relevance)
- Learned retrieval weighting; decay/strengthen/rehearsal; archive + pruning
- Sleep state machine + consolidation pipeline (replay, skill rehearsal, goal processing,
  summarization, association updates)
- Dreams v1 (associative recombination, no LLM)
- Gate: overnight consolidation improves a rehearsed skill; retrieval returns relevant
  episodes; memory DB stays bounded over long runs; sleep occurs at sane intervals.

## Phase 5 — Rich world & wildlife
- Seasons, weather events (rain/storm/heat/cold), temperature coupling
- Plants (edible/toxic/medicinal), regrowth, depletion; water sources
- Wildlife: prey (rabbits) + predators (dogs/wolves) with own drives and fear of the
  organism; hunting/fleeing/attack resolution
- Hazards (cliffs, deep water, disease vectors), infection/immune model, wounds
- Causal chains verified end-to-end (scarcity → exploration → …)
- Gate: in a seeded scarcity scenario the organism explores and finds food; predator events
  produce measurable threat learning; survival rate improves with experience.

## Phase 6 — Skills, tools, crafting, construction
- Skill models (Beta/Bernoulli competence), procedural store, habit formation
- Crafting with learned recipes (seeded basics: fire, sharp stone, spear, shelter)
- Construction: persistent structures on grid (shelter, walls, campfire, storage, farm
  plots), stored/retrieved in snapshot
- Affordance discovery: tool used in unexpected ways → new procedures
- Gate: organism builds a shelter that persists across save/load; discovers at least one
  novel tool use in a seeded run; skill competence improves with practice.

## Phase 7 — Planning & world model
- WorldPredictor (one-step transition MLP) + confidence
- Forward/beam planning over primitives using learned models; replan on surprise
- Goal emergence from drives + state + opportunities (goals not specified by us appear)
- LLM-assisted high-level plan proposals (validated, executed by runtime only)
- Gate: planner outperforms greedy policy in a resource-fetch benchmark; unexpected
  environmental change triggers replanning.

## Phase 8 — Social cognition & learning from the user
- User model: familiarity, trust, affection, fear, respect, resentment, reciprocity,
  expectations (updated by consequences of interactions; may be wrong)
- Wildlife social models (species + individual familiarity/fear)
- Attachment: user absence → attachment pressure; reunion affects state
- Learning from user: verifiable facts → beliefs with confidence; feedback shapes behaviour
- Gate: seeded test — user warnings that prove true raise trust and change behaviour; false
  warnings lower trust; long absence produces measurable attachment response.

## Phase 9 — Self-model, concepts, metacognition
- Self-model: body/abilities, autobiographical summary, preferences, beliefs, goals,
  reputation, future expectations — all experience-updated
- Metacognition: uncertainty, confidence, self-prediction, failed-prediction recognition →
  reflection triggers
- Concept formation: incremental clustering in embedding space, expandable ontology,
  LLM-assisted naming (rare)
- Gate: organism forms and names a concept it was never told about; self-model changes after
  significant events; it reports uncertainty honestly in conversation.

## Phase 10 — Dreams v2, reflection, narrative language
- Dreams influence associations measurably (tests)
- Slow layer reflection with LLM (rate-limited): life review, summary of changes
- "What happened while you were away" grounded in the actual event timeline
- Gate: conversation asks about a real past event → accurate details; asks about an
  unrecorded event → honest uncertainty (no fabrication, tested).

## Phase 11 — Performance & long-run stability
- Profiling: hot-path allocation audits, adaptive-clock tuning
- Benchmarks: 30 sim-days headless target < ~10 min; fine tick p50 ≤ 2 ms; snapshot ≤ 100 ms
- Long-run tests: weeks of simulated time, RSS flat, archive bounded, no drift in metrics
- Observability polish: /api/metrics complete, headless --stats, --bench report
- Gate: all performance budgets met; long-run memory stability test green.

## Phase 12 — Full test matrix & release
- Complete test suite per DESIGN §18 (unit, seeded replay, behavioural scenarios,
  adversarial: corrupt save, kill -9 during save, LLM garbage)
- Documentation finalization; CHANGELOG entries; performance report
- Optional: teacher-training pipeline (NVIDIA NIM) documented and wired into conda env
  `eidolon` tooling (CPU PyTorch; iGPU experimental — see AGENTS.md hardware note)
- Gate: full `ctest` + integration suite green on a clean checkout.

---

### Cross-cutting rules for every phase
- No LLM in the hot path. Ever.
- No personality/biography in prompts — ever.
- Memory, context and model sizes stay bounded; every phase re-verifies memory stability.
- All stochastic behaviour seedable; important tests use fixed seeds.
- All Python tooling runs in conda env `eidolon`; C++ runtime never calls Python.
- Commit at every step (AGENTS.md SOP).
