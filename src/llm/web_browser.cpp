// Web browsing implementation with multiple search provider support
#include "llm/web_browser.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <regex>
#include <thread>

#include "httplib.h"
#include "core/json.hpp"

namespace eidolon {

namespace {

std::string urlEncode(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string extractTextFromHtml(const std::string& html) {
  std::string text = html;
  std::regex scriptRe("<script[^>]*>.*?</script>", std::regex::icase | std::regex::ECMAScript);
  std::regex styleRe("<style[^>]*>.*?</style>", std::regex::icase | std::regex::ECMAScript);
  text = std::regex_replace(text, scriptRe, " ");
  text = std::regex_replace(text, styleRe, " ");
  std::regex tagRe("<[^>]*>");
  text = std::regex_replace(text, tagRe, " ");
  std::replace(text.begin(), text.end(), '&', ' ');
  std::replace(text.begin(), text.end(), ';', ' ');
  std::regex wsRe("\\s+");
  text = std::regex_replace(text, wsRe, " ");
  size_t start = text.find_first_not_of(" \t\n\r");
  size_t end = text.find_last_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  return text.substr(start, end - start + 1);
}

class DuckDuckGoProvider : public SearchProvider {
public:
  std::string name() const override { return "DuckDuckGo"; }
  uint32_t rateLimitRpm() const override { return 30; }
  
  bool search(const std::string& query, uint32_t maxResults,
              std::vector<WebSearchResult>& out, std::string& err) override {
    const uint32_t timeoutMs = 15000;
    const uint32_t maxRetries = 2;
    const uint32_t retryDelayMs = 1000;
    
    for (uint32_t attempt = 0; attempt <= maxRetries; ++attempt) {
      httplib::Client cli("https://duckduckgo.com");
      cli.set_connection_timeout(timeoutMs / 1000, timeoutMs % 1000);
      cli.set_read_timeout(timeoutMs / 1000, timeoutMs % 1000);
      cli.set_follow_location(true);
      
      std::string encoded = urlEncode(query);
      std::string path = "/html/?q=" + encoded + "&kl=us-en";
      
      auto res = cli.Get(path.c_str());
      if (!res || (res->status != 200 && res->status != 202)) {
        err = "search request failed: HTTP " + std::to_string(res ? res->status : 0);
        if (attempt < maxRetries) {
          std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs * (attempt + 1)));
          continue;
        }
        return false;
      }
      
      std::string html = res->body;
      if (html.find("bots use DuckDuckGo") != std::string::npos ||
          html.find("anomaly") != std::string::npos ||
          html.find("captcha") != std::string::npos) {
        err = "search blocked by CAPTCHA - try a different search provider";
        return false;
      }
      
      std::regex resultRe(
          "class=\"result__snippet\"[^>]*>([^<]+)</a>.*?"
          "class=\"result__url\"[^>]*>([^<]+)</a>",
          std::regex::icase | std::regex::ECMAScript);
      
      std::smatch match;
      std::string::const_iterator searchStart(html.cbegin());
      while (std::regex_search(searchStart, html.cend(), match, resultRe) && out.size() < maxResults) {
        WebSearchResult r;
        r.snippet = match[1].str();
        r.url = match[2].str();
        r.snippet = std::regex_replace(r.snippet, std::regex("&[a-z]+;"), " ");
        r.url = std::regex_replace(r.url, std::regex("&[a-z]+;"), "");
        
        size_t dashPos = r.snippet.find(" - ");
        if (dashPos != std::string::npos) {
          r.title = r.snippet.substr(0, dashPos);
        } else {
          r.title = r.snippet.substr(0, std::min<size_t>(r.snippet.size(), 80));
        }
        
        if (!r.url.empty() && r.url.find("http") != 0) {
          r.url = "https://" + r.url;
        }
        out.push_back(std::move(r));
        searchStart = match.suffix().first;
      }
      
      if (!out.empty()) return true;
      err = "no results found";
      return false;
    }
    return false;
  }
};

class SerpApiProvider : public SearchProvider {
public:
  SerpApiProvider(const std::string& apiKey) : apiKey_(apiKey) {}
  
  std::string name() const override { return "SerpAPI"; }
  uint32_t rateLimitRpm() const override { return 100; }
  
