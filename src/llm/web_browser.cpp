// Web browsing implementation
#include "llm/web_browser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

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

// Extract text content from HTML (simplified - removes tags, scripts, styles)
std::string extractTextFromHtml(const std::string& html) {
  std::string text = html;
  // Remove scripts and styles
  std::regex scriptRe("<script[^>]*>.*?</script>", std::regex::icase | std::regex::ECMAScript);
  std::regex styleRe("<style[^>]*>.*?</style>", std::regex::icase | std::regex::ECMAScript);
  text = std::regex_replace(text, scriptRe, " ");
  text = std::regex_replace(text, styleRe, " ");
  // Remove all tags
  std::regex tagRe("<[^>]*>");
  text = std::regex_replace(text, tagRe, " ");
  // Decode common HTML entities
  std::replace(text.begin(), text.end(), '&', ' ');
  std::replace(text.begin(), text.end(), ';', ' ');
  // Collapse whitespace
  std::regex wsRe("\\s+");
  text = std::regex_replace(text, wsRe, " ");
  // Trim
  size_t start = text.find_first_not_of(" \t\n\r");
  size_t end = text.find_last_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  return text.substr(start, end - start + 1);
}

// Simple search using DuckDuckGo Lite (HTML) - handles CAPTCHA gracefully
bool duckduckgoSearch(const std::string& query, uint32_t maxResults,
                      std::vector<WebSearchResult>& out, std::string& err,
                      uint32_t timeoutMs) {
  httplib::Client cli("https://lite.duckduckgo.com");
  cli.set_connection_timeout(timeoutMs / 1000, timeoutMs % 1000);
  cli.set_read_timeout(timeoutMs / 1000, timeoutMs % 1000);

  std::string encoded = urlEncode(query);
  std::string path = "/lite/?q=" + encoded;
  auto res = cli.Get(path.c_str());
  if (!res || res->status != 200) {
    err = "search request failed: HTTP " + std::to_string(res ? res->status : 0);
    return false;
  }

  std::string html = res->body;
  // Check for CAPTCHA/blocking
  if (html.find("bots use DuckDuckGo") != std::string::npos ||
      html.find("anomaly") != std::string::npos ||
      html.find("captcha") != std::string::npos) {
    err = "search blocked by CAPTCHA - try a different search provider";
    return false;
  }

  // Parse Lite results: table rows with links
  // Format: <table>...<tr><td><a href="URL">Title</a></td><td>Snippet</td></tr>...
  std::regex resultRe(
      "<td[^>]*>\\s*<a\\s+href=\"([^\"]+)\"[^>]*>([^<]+)</a>\\s*</td>\\s*"
      "<td[^>]*>([^<]*)</td>",
      std::regex::icase | std::regex::ECMAScript);

  std::smatch match;
  std::string::const_iterator searchStart(html.cbegin());
  while (std::regex_search(searchStart, html.cend(), match, resultRe) && out.size() < maxResults) {
    WebSearchResult r;
    r.url = match[1].str();
    r.title = match[2].str();
    r.snippet = match[3].str();
    // Clean up
    r.title = std::regex_replace(r.title, std::regex("&[a-z]+;"), " ");
    r.snippet = std::regex_replace(r.snippet, std::regex("&[a-z]+;"), " ");
    // Only keep http(s) URLs
    if (r.url.rfind("http", 0) == 0) {
      out.push_back(std::move(r));
    }
    searchStart = match.suffix().first;
  }
  return !out.empty();
}

} // namespace

bool HttpWebBrowser::search(const std::string& query, std::vector<WebSearchResult>& out,
                            std::string& err) {
  if (!cfg_.enabled) {
    err = "browser disabled";
    return false;
  }
  return duckduckgoSearch(query, cfg_.maxResults, out, err, cfg_.timeoutMs);
}

bool HttpWebBrowser::fetch(const std::string& url, WebFetchResult& out,
                           std::string& err) {
  if (!cfg_.enabled || !cfg_.allowFetch) {
    err = "fetch disabled";
    return false;
  }
  out.url = url;

  // Parse URL to get host and path
  std::string host, path;
  std::string u = url;
  if (u.rfind("https://", 0) == 0) u = u.substr(8);
  else if (u.rfind("http://", 0) == 0) u = u.substr(7);
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