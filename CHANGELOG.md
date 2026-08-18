# Changelog

All notable user-visible changes to Eidolon, grouped by phase. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Phase 0 — Repository bootstrap
- Initialized git repository (user `Clark Dale`, remote `git@github.com:cdale11/eidolon.git`).
- Added project documentation: README (overview/quickstart), DESIGN (consolidated
  architecture), ROADMAP (sequential phases & gates), AGENTS (agent SOP), MISTAKES (lesson
  log), CHANGELOG.
- Environment audit: g++ 14.3.1, CMake 3.30.8, ninja, sqlite3 headers, conda env `eidolon`,
  llama.cpp with llama-server + GGUF models, ROCm 6.2.1 iGPU runtime (Radeon 740M,
  gfx1103); PyTorch policy: CPU default, iGPU experimental.