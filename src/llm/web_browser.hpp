// Web browsing capability for the organism (DESIGN §Future: Internet Access).
// Configurable, user-gated browsing — results become content the organism reads
// and learns from via the normal memory/learning pipeline. Never injected as
// prompt text or live-mind backdoor.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace eidolon {

struct WebSearchResult {
  std::string title;
  std::string url;
  std::string snippet; // brief summary from search results
};

struct WebFetchResult {
  std::string url;
  std::string content; // extracted main text content (truncated)
  bool success = false;
  std::string error;
};

// Browser configuration (server-side, user-gated)
struct BrowserConfig {
  bool enabled = false;           // master switch (user-gated)
  std::string searchEndpoint;     // e.g. "https://api.duckduckgo.com/"
  std::string searchApiKey;       // optional API key
  uint32_t maxResults = 5;        // max results per search
  uint32_t maxFetchChars = 8000;  // max chars to fetch per page
  uint32_t timeoutMs = 10000;     // request timeout
  bool allowFetch = true;         // allow full page fetch (not just snippets)
};

// Abstract browser interface for testing/mocking
class WebBrowser {
public:
  explicit WebBrowser(const BrowserConfig& cfg) : cfg_(cfg) {}
  virtual ~WebBrowser() = default;

  // Search the web for a query, return top results
  virtual bool search(const std::string& query, std::vector<WebSearchResult>& out,
                      std::string& err) = 0;

  // Fetch and extract main text content from a URL
  virtual bool fetch(const std::string& url, WebFetchResult& out,
                     std::string& err) = 0;

  bool enabled() const { return cfg_.enabled; }
  const BrowserConfig& config() const { return cfg_; }

protected:
  BrowserConfig cfg_;
};

// Production implementation using a search API + HTTP fetcher
class HttpWebBrowser : public WebBrowser {
public:
  explicit HttpWebBrowser(const BrowserConfig& cfg) : WebBrowser(cfg) {}

  bool search(const std::string& query, std::vector<WebSearchResult>& out,
              std::string& err) override;

  bool fetch(const std::string& url, WebFetchResult& out,
             std::string& err) override;
};

} // namespace eidolon