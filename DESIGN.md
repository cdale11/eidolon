# Eidolon — Consolidated Design Document

This document consolidates the project brief ("what we want from this simulation"), the
architectural decisions derived from it, and the concrete implementation details for every
subsystem. It is the single source of truth for how the organism works. The roadmap lives in
`ROADMAP.md`; operational rules for agents in `AGENTS.md`.

---

## 1. Vision

Eidolon is a **small artificial organism that happens to possess language** — not an NPC with
a large prompt. It must continue living, perceiving, learning, adapting, forming memories,
developing personality, acquiring skills, building things, forming relationships and changing
psychologically **while the user is offline**. When the user returns, they chat with the same
persistent individual and learn what happened to it.

The central loop:

```
WORLD ↔ BODY ↔ NEURAL STATE ↔ LEARNING ↔ MEMORY ↔ COGNITION ↔ ACTION ↔ WORLD
```

### Non-negotiables

1. The organism exists independently of the player. The UI is a client, not the host.
2. **Never** encode personality, biography, trauma, beliefs or goals as prompt text. They must
   emerge from persistent internal state, learning and experience.
3. The LLM is **never** the organism's source of truth or persistent mind.
4. Maximize AI/ML usage while minimizing LLM usage. Small CPU-efficient neural networks and
   associative/statistical learning carry the mind; the local LLM only does natural language
   and occasional hard language-heavy cognition.
5. Fully functional with the LLM unavailable. No LLM per tick. No LLM for ordinary decisions.
6. C++ for the runtime; Python only for offline training/tooling (conda env `eidolon`).
7. Bounded memory, bounded context, bounded model sizes. ~8 GB RAM envelope, CPU-first.
8. Genuine stochasticity, learned adaptation, no fixed script; seedable for debugging.
9. A world that produces genuine pressure: scarcity → hunger → exploration → discovery →
   tool use → construction → depletion → adaptation. Not decorative.
10. The organism may pursue goals nobody specified.

---

## 2. Architecture Overview