  bool search(const std::string& query, uint32_t maxResults,
              std::vector<WebSearchResult>& out, std::string& err) override {
    if (apiKey_.empty()) {
      err = "SerpAPI key not configured";
      return false;
    }
    
    const uint32_t timeoutMs = 15000;
    httplib::Client cli("https://serpapi.com");
    cli.set_connection_timeout(timeoutMs / 1000, timeoutMs % 1000);
    cli.set_read_timeout(timeoutMs / 1000, timeoutMs % 1000);
    
    std::string encoded = urlEncode(query);
    std::string path = "/search?api_key=" + apiKey_ + "&q=" + encoded + "&engine=google&num=" + std::to_string(maxResults);
    
    auto res = cli.Get(path.c_str());
    if (!res || res->status != 200) {
      err = "SerpAPI request failed: HTTP " + std::to_string(res ? res->status : 0);
      return false;
    }
    
    JsonValue body;
    if (!jsonParse(res->body, body)) {
      err = "failed to parse SerpAPI response";
      return false;
    }
    
    const JsonValue* organic = body.find("organic_results");
    if (!organic || organic->type() != JsonValue::Type::Array) {
      err = "no organic results in SerpAPI response";
      return false;
    }
    
    for (const auto& result : organic->asArray()) {
      if (out.size() >= maxResults) break;
      
      WebSearchResult r;
      r.title = result.str("title", "");
      r.url = result.str("link", "");
      r.snippet = result.str("snippet", "");
      
      if (!r.url.empty()) {
        out.push_back(std::move(r));
      }
    }
    
    return !out.empty();
  }

private:
  std::string apiKey_;
};

class BraveProvider : public SearchProvider {
public:
  BraveProvider(const std::string& apiKey) : apiKey_(apiKey) {}
  
  std::string name() const override { return "Brave"; }
  uint32_t rateLimitRpm() const override { return 2000; }
  
  bool search(const std::string& query, uint32_t maxResults,
              std::vector<WebSearchResult>& out, std::string& err) override {
    if (apiKey_.empty()) {
      err = "Brave API key not configured";
      return false;
    }
    
    const uint32_t timeoutMs = 15000;
    httplib::Client cli("https://api.search.brave.com");
    cli.set_connection_timeout(timeoutMs / 1000, timeoutMs % 1000);
    cli.set_read_timeout(timeoutMs / 1000, timeoutMs % 1000);
    
    std::string encoded = urlEncode(query);
    std::string path = "/res/v1/web/search?q=" + encoded + "&count=" + std::to_string(maxResults);
    
    httplib::Headers headers;
    headers.emplace("Accept", "application/json");
    headers.emplace("X-Subscription-Token", apiKey_);
    
    auto res = cli.Get(path.c_str(), headers);
    if (!res || res->status != 200) {
      err = "Brave Search request failed: HTTP " + std::to_string(res ? res->status : 0);
      if (res) err += " - " + res->body;
      return false;
    }
    
    JsonValue body;
    if (!jsonParse(res->body, body)) {
      err = "failed to parse Brave response";
      return false;
    }
    
    const JsonValue* web = body.find("web");
    if (web) {
      const JsonValue* results = web->find("results");
      if (results && results->type() == JsonValue::Type::Array) {
        for (const auto& result : results->asArray()) {
          if (out.size() >= maxResults) break;
          
          WebSearchResult r;
          r.title = result.str("title", "");
          r.url = result.str("url", "");
          r.snippet = result.str("description", "");
          
          if (!r.url.empty()) {
            out.push_back(std::move(r));
          }
        }
      }
    }
    
    return !out.empty();
  }

private:
  std::string apiKey_;
};

class GoogleProvider : public SearchProvider {
public:
  GoogleProvider(const std::string& apiKey, const std::string& cx)
    : apiKey_(apiKey), cx_(cx) {}
  
  std::string name() const override { return "Google"; }
  uint32_t rateLimitRpm() const override { return 100; }
  
