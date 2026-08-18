# Eidolon C++ runtime sources (library `eidolon` + binaries)

Phases 1+ fill this tree:

```
src/
├── core/       RNG (xoshiro256++), adaptive clock, event queue, logging, math, serialization
├── ml/         tiny NN: mlp, linear, embeddings, associative tables, rnn cell, online learners
├── world/      grid, terrain, weather, seasons, flora, fauna, hazards, items, structures
├── body/       physiology, neuromodulators
├── perception/ senses, attention, feature extraction
├── memory/     episodic/semantic/procedural/emotional/social stores, consolidation, dreams
├── cognition/  drives, goals, planning, self-model, concepts, metacognition
├── social/     user model, wildlife models
├── llm/        provider interface (llama.cpp/OpenAI), snapshot builder, semantic I/O, validation
├── persist/    binary snapshot + SQLite archive, migrations
├── server/     cpp-httplib HTTP/WS server, chat API, metrics
└── tools/      eidolon-sim (headless), eidolon-server, bench
```

See DESIGN.md for the full architecture.