```
┌──────────────────────────── C++ runtime (single process) ───────────────────────────┐
│                                                                                     │
│  ┌─────────┐  ┌──────────┐  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  WORLD  │─▶│PERCEPTION│─▶│   NEURAL    │─▶│  COGNITION   │─▶│    ACTION     │──┼─▶ WORLD
│  │ grid,   │  │ senses,  │  │  STATE      │  │ fast/interm. │  │ primitives,   │  │   (state
│  │ weather,│  │ attention│  │ neuromods,  │  │ drives,      │  │ planner,      │  │   change)
│  │ flora,  │◀─│ salience │◀─│ value,      │◀─│ goals,       │◀─│ execution,    │  │
│  │ fauna   │  └────┬─────┘  │ threat,     │  │ self-model   │  │ skill use     │  │
│  └─────────┘       │        │ policy      │  └──────┬───────┘  └───────────────┘  │
│                    │        └─────┬───────┘         │                             │
│                    │              ▼                 ▼                             │
│                    │       ┌────────────┐    ┌───────────┐   ┌──────────────┐      │
│                    └──────▶│  LEARNING  │◀──▶│  MEMORY   │◀──│  SLEEP/      │      │
│                            │  (all      │    │ hot ring +│   │  CONSOLIDATION│      │
│                            │  online)   │    │ archive   │   └──────────────┘      │
│                            └────────────┘    └─────┬─────┘                         │
│                                                    │                               │
│  ┌──────────────┐  ┌─────────────┐  ┌───────────────┐│                              │
│  │  PERSISTENCE │  │   SERVER    │  │    LLM BRIDGE ││  (language only)            │
│  │ binary + SQL │  │ REST + WS   │──│ provider,     │◀── llama.cpp / NIM           │
│  │ atomic, vers.│  │ chat UI     │  │ snapshot,     │                              │
│  └──────────────┘  └─────────────┘  │ semantic I/O  │                              │
│                                     └───────────────┘                              │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

### Processes & modes

| Mode | Entry point | Purpose |
|---|---|---|
| Headless | `eidolon-sim` | Long-running autonomous simulation, no UI, `--days N`, stats reporter |
| Server | `eidolon-server` | Same engine + embedded HTTP/WS server for the chat UI |
| Bench | `eidolon-sim --bench` | Performance / long-run stability harness |
| Replay | `--seed N --deterministic` | Reproducible runs for debugging and tests |

The simulation engine is one library (`libeidolon`); `eidolon-sim` and `eidolon-server` are
thin front-ends.

---

## 3. Simulation Clock & Lifecycle

- Base unit: **1 simulated second**. Internal simulated clock advances independently of wall
  time; a run of `--days N` advances N simulated days as fast as the CPU allows.
- **Adaptive tick**: the engine picks a step size per tick:
  - `1 s` — organism active, high arousal, event queue non-empty, combat, construction.
  - `5–30 s` — routine activity (foraging, walking, resting).
  - `30–60 s` — sleep, low-arousal idle, long offline stretches.
- Trigger-driven micro-batches: when a meaningful event fires (thirst spike, predator near,
  weather change, user message) the step size collapses to fine and the event is processed.
- **Offline catch-up**: on load, unprocessed wall-clock time is advanced in chunks (e.g.
  15 sim-minutes per chunk) with an autosave between chunks; event-rich periods are replayed
  at fine resolution, routine stretches coarsely. The timeline of notable events is preserved.
- **RNG**: `xoshiro256++`, seeded by entropy by default; `--seed N --deterministic` fixes the
  seed and disables entropy mixing. Per-subsystem streams (body, world, learning, memory,
  language) derived via `splitmix64` so one subsystem's randomness doesn't disturb another's
  replay. RNG state is persisted so loads continue the same stream.

### Tick structure (hot path)

1. Advance weather/time/season (cheap).
2. Process event queue (bounded ring).
3. Perceive → attention → sensory feature vector.
4. Update physiology (Euler, fine if active).
5. Update neuromodulators & derived quantities.
6. Fast layer: reflexes, habits, drives → candidate action bias.
7. Intermediate layer (throttled): goal re-evaluation, planning, prediction.
8. Execute action → world mutation → consequences → rewards/errors.
9. Learning updates (throttled per-model).
10. Memory write (if event worth recording).
11. Sleep/consolidation when sleep pressure forces it.

Hot path is allocation-light: pre-allocated pools, ring buffers, per-tick arena, no heap
churn, no dynamic dispatch in the innermost loops.

---

## 4. World

A **tile grid** (default 128×128; configurable) with spatial hashing for entity lookup.

| System | Contents |
|---|---|
| Terrain | plains, forest, hills, water/river, lake, desert, caves; walkability, food yield, cover |
| Weather | day/night cycle, seasons (temp curves), rain, snow, storm, heatwave, cold snap; affects body and foraging |
| Plants | edible plants, fruit trees, wood sources, medicinal herbs, toxic plants; regrowth, seasons |
| Animals (wildlife) | small game (rabbits), predators (dogs/wolves), birds; autonomous agents with own drives (hunger, fear of organism), reproduction, migration |
| Resources | water (river/lake/rain), stone, wood, clay, flint, plant fiber |
| Food chain | plant → prey → predator; predation risk gives the organism genuine fear-relevant events |
| Hazards | cliffs, deep water, storms, cold, predators, disease vectors, poisonous plants |
| Disease | infection model (immune system vs. pathogen load), contaminated water, wound infection |
| Persistent objects | shelters, walls, campfires, tools, crafted items, stored food, farm plots — the organism can build and the world remembers |
| Causal chains | scarcity → hunger → exploration → discovery → tool use → construction → resource depletion → adaptation |

Design notes:
- The world is **not decorative**: every element couples to body state or learning.
- Wildlife exists so the organism perceives a living world; wildlife is the only other
  autonomous population (per decision: one human replica, no other humans).
- Resource depletion and regrowth cycles force recurring adaptation, which drives learning
  updates and keeps the organism's behaviour from stagnating.
- Crafting recipes are not all hardcoded: many are discovered by experimentation
  (combination + outcome → learned association). A catalog of "known-for-certain" recipes
  seeds only the most basic survival ones (fire, sharp stone, etc.).

---

## 5. Body (Physiology)

Compact float variables, each with homeostasis setpoint, decay, and effect on drives:

energy, hunger, thirst, hydration, fatigue, sleepPressure, bodyTemperature,
health, pain, injury (per-body-region), woundInfection, immuneLoad, immuneStrength,
bloodLoss, toxin, metabolism (basal rate), chronicCondition (rare, emergent).

- Thermoregulation couples to weather/clothing/shelter/fire.
- Injury is location-specific and heals over time; healing rate depends on health, food,
  sleep, medicine.
- Immune model: immuneLoad (pathogen load from dirty water, wounds, disease) vs
  immuneStrength (trained by exposure; short-term inflammation costs energy).
- Metabolism scales with activity, temperature, injury, immune response.
- Sleep: sleepPressure accumulates; the organism sleeps when pressure crosses its learned
  threshold (affected by stress, safety, comfort). Sleep restores energy, heals, and gates
  consolidation (see §11).

---

## 6. Neural State (Neuromodulation Layer)

Continuous variables that gate everything else:

| Variable | Role |
|---|---|
| arousal | alertness; sets tick resolution, attention breadth, reaction speed |
| valence | hedonic tone; biases memory encoding, social perception, exploration |
| stress | cortisol-like; gates threat learning rate, decision horizon, risk aversion |
| reward | latest RPE (reward prediction error) signal for TD learning |
| threat | current threat estimate (from ThreatNet × percepts) |
| curiosity | novelty-driven motivation to explore |
| novelty | surprise at unfamiliar percepts |
| uncertainty | model uncertainty / entropy; drives information seeking |
| attention | distribution over percept channels (where senses point) |
| motivation | drive-weighted action urgency |
| predictionError | global PE; spikes trigger learning bursts and memory encoding |

Effects implemented in code (not text): high stress narrows attention to threat channels and
accelerates threat learning; high arousal enables fine ticks; negative valence strengthens
episodic encoding; low uncertainty reduces exploration bonus; predictionError spikes gate
TD updates and memory salience. These couplings are explicit, measurable and testable.

**Temperament priors** (initialized once, small in number, no finished personality):
rewardSensitivity, threatSensitivity, noveltySensitivity, socialSensitivity, impulsivity,
persistence, attachmentSensitivity, stressReactivity. They modulate learning rates, discount
factors, drive weights and risk posture at birth, and they are themselves slowly adjusted by
experience (see §12 personality latent).

**Intrinsic drives** (homeostatic + hedonic, *not scripted goals*): survival, energy, safety,
exploration, novelty, affiliation, attachment (to the user), competence, curiosity, and
optionally status/achievement (a self-model-derived drive that can emerge when competence and
social evaluation history support it). Their relative strengths live in `DriveWeights` and
evolve through experience. Goals emerge from drives + internal state + environmental
opportunities + learned predictions (§9); the organism may pursue goals nobody specified.

---

## 7. Perception & Attention

- **Not omniscient**: line-of-sight radius + hearing radius + smell radius (scent field).
- Perception produces a **compact multimodal feature vector** per tick:
  entity class × distance × direction × size × motion × familiarity; ambient weather/season/
  time; ground type; water proximity; sound events; body-state summary (internal percepts).
- **Learned attention**: a small linear/scoring model over candidate percepts
  (top-k, k ≤ 8) selects what enters cognition. Attention is influenced by arousal
  (breadth), stress (threat bias), drives (need-directed search: hungry → food cues
  upweighted) and novelty.
- Senses have costs: sustained high arousal perception drains energy; active scanning is a
  deliberate action.
- The full world is never serialized into any LLM context; only the attended feature vector
  and retrieved, condensed memory summaries cross the language boundary.

---

## 8. Learning Systems (the actual mind)

Several **small, specialized, online-updatable** models. All persist. All update continuously
during life; none require retraining the LLM. Parameter counts are bounded (total learned
weights target ≈ 200–600 k floats across all models; ≤ a few MB).

| Model | Size (approx) | Type | Updates | Job |
|---|---|---|---|---|
| ValueNet | MLP 3×32 | MLP (TD) | every reward | predicts value of states/contexts; RPE source |
| ThreatNet | MLP 2×24 | MLP | aversive/safe events | threat probability per context; sensitization & extinction |
| Policy | per-action linear | contextual bandit | after every action | action preferences; softmax temperature from impulsivity/uncertainty |
| Attention | linear top-k | online | after outcome | what to perceive next |
| WorldPredictor | MLP 2×24 | online | every action | one-step next-state delta prediction; planning substrate; confidence from error history |
| SkillModels | Beta/Bernoulli | online Bayes | per attempt | success probability + efficiency per skill+context; competence is learned, not RPG stats |
| HabitTable | associative freq | online | per action | state→action frequency; decays; biases fast layer |
| MemoryRetriever | linear scorer | online | on retrieval usefulness | importance × recency × emotional × cue-match weighting |
| SocialModels | online linear per agent | online | per interaction | familiarity, trust, affection, fear, respect, resentment, reciprocity, expectation of other's behaviour (may be wrong) |
| PersonalityLatent | 16-dim vector | slow online | daily | slowly drifts with life statistics |
| DriveWeights | small vector | slow online | periodic | relative drive strengths evolve |
| ConceptSpace | embedding 32-dim + online clustering | online | on concept events | incremental concept formation (stream k-means / resonance) |

Principles:
- **Prefer the simplest model that produces the emergent behaviour.** A frequency table beats
  an MLP for habits; a Beta posterior beats gradient descent for success rates; an online
  linear model beats an MLP for attention.
- **Quantized where appropriate**: models that do not need float precision may be run in
  int8/fixed-point (compiled variants and WASM/WebGPU paths); exact parity is not required,
  only bounded behavioural divergence — the organism is adaptive by design.
- **Tiny recurrent cells are allowed** where temporal structure matters (e.g. short-term
  state summaries feeding WorldPredictor or the policy); an RNN is only preferred when a
  feed-forward or tabular model measurably fails.
- **Online learning rates** are small and individually configurable; learning is gated by
  arousal/predictionError (surprise-gated, salience-gated).
- **Architecture allows swapping models** behind a `Learner` interface; each learner reports
  update counts for observability.
- **Associative learning** produces fear, avoidance, sensitization, habituation/extinction,
  attachment and trauma-like biases *naturally*: high-arousal negative events strengthen
  threat associations; repeated safe exposure extinguishes them; no trauma flags, no scripted
  stories.

### Reinforcement-learning framing

The agent is a contextual bandit with TD value learning and model-based planning layered on
top:
- Reward function is intrinsic: homeostatic improvement (hunger↓, threat↓, novelty↑,
  competence↑, social contact with user) plus negative shocks (pain, fear, cold).
- `Policy` proposes; `ValueNet` criticizes; `WorldPredictor` enables imagined rollouts;
  `ThreatNet` vetoes/urges flee; habits shortcut routine states; planning (below) overrides
  when uncertainty is high and stakes are high.

---

## 9. Cognition Layers

### Fast layer (per fine tick)
Physiology-driven reflexes (flee on sudden threat, flinch on pain), habits (automatized
state→action), drive urgency, learned value/action models, attention. Majority of behaviour
lives here.

### Intermediate layer (throttled, e.g. every 2–10 s or on events)
- Goal selection: goals emerge from drives + state + learned opportunities; a goal has a
  structured representation and a priority from expected value.
- Planning: forward/beam search over action primitives (≤ ~200 node expansions) using
  WorldPredictor + ValueNet; replan on surprise.
- Memory retrieval for context; social reasoning via SocialModels; prediction of outcomes.

### Slow layer (minutes–days of sim time, or on demand)
- Reflection: reviews recent prediction failures and goal outcomes; updates self-model,
  beliefs, drive weights, personality latent.
- Abstraction: concept formation passes; summarization of experiences into autobiographical
  beliefs (statistics, not raw logs).
- LLM-assisted reasoning (rare): complex language-heavy cognition (e.g. "the user said X,
  what might that imply about my situation?"), high-level plan *proposals* — always validated
  and converted to structured actions by the runtime. The LLM never modifies world state.

---

## 10. Action Primitives & Planning

A fixed, structured action set (the organism's body and culture):

move, rest, sleep, wake, drink, eat, forage, gather(wood/stone/clay/flint/fiber), hunt,
fish, flee, hide, fight, explore(region), observe(target), craft(recipe,materials),
build(structure,pos), plant, harvest, tend(fire/shelter/crop), repair, store/retrieve,
make-tool, mend(self), seek-shelter, avoid(hazard), rehearse(skill), recall(memory),
socialize-with(user) [via language], think/reflect [LLM-assisted, rare].

- Each primitive: preconditions, cost (time/energy), success distribution (from SkillModels),
  world effects, reward signals.
- **Planning** uses learned models only. The LLM may occasionally propose high-level plans;
  the runtime translates/validates into executable primitives. Novel combinations arise from
  planning + affordance generalization, e.g. learning a sharp stone cuts branches → "flint
  spear"; a stick + fire → torch. Procedures learned once are stored procedurally.

---

## 11. Memory Systems

| System | What | Where |
|---|---|---|
| Episodic | events: time, location, participants, observations, action, outcome, internal state, prediction, predictionError, importance, emotional/social relevance | hot ring (≤ 4096) → SQLite archive |
| Semantic | facts & statistics distilled from episodes (water west, predators at dusk, user returns at evening) | SQLite + concept space |
| Procedural | learned skills, habits, recipes, routines | skill models + habit table + procedure store |
| Autobiographical | life summary: timeline of milestones, identity narrative (emergent) | SQLite, summarized |
| Emotional | valence-laden memory traces, association strengths | hot ring weighting + ThreatNet/association links |
| Social | per-agent models + interaction history | SocialModels + SQLite |

- **Encoding**: events are compact feature vectors; embeddings (32-d) stored with episodes;
  learned retrieval weighting: importance × recency × emotional salience × goal relevance ×
  cue match.
- **Decay & strengthening**: memories fade unless re-retrieved (spaced rehearsal); successful
  retrievals strengthen; interference between similar traces; reconstructive processes during
  sleep.
- **Never send the memory database to the LLM.** Only top-k (k ≤ 12) retrieved summaries in
  the snapshot, plus archive-derived statistics.
- **Bounded active memory**: hot ring capped; overflow consolidates then archives detail to
  disk; old low-importance detail is pruned after summarization.
- **Dreams** (sleep, no LLM): retrieved fragments + current state are associatively
  recombined; the recombination strengthens/destabilizes associations (this is where
  overnight insight emerges), and occasionally produces a summary sentence when language is
  available.

---

## 12. Personality, Self-Model, Concepts, Social

### Personality
- Starts from biological priors only (see §6). Representation: the slow `PersonalityLatent`
  vector updated by life statistics (reward history, threat history, social history, novelty
  history). Two initially identical organisms diverge under different experiences. No textual
  personality anywhere.

### Self-model
Persistent structure: body & ability model, autobiographical summary, preferences (from
valence history), beliefs (proposition store with confidence), goals, reputation with user,
expectations about the future (from WorldPredictor). Metacognitive variables: uncertainty,
confidence, self-prediction (did I do what I predicted?), failure recognition → triggers
reflection. The self-model changes as a consequence of experience — identity is developed,
not assigned.

### Concepts
- Recurring perceptual patterns → incremental online clustering in concept space
  (stream k-means / resonance with novelty threshold). Expandable ontology stored in SQLite.
- Concepts carry **learned relationships** (edges in the concept graph: predator→fear,
  dawn→safe-forage, tool→crafting), formed and strengthened by co-occurrence and outcome
  associations; relationships decay and are re-weighted like memories.
- When language is available, the organism may **name** concepts (LLM-assisted naming
  ceremony, rare); names persist; no hardcoded full ontology — only seed concepts for
  fundamental classes (self, food, water, threat, shelter, user).

### Social cognition (no other humans)
- **User model**: familiarity, trust, affection, fear, respect, resentment, reciprocity,
  expectations about the user (when they return, what they ask, which advice helped).
  Updated by chat interactions and their consequences (e.g. user warns of danger →
  danger materializes → trust+; user ignores → trust−). Relationships affect attention,
  memory, motivation, decisions. The model is learned and may be wrong.
- **Theory of mind**: beliefs about what others believe — e.g. the user's beliefs about the
  organism, or a predator's beliefs about where prey hides — represented as nested,
  low-confidence belief propositions, tested by experience and corrected on mismatch.
- **Wildlife models**: per-species and per-individual familiarity/fear/valence; predation
  events shape them. Rudimentary expectation of other agents' behaviour.
- **Learning from the user**: conversation teaches the organism about the world (facts it
  verifies or files as beliefs with confidence); behaviours and preferences can be shaped by
  user feedback, all through the same learning systems.

---

## 13. Sleep, Consolidation, Dreams

When sleepPressure passes threshold: sleep state machine (awake → drowsy → deep → REM →
wake). During sleep the clock runs coarse (30–60 s ticks) and consolidation runs in
micro-batches:

1. Replay hot-ring episodes through learning systems (surprise-gated).
2. Strengthen important associations, decay irrelevant ones.
3. Consolidate skills (rehearsal), update procedural store.
4. Promote important memories to long-term archive; summarize old ones.
5. Process unresolved goals (drop/update/re-prioritize).
6. Update self-model statistics and drive weights.
7. Dreams: associative recombination; possible dream summary when language available.
8. On wake: restore energy/health, clear fatigue.

Consolidation is bounded per night (caps processing time); no LLM calls in the cycle.

---

## 14. Language (LLM Bridge)

### Provider interface
- OpenAI-compatible HTTP client (`POST /v1/chat/completions`, streaming SSE support) so any
  compatible server works: local `llama.cpp` `llama-server` (default), or any endpoint.
- Default local model: `Qwen3-4B-Instruct-Q4_K_M.gguf` (present in the local llama.cpp
  build tree). Configurable via `eidolon.toml` / CLI.
- Teacher training / distillation for offline tooling may use NVIDIA NIM cloud models
  (Python, conda env `eidolon`); never in the runtime path. Teacher output is baked into
  **frozen artifacts** — an `.eprp` policy prior (`--policy-prior`, `Policy::loadPrior`),
  reward-tuning scenario suites, and optional LoRA adapters for the local 4B — so replays
  stay deterministic. Baked priors are only an *initialization*: every small model keeps
  updating online over the prior (see §8), which is what preserves adaptability,
  emergence and sentience rather than a fixed "scripted" policy.

### Call types (rare, bounded)
1. **parse** — user message → structured JSON semantic representation: intent, referenced
   objects/concepts, emotional tone, requests. ≤ 512 output tokens. Called once per user
   message.
2. **respond** — cognitive snapshot → grounded natural language reply (may include validated
   action suggestions). ≤ 1024 tokens. Called once per user turn; optional reflective calls
   at slow layer are rate-limited.
3. **name/reflect** — rare slow-layer calls (concept naming, high-level reflection,
   high-level plan proposals), heavily rate-limited.

### The cognitive snapshot (what the LLM sees — never the whole mind)
Current conversation turn, semantic interpretation, top-k retrieved memories (≤ 12,
compacted summaries), current physiological/neural summary, active goals, relevant beliefs,
relationship summaries, concise self/world summary. Sized to stay small (target ≈ 1–2 k
tokens). The LLM is instructed to answer only from the snapshot and to admit uncertainty;
the runtime checks responses for consistency.

### Grounding & safety
- LLM output is **parsed and validated**; any world mutation must come as a structured action
  request executed by the runtime. The LLM can never directly modify world state.
- LLM failure (timeout, down, garbage) → deterministic fallback replies generated from state
  ("I'm tired..." from body state), organism keeps living normally, and the failure is
  logged/measured.
- **No fabrication**: if asked about events that aren't in memory, the reply is honest
  uncertainty (fallback or LLM-with-snapshot both enforce this).

---

## 15. Persistence

### Hybrid scheme
- **Binary snapshot** (`save.snap`, atomic via write-temp + fsync + rename, checksummed,
  versioned header): world, body, neural weights, personality latent, skills, beliefs, goals,
  self-model, relationships, RNG streams, sim clock. Compact (< ~10 MB typical).
- **SQLite** (WAL mode, `memory.db`): archived memories, event timeline, conversations,
  concepts, schema version table. Migrations run on version mismatch.
- Autosave: on significant events (sleep onset, milestone), on a sim-time interval, at exit,
  and between offline catch-up chunks. Never serialize the full organism per tick.

### Restore guarantees
- Load restores the same individual, same learned parameters, same world, same clock.
- Schema versioning + migration path; corrupt/absent files → clear error, never silent reset.

---

## 16. Web Server & UI

- `eidolon-server`: embedded HTTP server (vendored `cpp-httplib`) + WebSocket for streaming
  replies. Endpoints:
  - `GET /` — chat UI (vanilla JS/CSS, no framework).
  - `GET/POST /api/conversations`, `/api/messages` — conversation persistence.
  - `POST /api/conversations/new` — start a new chat (conversation titled from its first
    message).
  - `POST /api/conversations/delete` — remove an old chat and its messages.
  - `POST /api/world/reset` — spawn a fresh world + brand-new organism (entropy seed unless
    a `seed` is given; applies the policy prior if configured).
  - `POST /api/send` — user message → organism response (streamed).
  - `GET /api/status` — compact status (sim time, awake/asleep, mood summary).
  - `GET /api/metrics` — observability (see §18).
  - `GET /api/debug/{status,memories,goals,relations,state}` — expandable debug panels
    (optional, dev-facing).
- UI: light theme, ChatGPT-style; left sidebar of conversations (new chat, delete chat,
  restart-world), centered message column with avatars, auto-growing input. No bundler;
  the browser is disposable (server keeps sim).
- `eidolon-sim` headless: same engine, no server, `--stats` reporter to stdout.

### LLM response parsing (reasoning models)
- The OpenAI-compatible client must survive reasoning-enabled models (DeepSeek, Nemotron,
  Qwen3, …) that emit a long chain-of-thought in `reasoning_content` and/or at the start of
  `content`. Rules: the structured answer is the **last balanced JSON object** in `content`
  (teacher labels, message parse); chat replies strip fence/thinking wrappers and cap
  length; `reasoning_content` is never used as the answer and is never surfaced to the user
  or stored as a memory.

---

## 17. Client-First Compute Architecture (Portable Engine)

**Critical invariant: the server is NOT required for every thought, action, simulation tick,
neural update or memory operation.** The client performs the maximum amount of work it can
support. The server provides persistence, synchronization, session management, recovery and
(optionally) LLM inference — and guarantees continuous life via a native headless engine when
no capable client is attached.

### Platform layer (clean separation, no browser APIs in the core)

```
ReplicaCore  (the whole organism: world, body, neural, learning, memory, cognition,
              planning, persistence logic — 100% platform-independent C++)
  → NativeBackend          (PC/server: pthreads, filesystem, native GPU)
  → WebAssemblyBackend     (browser: Workers, SharedArrayBuffer, WASM SIMD, WebGPU)
  → future ConsoleBackend  (native Xbox: exposes console CPU/GPU without touching the organism)