  bool search(const std::string& query, uint32_t maxResults,
              std::vector<WebSearchResult>& out, std::string& err) override {
    if (apiKey_.empty() || cx_.empty()) {
      err = "Google API key or CX not configured";
      return false;
    }
    
    const uint32_t timeoutMs = 15000;
    httplib::Client cli("https://www.googleapis.com");
    cli.set_connection_timeout(timeoutMs / 1000, timeoutMs % 1000);
    cli.set_read_timeout(timeoutMs / 1000, timeoutMs % 1000);
    
    std::string encoded = urlEncode(query);
    std::string path = "/customsearch/v1?key=" + apiKey_ + "&cx=" + cx_ + 
                       "&q=" + encoded + "&num=" + std::to_string(maxResults);
    
    auto res = cli.Get(path.c_str());
    if (!res || res->status != 200) {
      err = "Google Search request failed: HTTP " + std::to_string(res ? res->status : 0);
      if (res) err += " - " + res->body;
      return false;
    }
    
    JsonValue body;
    if (!jsonParse(res->body, body)) {
      err = "failed to parse Google response";
      return false;
    }
    
    const JsonValue* items = body.find("items");
    if (items && items->type() == JsonValue::Type::Array) {
      for (const auto& item : items->asArray()) {
        if (out.size() >= maxResults) break;
        
        WebSearchResult r;
        r.title = item.str("title", "");
        r.url = item.str("link", "");
        r.snippet = item.str("snippet", "");
        
        if (!r.url.empty()) {
          out.push_back(std::move(r));
        }
      }
    }
    
    return !out.empty();
  }

private:
  std::string apiKey_;
  std::string cx_;
};

class CustomProvider : public SearchProvider {
public:
  CustomProvider(const std::string& endpoint, const std::string& apiKey)
    : endpoint_(endpoint), apiKey_(apiKey) {}
  
  std::string name() const override { return "Custom"; }
  uint32_t rateLimitRpm() const override { return 60; }
  
  bool search(const std::string& query, uint32_t maxResults,
              std::vector<WebSearchResult>& out, std::string& err) override {
    if (endpoint_.empty()) {
      err = "custom endpoint not configured";
      return false;
    }
    
    const uint32_t timeoutMs = 15000;
    httplib::Client cli(endpoint_);
    cli.set_connection_timeout(15, 0);
    cli.set_read_timeout(15, 0);
    
    std::string encoded = urlEncode(query);
    std::string path = "?q=" + encoded + "&max_results=" + std::to_string(maxResults);
    
    httplib::Headers headers;
    if (!apiKey_.empty()) {
      headers.emplace("Authorization", "Bearer " + apiKey_);
    }
    
    auto res = cli.Get(path.c_str(), headers);
    if (!res || res->status != 200) {
      err = "custom search request failed: HTTP " + std::to_string(res ? res->status : 0);
      return false;
    }
    
    JsonValue body;
    if (!jsonParse(res->body, body)) {
      err = "failed to parse custom search response";
      return false;
    }
    
    const JsonValue* items = body.find("results");
    if (items && items->type() == JsonValue::Type::Array) {
      for (const auto& item : items->asArray()) {
        if (out.size() >= maxResults) break;
        
        WebSearchResult r;
        r.title = item.str("title", "");
        r.url = item.str("url", "");
        r.snippet = item.str("snippet", "");
        
        if (!r.url.empty()) {
          out.push_back(std::move(r));
        }
      }
    }
    
    return !out.empty();
  }

private:
  std::string endpoint_;
  std::string apiKey_;
};

class SearXngProvider : public SearchProvider {
public:
  SearXngProvider() {
    instances_ = {
      "https://searx.be",
      "https://search.bus-hit.me", 
      "https://searx.tiekoetter.com",
      "https://searx.space",
      "https://search.privacypost.xyz",
    };
  }
  
  std::string name() const override { return "SearXNG"; }
  uint32_t rateLimitRpm() const override { return 30; }
  
