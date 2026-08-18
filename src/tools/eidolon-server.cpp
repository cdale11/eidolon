// eidolon-server entry point: runs the sim + chat UI.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "server/server.hpp"

using namespace eidolon;

namespace {
Server* g_server = nullptr;

void handleSignal(int) {
  if (g_server) g_server->requestStop();
}

void printUsage(FILE* out, const char* prog) {
  std::fprintf(out,
               "usage: %s [options]\n"
               "  --data DIR        run directory (default data/runs/server)\n"
               "  --host HOST       listen host (default 127.0.0.1)\n"
               "  --port N          listen port (default 8081)\n"
               "  --seed N          master seed for fresh organisms (default 42)\n"
               "  --deterministic   deterministic sim (requires --seed)\n"
               "  --world WxH       world grid size (default 128x128)\n"
               "  --llm URL         OpenAI-compatible endpoint, e.g.\n"
               "                    http://127.0.0.1:8080/v1 (default: offline)\n"
               "  --llm-timeout MS  LLM call timeout in ms (default 10000)\n"
               "  --help            this message\n",
               prog);
}
} // namespace

int main(int argc, char** argv) {
  Server::Options opts;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto need = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires %s\n", a.c_str(), what);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--data") opts.dataDir = need("DIR");
    else if (a == "--host") opts.listenHost = need("HOST");
    else if (a == "--port") opts.port = std::atoi(need("N"));
    else if (a == "--seed") opts.seed = std::strtoull(need("N"), nullptr, 0);
    else if (a == "--deterministic") opts.deterministic = true;
    else if (a == "--world") {
      int w = 0, h = 0;
      if (std::sscanf(need("WxH"), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
        std::fprintf(stderr, "error: bad --world size\n");
        return 2;
      }
      opts.worldW = w;
      opts.worldH = h;
    } else if (a == "--llm") opts.llmEndpoint = need("URL");
    else if (a == "--llm-timeout") opts.llmTimeoutMs = std::atoi(need("MS"));
    else if (a == "--help") {
      printUsage(stdout, argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "error: unknown option '%s'\n", a.c_str());
      printUsage(stderr, argv[0]);
      return 2;
    }
  }

  Server server(std::move(opts));
  g_server = &server;
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
  return server.run();
}