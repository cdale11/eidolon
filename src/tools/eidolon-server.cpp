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
               "  --data DIR           run directory (default data/runs/server)\n"
               "  --host HOST          listen host (default 127.0.0.1)\n"
               "  --port N             listen port (default 8081)\n"
               "  --seed N             master seed for fresh organisms (default: entropy)\n"
               "  --deterministic      deterministic sim (requires --seed)\n"
               "  --world WxH          world grid size (default 128x128)\n"
               "  --policy-prior FILE  seed fresh organisms with teacher-baked weights\n"
               "  --llm URL            OpenAI-compatible endpoint, e.g.\n"
               "                       http://127.0.0.1:8080/v1 (default: offline)\n"
               "  --llm-timeout MS     LLM call timeout in ms (default 10000)\n"
               "  --fidelity 0|1|2|3   adaptive fidelity: 0=auto (default), 1=low, 2=medium, 3=high\n"
               "  --internet-enabled   enable internet browsing for the organism (default: off)\n"
               "  --search-engine NAME search engine: searxng|ddg|serpapi|brave|google|custom (default: searxng)\n"
               "  --search-endpoint URL  custom search API endpoint (for custom engine)\n"
               "  --search-api-key KEY   API key for search engine (Brave/SerpAPI/Google)\n"
               "  --google-cx ID         Google Custom Search CX (for google engine)\n"
               "  --max-search-results N  max results per search (default 5)\n"
               "  --max-fetch-chars N  max chars to fetch per page (default 8000)\n"
               "  --browse-timeout MS  browse request timeout in ms (default 10000)\n"
               "  --help               this message\n",
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
    } else if (a == "--policy-prior") opts.policyPriorPath = need("FILE");
    else if (a == "--heredity") opts.heredityPath = need("FILE");
    else if (a == "--heredity-weight") opts.heredityWeight = std::atof(need("FLOAT"));
    else if (a == "--dump-experiences") opts.dumpExperiencesPath = need("FILE");
    else if (a == "--llm") opts.llmEndpoint = need("URL");
    else if (a == "--llm-timeout") opts.llmTimeoutMs = std::atoi(need("MS"));
    else if (a == "--fidelity") {
      opts.fidelityLevel = std::atoi(need("0|1|2|3 (auto|low|medium|high)"));
    } else if (a == "--internet-enabled") {
      opts.internetEnabled = true;
    } else if (a == "--search-engine") {
      const std::string engine = need("searxng|ddg|serpapi|brave|google|custom");
      if (engine == "searxng") opts.searchEngine = eidolon::SearchEngine::SearXNG;
      else if (engine == "ddg") opts.searchEngine = eidolon::SearchEngine::DuckDuckGo;
      else if (engine == "serpapi") opts.searchEngine = eidolon::SearchEngine::SerpAPI;
      else if (engine == "brave") opts.searchEngine = eidolon::SearchEngine::Brave;
      else if (engine == "google") opts.searchEngine = eidolon::SearchEngine::Google;
      else if (engine == "custom") opts.searchEngine = eidolon::SearchEngine::Custom;
      else {
        std::fprintf(stderr, "error: unknown search engine '%s'\n", engine.c_str());
        return 2;
      }
    } else if (a == "--search-endpoint") {
      opts.searchEndpoint = need("URL");
    } else if (a == "--search-api-key") {
      opts.searchApiKey = need("KEY");
    } else if (a == "--google-cx") {
      opts.searchApiKey += ":" + std::string(need("CX")); // format: apiKey:cx
    } else if (a == "--max-search-results") {
      opts.maxSearchResults = static_cast<uint32_t>(std::atoi(need("N")));
    } else if (a == "--max-fetch-chars") {
      opts.maxFetchChars = static_cast<uint32_t>(std::atoi(need("N")));
    } else if (a == "--browse-timeout") {
      opts.browseTimeoutMs = static_cast<uint32_t>(std::atoi(need("MS")));
    } else if (a == "--help") {
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