  bool search(const std::string& query, uint32_t maxResults,
              std::vector<WebSearchResult>& out, std::string& err) override {
    // Try each instance until one works
    for (const auto& instance : instances_) {
      httplib::Client cli(instance);
      cli.set_connection_timeout(15, 0);
      cli.set_read_timeout(15, 0);
      
      std::string path = "/search?q=" + urlEncode(query) + "&format=json&categories=general&language=en-US&safesearch=1";
      auto res = cli.Get(path.c_str());
      if (!res || res->status != 200) continue;
      
      JsonValue body;
      if (!jsonParse(res->body, body)) continue;
      
      const JsonValue* results = body.find("results");
      if (!results || results->type() != JsonValue::Type::Array) continue;
      
      for (const auto& item : results->asArray()) {
        if (out.size() >= maxResults) break;
        
        WebSearchResult r;
        r.title = item.str("title", "");
        r.url = item.str("url", "");
        r.snippet = item.str("content", "");
        
        if (!r.url.empty()) {
          out.push_back(std::move(r));
        }
      }
      
      if (!out.empty()) return true;
    }
    
    // Fallback: provide a helpful error message indicating the need for a proper search API
    err = "SearXNG instances unavailable (likely CAPTCHA/blocking). For production use, configure --search-engine=serpapi|brave|google with --search-api-key, or --search-engine=custom with a working endpoint. See: https://serpapi.com, https://brave.com/search/api/, https://developers.google.com/custom-search";
    return false;
  }

private:
  std::vector<std::string> instances_;
};

static std::unique_ptr<SearchProvider> createProvider(const BrowserConfig& cfg) {
  switch (cfg.searchEngine) {
    case SearchEngine::DuckDuckGo:
      return std::make_unique<DuckDuckGoProvider>();
    case SearchEngine::SerpAPI:
      return std::make_unique<SerpApiProvider>(cfg.searchApiKey);
    case SearchEngine::Brave:
      return std::make_unique<BraveProvider>(cfg.searchApiKey);
    case SearchEngine::Google:
      {
        size_t colon = cfg.searchApiKey.find(':');
        if (colon == std::string::npos) return nullptr;
        return std::make_unique<GoogleProvider>(
            cfg.searchApiKey.substr(0, colon),
            cfg.searchApiKey.substr(colon + 1));
      }
    case SearchEngine::Custom:
      return std::make_unique<CustomProvider>(cfg.searchEndpoint, cfg.searchApiKey);
    case SearchEngine::SearXNG:
      return std::make_unique<SearXngProvider>();
    default:
      return nullptr;
  }
}

} // namespace

HttpWebBrowser::HttpWebBrowser(const BrowserConfig& cfg) : WebBrowser(cfg) {
  provider_ = createProvider(cfg);
  if (!provider_) {
    std::fprintf(stderr, "warning: failed to create search provider for engine %d\n",
                 static_cast<int>(cfg.searchEngine));
  }
}

bool HttpWebBrowser::search(const std::string& query, std::vector<WebSearchResult>& out,
                            std::string& err) {
  if (!provider_) {
    err = "no search provider configured";
    return false;
  }
  if (!cfg_.enabled) {
    err = "internet access disabled";
    return false;
  }
  if (query.empty()) {
    err = "empty query";
    return false;
  }
  
  uint32_t maxResults = std::min(cfg_.maxResults, static_cast<uint32_t>(20));
  return provider_->search(query, maxResults, out, err);
}

bool HttpWebBrowser::fetch(const std::string& url, WebFetchResult& out,
                           std::string& err) {
  if (!cfg_.enabled || !cfg_.allowFetch) {
    err = "fetch disabled";
    return false;
  }
  out.url = url;
  
  std::string host, path;
  std::string u = url;
  bool isHttps = false;
  if (u.rfind("https://", 0) == 0) {
    u = u.substr(8);
    isHttps = true;
  } else if (u.rfind("http://", 0) == 0) {
    u = u.substr(7);
  }
  size_t slash = u.find('/');
  if (slash == std::string::npos) {
    host = u;
    path = "/";
  } else {
    host = u.substr(0, slash);
    path = u.substr(slash);
  }
  
  httplib::Client cli(host.c_str());
  cli.set_connection_timeout(cfg_.timeoutMs / 1000, cfg_.timeoutMs % 1000);
  cli.set_read_timeout(cfg_.timeoutMs / 1000, cfg_.timeoutMs % 1000);
  cli.set_follow_location(true); // Follow redirects
  
  if (isHttps) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    cli.enable_server_certificate_verification(true);
#else
    err = "HTTPS not supported (OpenSSL not linked)";
    return false;
#endif
  }
  
  auto res = cli.Get(path.c_str());
  if (!res || res->status != 200) {
    out.success = false;
    out.error = "HTTP " + std::to_string(res ? res->status : 0);
    err = out.error;
    return false;
  }
  
  out.content = extractTextFromHtml(res->body);
  if (out.content.size() > cfg_.maxFetchChars) {
    out.content.resize(cfg_.maxFetchChars);
  }
  out.success = true;
  return true;
}

} // namespace eidolon