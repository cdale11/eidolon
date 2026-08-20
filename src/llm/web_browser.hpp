// Web browsing capability for the organism (DESIGN §Future: Internet Access).
// Configurable, user-gated browsing — results become content the organism reads
// and learns from via the normal memory/learning pipeline. Never injected as
// prompt text or live-mind backdoor.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>

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

// Search engine type enumeration
enum class SearchEngine : uint8_t {
  DuckDuckGo = 0,  // HTML scraping (free, rate-limited)
  SerpAPI = 1,     // SerpAPI (paid, reliable)
  Brave = 2,       // Brave Search API (free tier)
  Google = 3,      // Google Custom Search API (paid)
  Custom = 4,      // Custom endpoint (configure searchEndpoint + searchApiKey)
  SearXNG = 5      // SearXNG public instances (free, no API key)
};

// Browser configuration (server-side, user-gated)
struct BrowserConfig {
  bool enabled = false;           // master switch (user-gated)
  SearchEngine searchEngine = SearchEngine::SearXNG;  // SearXNG public instances (free, no API key)
  std::string searchEndpoint;     // custom endpoint for SearchEngine::Custom
  std::string searchApiKey;       // API key for SerpAPI/Brave/Google/Custom
  uint32_t maxResults = 5;        // max results per search
  uint32_t maxFetchChars = 8000;  // max chars to fetch per page
  uint32_t timeoutMs = 10000;     // request timeout
  bool allowFetch = true;         // allow full page fetch (not just snippets)
  uint32_t maxRetries = 2;        // max retry attempts for transient errors
  uint32_t retryDelayMs = 500;    // base delay between retries
};

// Abstract search provider interface
class SearchProvider {
public:
  virtual ~SearchProvider() = default;
  
  // Search for query, populate results
  virtual bool search(const std::string& query, uint32_t maxResults,
                      std::vector<WebSearchResult>& out,
                      std::string& err) = 0;
  
  // Get provider name for logging/debugging
  virtual std::string name() const = 0;
  
  // Get rate limit info (requests per minute, 0 = unlimited)
  virtual uint32_t rateLimitRpm() const { return 0; }
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

// Production implementation using configured search provider + HTTP fetcher
class HttpWebBrowser : public WebBrowser {
public:
  explicit HttpWebBrowser(const BrowserConfig& cfg);
  
  bool search(const std::string& query, std::vector<WebSearchResult>& out,
              std::string& err) override;
  
  bool fetch(const std::string& url, WebFetchResult& out,
             std::string& err) override;

private:
  std::unique_ptr<SearchProvider> provider_;
};

} // namespace eidolon