```

- The core depends on a thin `Platform` interface (threads, storage, time, RNG entropy,
  optional GPU/ML backend). Nothing in the simulation, organism, neural systems, memory,
  learning, world, planning or persistence layers depends on browser APIs.
- Native and WASM executions use **the same core logic and the same state schema**, so the
  organism remains the same individual regardless of where it runs.
- Simulation logic is never implemented in JavaScript and is never coupled to the frontend.

### Capability detection & ComputeProfile

On startup the client detects: WASM SIMD, Web Workers, SharedArrayBuffer (COOP/COEP
permitting), WebGPU (and WebGL fallback), hardware concurrency, available memory, storage
capacity. The result is a `ComputeProfile`; the engine **automatically selects the best
backend** from this hierarchy:

1. WebGPU — massively parallel neural/ML workloads (when available; never assumed).
2. WASM SIMD + Web Workers — CPU workloads.
3. Plain WASM — fallback.
4. Server-side computation — only when the client cannot safely/efficiently run the workload.

### Workload partitioning & worker separation

Independently schedulable workloads: world, physiology/cognition, neural/ML inference,
memory/consolidation, planning, social simulation. Typical worker layout:

```
Browser
├── Chat/UI main thread (never runs heavy simulation)
├── Replica WASM core
├── Web Workers
│    ├── world
│    ├── physiology/cognition
│    ├── neural/ML
│    └── memory/consolidation
├── WebGPU ML backend when available
└── local persistent cache (IndexedDB/OPFS, compact binary)
```

- Workers communicate via SharedArrayBuffer or compact message passing; large buffers are
  never copied between workers.
- Do **not** spawn workers for tiny workloads — worker overhead is benchmarked and batching
  strategies are chosen dynamically.
- A `ComputeScheduler` maintains priority queues: interactive chat and organism
  responsiveness first, then active simulation, then background consolidation/learning;
  heavy background work yields to UI responsiveness.

### Adaptive fidelity

The same organism runs at different fidelity levels: a powerful PC runs high-frequency
simulation, larger model budgets, more detailed perception, more wildlife; a constrained
client automatically reduces simulation frequency, neural model sizes and world-detail
fidelity. **Persistent identity must remain compatible across fidelity levels** (same state
schema, same individual; only detail/frequency scale).

### Synchronization (checkpoint/delta, never tick streaming)

- The client periodically sends **compact state deltas**: changed physiology, learned-model
  weight deltas or snapshots, new memories, modified beliefs, relationships, skills,
  concepts, important world events, simulation time. Batched and compressed. The server
  persists them.
- The server does not receive every simulation event and never continuously streams ticks.
- Offline client execution: if connectivity is lost, the organism keeps running locally;
  on reconnect the client reconciles and uploads state. For the single-user organism,
  **client-authoritative cognition and learning** is the default; the server validates
  structural consistency rather than reproducing neural calculations.
- **World-state authority is configurable**: client-authoritative for private/single-user
  runs; server-authoritative world (cognition stays client-side) for future shared-world
  deployments. Neither model is hardwired.

### Client-side checkpointing

The browser periodically persists local state (IndexedDB/OPFS) using **compact binary
serialization** (never JSON for simulation state), so a tab crash does not destroy the
organism. Human-readable diagnostic exports are separate.

### Server roles

```
Server
├── authentication/session
├── persistent Replica storage
├── synchronization/checkpoints
├── optional LLM endpoint
└── native headless fallback (unattended simulation when no client is attached)
```

The server runs the same headless native engine as fallback, for testing and for
unattended periods. Language processing falls back to server-side LLM inference when the
client has none; browser-side local LLM inference is an optional provider, never a hard
dependency.

### UI contract

The UI communicates with the engine through a **compact API**; raw world state is never
sent to the frontend. The ChatGPT-like frontend stays lightweight and responsive while the
organism simulation runs independently wherever it runs.

### Xbox policy

No undocumented hardware access, no sandbox bypasses, no platform hacks — the Xbox browser
can only use capabilities legitimately exposed to web applications; capability detection
automatically gives it the maximum compute path its browser actually exposes (WASM SIMD →
plain WASM → server fallback; never degrade the core simulation merely because GPU
acceleration is unavailable). The architecture keeps a future native Xbox client possible by
reusing the exact same `ReplicaCore` through `ConsoleBackend` without organism changes.
Actual browser capabilities are measured and benchmarked, never assumed.

### Profiling & backend benchmarks (from the beginning)

- Per-backend profiling: simulation steps/sec, simulated hours/sec, neural inferences/sec,
  memory processing latency, CPU time, GPU time where available, WASM memory usage, worker
  utilization, synchronization bandwidth, LLM calls. Shown in a compact developer
  diagnostics panel in the UI.
- A benchmark suite runs the same scenario on native C++, WASM CPU, WASM SIMD, WASM
  multithreaded and WebGPU (where available) and compares throughput/memory/latency; the
  system automatically picks the fastest stable backend for the device.
- No premature optimization on assumed hardware: measure first, then tune.

---

## 18. Observability & Performance

### Metrics (`GET /api/metrics` / `--stats` / `--bench`)
simulated time, wall time, tick counts by step-size class, avg tick time (p50/p95),
CPU%, RSS, neural inference counts, learning update counts, LLM calls + latency + failures,
active goals, memory counts (hot/archive), model sizes (bytes), world event counts
(weather/predation/construction/fights), snapshot sizes & save times.

### Performance budgets (targets)
- Idle/sleep CPU: < 5% of one core; active moments < 40%; 8-core / ~6 GB RAM envelope.
- Fine tick ≤ 2 ms p50; snapshot ≤ 100 ms; boot ≤ 1 s.
- Total learned weights ≈ 200–600 k floats; hot memory ring bounded (≤ 4096 episodes);
  archive prunes detail; RSS growth flat over months of simulated life (tested).
- Benchmarks: 30 simulated days headless in < ~10 min wall (target); memory stable across
  weeks of simulated time; `--bench` reports all of the above.

---

## 19. Testing Strategy

Test levels (all in `tests/`; C++ unit tests with the embedded minimal harness, Python
integration drivers in conda env `eidolon`):

1. **Unit** — physiology invariants, homeostatic recovery, TD convergence on toy MDP,
   ThreatNet extinction, Beta skill updates, memory decay curves, snapshot round-trip,
   migration, RNG replay determinism, planner sanity, concept clustering, clock adaptivity.
2. **Integration (seeded replay)** — deterministic runs with fixed `--seed`: expected event
   sequences reproduce bit-for-bit.
3. **Behavioural scenarios** (autonomous, seeded): offline autonomy (runs N days with no
   input), learning/adaptation (repeated scenario → success rate rises), personality drift
   (two identical seeds, different experiences → different latent vectors), social learning
   (user warnings change behaviour), skill learning, construction (shelter built persists),
   environmental pressure (scarcity → exploration), sleep consolidation (skills/memories
   improve overnight), LLM failure (provider down → organism continues, honest replies),
   save/load identity (same individual, same fears), long-run memory stability (month of sim
   time, bounded memory), performance (`--bench` budgets).
4. **Client-compute & sync** — WASM build parity with native (same scenario → same
   individual state); capability detection → correct ComputeProfile for mocked profiles;
   ComputeScheduler priority behaviour; offline client execution then reconnect →
   deltas reconcile without loss or duplication; client-authoritative vs
   server-authoritative world modes; browser checkpoint survives simulated tab crash;
   delta/compression bandwidth bounded; server headless fallback continues unattended life.
5. **Backend benchmark suite** — same scenario on native / WASM CPU / WASM SIMD / WASM MT /
   WebGPU (where available): throughput, memory, latency; automatic backend selection picks
   the fastest stable one for the measured profile.
6. **Adversarial** — corrupted save → clean error; kill -9 during save → previous snapshot
   intact; provider returning garbage → fallback path; sync from divergent client states →
   structural validation rejects or cleanly merges.

Every phase ends with: compile → unit tests → integration tests → **run an actual
autonomous simulation and inspect the log for anomalous behaviour** → fix root causes →
commit.

---

## 20. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| LLM becomes the mind | Strict call budget, snapshot-only contract, no world mutation, offline tests |
| Memory bloat | Bounded ring, archive + pruning, long-run tests |
| Model explosion | Per-model size budget, simplest-model-first rule, metrics on bytes |
| Simulation too slow | Adaptive clock, coarse sleep ticks, per-tick arena, benchmarks at every phase |
| Emergent behaviour too chaotic to debug | Seedable streams, replay tests, event timeline log |
| Personality written by accident | No personality in prompts; PersonalityLatent is data only |
| Fabricated memories | Retrieval-grounded replies; honesty fallback; tests assert no fabrication |
| World feels decorative | Every world element coupled to physiology/learning; scarcity chains tested |
| WASM path drifts from native behaviour | Same core code + same state schema; parity tests per §19.4; fidelity scaling affects detail, not identity |
| Sync conflicts / lost offline progress | Client-authoritative cognition; structural validation; deltas with sim-clock ordering; reconcile tests |
| Browser tab crash destroys the organism | Periodic client checkpoints (IndexedDB/OPFS, binary); server headless fallback continues unattended life |
| WebGPU assumed everywhere | Capability detection; WebGPU never a hard dependency; WASM SIMD / WASM / server hierarchy |
| Premature optimization on assumed hardware | Measure-first rule; per-backend benchmark suite; auto-select fastest stable backend |

---

## 22. Deterministic Content & Substrate Systems

Beyond the neural learner (§8) and the LLM bridge (§14), a family of **deterministic,
seedable, allocation-light generative systems** enrich the world, the organism's
experience and the offline tooling. None of them run the LLM, none break the
bit-exact determinism contract (`--seed N --deterministic`), and all are bounded.

### Why deterministic generative systems

- They produce **structured content from a small seed** (a PRNG gives noise; a CA or an
  L-system gives *shaped* noise — patches, networks, hierarchies).
- They give the memory and concept systems **stable, recomputable ground truth**
  (a landmark generated by an L-system has a canonical structure the organism can
  recognise and remember).
- They are **auditable**: any generated object is reproducible from its seed, which
  keeps replays bit-exact and debugging tractable.
- They keep the **LLM out of content generation**: generation is a pure function of
  the seed, never a language-model call (invariant: LLM never mutates world state).

### Where each technique fits

Techniques split between **runtime** (live C++ systems, allocation-light, called every
tick or at well-defined moments) and **offline** (Python tooling in conda `eidolon`,
bake frozen artifacts the C++ runtime only consumes). Every technique preserves the
determinism contract (`--seed N --deterministic`) and the "no LLM in the hot path"
invariant.

#### World, environment & resources (§4)

| Technique | Where it fits in Eidolon | Runtime / Offline | Determinism / invariant notes |
|---|---|---|---|
| **Noise fields** (Perlin / simplex / value noise) | Foundation of world gen: elevation, climate (temperature, humidity), biome boundaries, resource density (mineral veins, fertile soil, water table). Multi-octave noise gives smooth gradients the organism's perception and planning can use. | Runtime (init + per-tick cheap lookup, no grid scan) | Pure function of (world seed, coord); cached, bounded memory |
| **Voronoi / Delaunay** | Region划分 for biomes / territories / settlement placement (wildlife dens, the organism's shelter). Delaunay graph = path/road/landmark connectivity the spatial memory can index. | Runtime (init) | Geometric tessellation is deterministic for a fixed seed |
| **Cellular automata (CA)** | Terrain shaping (init: thickets, clearings, water networks, cave systems). Live plant ecology: per-tile spread/death rules so vegetation clumps and regrows organically — the organism learns to revisit patches. Fire spread (wildfire hazard). Disease spread across tiles or population. | Runtime (init steps; per-tick single-tile ring update, never full-grid in hot path) | CA step = pure function of (grid, rule, iter cap); seedable |
| **Reaction-diffusion** | Terrain texture patterns (mineral veins, fertile-soil gradients), wildlife coat patterns (cosmetic), biological pattern formation. Gives organic resource patterns that noise alone can't produce. | Runtime (init, bounded steps) | Discretised PDE; stable under explicit Euler with capped iterations |
| **Procedural generation** | Ruins / landmarks / named places / environmental objects with semantic tags (memory ground truth, §11). Extends existing seedable world gen. | Runtime (init) | Any PRNG stream derived from the world seed |
| **L-systems** | Plant / bush / branch geometry, river / road / root networks, terrain detail (dense groves along a "trunk" line). Foraging targets get spatial identity the memory system can reference. | Runtime (init) | Turtle interpretation = pure (axiom, rules, depth cap, seed) |

#### Body & physiology (§5)

| Technique | Where it fits in Eidolon | Runtime / Offline | Determinism / invariant notes |
|---|---|---|---|
| **ODE systems** | Already core: body physiology (energy, hunger, thirst, fatigue, sleepPressure, body temperature, pain) evolves under coupled ODEs driven by actions, weather and foraging. The explicit ODE structure (phase, decay constants, integration step) is documented and unit-tested. | Runtime (every tick) | Integrator is symplectic / explicit Euler with fixed step; cap on max rate prevents explosion |

#### Wildlife, ecology & collective behaviour (§4 wildlife)

| Technique | Where it fits in Eidolon | Runtime / Offline | Determinism / invariant notes |
|---|---|---|---|
| **Agent-based models (ABM)** | The whole sim is already an ABM: the organism + wildlife are autonomous agents. Wildlife (prey, predators, birds) is the only other autonomous population (invariant: only one humanoid). Formalise the per-agent decision loop as the canonical ABM pattern (sense → decide → act). | Runtime | Per-agent RNG stream derived from (world seed, agent id); bit-exact |
| **Flocking / Boids** | Collective wildlife behaviour: bird flocks, prey herds, wolf packs (separation / alignment / cohesion + obstacle avoidance). Produces emergent group dynamics the organism perceives as living groups, not independent points. | Runtime (per agent, O(neighbours) update) | Rules are a pure function of neighbour positions; deterministic given seeds |
| **Markov models** | Explicit Markov chains for weather state transitions, wildlife behavioural states (forage / flee / rest / hunt), the organism's sleep / wake / active state machine, and skill-stage progression. Makes state transitions inspectable, testable, and tunable. | Runtime | Transition matrix is a constant of the sim; RNG chooses the next state from the row |
| **Ising models** | Social belief / norm dynamics: the organism's binary beliefs and trust states as spins, evidence as fields, consistency as couplings. Produces coherent worldviews, belief flips under strong evidence, cognitive dissonance when evidence conflicts. | Runtime (slow layer / reflection cadence) | Spin update rule is deterministic + bounded noise; convergence testable |

#### Mind, learning & cognition (§8–§9)

| Technique | Where it fits in Eidolon | Runtime / Offline | Determinism / invariant notes |
|---|---|---|---|
| **Evolutionary algorithms (EA)** | Offline: evolve policy-prior weights (PoC: `python/teacher/evolve_prior.py`), tune wildlife behaviour parameters, tune recipe hyperparameters. Online micro-EA on tiny task-specific populations (e.g. hyperparameter search for the bandit temperature). | Offline (Python tooling); online only when trivially bounded | Fixed RNG seed + deterministic sims on held-out seeds ⇒ reproducible search; frozen artifacts |
| **Genetic programming (GP)** | Offline: evolve recipe trees (crafting / tool invention) and behaviour trees (action sequences) validated against world physics. The organism "discovers" procedures through evolution, not memorisation. | Offline (Python tooling) | Tournament + subtree crossover/mutation; fitness = sim validation; depth cap |
| **Graph rewriting** | Concept ontology (§9) as a typed graph grown by rewrite rules when the organism forms associations. Tech / recipe graph (§6) rewritten when new crafting combinations are discovered. Belief graph (§8) rewritten when evidence resolves contradictions. | Runtime (slow layer) and Offline (concept / recipe discovery) | Rewrite rules = deterministic productions applied under the sim seed |
| **Formal grammars** | Structured goal / event templates for episodic-memory compression ("thirsty → went to water → drank"), recipe production rules, and grounded utterance templates for the language bridge (§14) — replaces some LLM dependence with deterministic, state-seeded language. | Runtime (slow layer) | Production rules are a deterministic rewrite system; choices driven by the sim seed and the organism's state |
| **Shape grammars** | Construction geometry (§6): shelter / wall / campfire / storage / farm-plot forms generated from a shape grammar seeded by site context and available materials. Tools get anatomical structure (handle / blade / binding) from a shape grammar. | Runtime (when the organism builds) | Turtle interpretation with depth cap; deterministic for a fixed seed |

### Policy (guarding the invariants)

1. **Generation is a pure function of the world/sim seed** and any explicitly passed
   parameters — never of wall-clock time or LLM output.
2. **Bounded**: CA iterations, L-system depth, grammar depth, ODE step, EA generations,
   GP depth, Ising sweeps, Markov chain length all have hard caps; generation is
   amortised (world-gen at init, plant ecology at most a few cells per tick via a
   ring/sweep, never a full-grid pass in the hot path).
3. **Offline-first** for heavy compute: EA, GP, graph-rewriting search, large Markov
   parameter fits run in Python tooling and bake frozen artifacts (like `.eprp` priors);
   the C++ runtime only *consumes* artifacts.
4. **Content couples to behaviour** (§4: "the world is not decorative") — generated
   structure must feed perception / affordances / memory, not just cosmetics.
5. **No LLM call from a deterministic system**: grammar / shape / graph rewriting produce
   *templates* that the LLM bridge may surface to the user, but the LLM never mutates the
   underlying generated object.

---

## 21. Development Process

1. Inspect existing repo first; never blindly rewrite (repo is currently empty; this doc
   fixes the baseline).
2. Build minimal end-to-end organism first (§2–§16 slice), then add systems incrementally per
   `ROADMAP.md`.
3. Isolate the C++ core from server/UI from the start (no browser APIs in `ReplicaCore`),
   then: native headless build → same engine compiled to WASM → Web Workers → SIMD →
   WebGPU neural inference (experimental) → synchronization + offline persistence →
   benchmark across devices and auto-select the execution profile (§17).
4. After every change: compile, run unit + integration tests, run an autonomous simulation,
   inspect anomalies, fix root causes.
5. Follow `AGENTS.md` SOP: commit at every step, conda env `eidolon` for all Python, ask the
   user when in doubt, install missing tools (or ask the user to install).
