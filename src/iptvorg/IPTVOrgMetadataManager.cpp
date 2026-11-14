#include "IPTVOrgMetadataManager.h"
#include "../LogControl.h"
#include "../PlaylistManager.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

IPTVOrgMetadataManager::IPTVOrgMetadataManager(PlaylistManager *playlistMgr)
    : m_playlistMgr(playlistMgr) {}

bool IPTVOrgMetadataManager::FetchJson(const std::string &url,
                                       std::string &outContent,
                                       std::string *errorText,
                                       std::atomic<bool> *cancel) {
  if (!m_playlistMgr) {
    LOG_ERROR("IPTVOrgMetadataManager: PlaylistManager is null");
    if (errorText)
      *errorText = "Download failed: internal error (no PlaylistManager)";
    return false;
  }

  DownloadProgress prog;
  prog.externalCancel    = cancel;
  prog.connectTimeoutSec = 15;
  prog.totalTimeoutSec   = 60;

  ErrorCode ec = m_playlistMgr->downloadUrl(url, outContent, "", &prog);
  if (ec != ErrorCode::OK) {
    if (cancel && cancel->load(std::memory_order_acquire)) {
      if (errorText) *errorText = "Cancelled";
      return false;
    }
    std::string lastErr = m_playlistMgr->getLastError();
    LOG_ERROR("IPTVOrgMetadataManager: Failed to download %s, error: %s",
              url.c_str(), lastErr.c_str());
    if (errorText) {
      if (lastErr.empty())
        *errorText = "Download failed: unknown error";
      else
        *errorText = "Download failed: " + lastErr;
    }
    return false;
  }
  return true;
}

bool IPTVOrgMetadataManager::ParseCountries(const std::string &json,
                                            std::vector<Country> &out,
                                            std::string *errorText) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  if (doc.HasParseError()) {
    const char *en = rapidjson::GetParseError_En(doc.GetParseError());
    LOG_ERROR("IPTVOrgMetadataManager: Failed to parse countries JSON: %s", en);
    if (errorText)
      *errorText = std::string("Parse failed: JSON parse error: ") + en;
    return false;
  }
  if (!doc.IsArray()) {
    LOG_ERROR("IPTVOrgMetadataManager: countries JSON is not an array");
    if (errorText)
      *errorText = "Parse failed: unexpected JSON format (not an array)";
    return false;
  }
  out.clear();
  for (const auto &item : doc.GetArray()) {
    if (!item.IsObject())
      continue;
    Country c;
    if (item.HasMember("code") && item["code"].IsString())
      c.code = item["code"].GetString();
    if (item.HasMember("name") && item["name"].IsString())
      c.name = item["name"].GetString();
    if (item.HasMember("languages") && item["languages"].IsArray()) {
      for (const auto &lang : item["languages"].GetArray()) {
        if (lang.IsString())
          c.languages.push_back(lang.GetString());
      }
    }
    if (!c.code.empty() && !c.name.empty())
      out.push_back(std::move(c));
  }
  return true;
}

bool IPTVOrgMetadataManager::ParseLanguages(const std::string &json,
                                            std::vector<Language> &out,
                                            std::string *errorText) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  if (doc.HasParseError()) {
    const char *en = rapidjson::GetParseError_En(doc.GetParseError());
    LOG_ERROR("IPTVOrgMetadataManager: Failed to parse languages JSON: %s", en);
    if (errorText)
      *errorText = std::string("Parse failed: JSON parse error: ") + en;
    return false;
  }
  if (!doc.IsArray()) {
    LOG_ERROR("IPTVOrgMetadataManager: languages JSON is not an array");
    if (errorText)
      *errorText = "Parse failed: unexpected JSON format (not an array)";
    return false;
  }
  out.clear();
  for (const auto &item : doc.GetArray()) {
    if (!item.IsObject())
      continue;
    Language l;
    if (item.HasMember("code") && item["code"].IsString())
      l.code = item["code"].GetString();
    if (item.HasMember("name") && item["name"].IsString())
      l.name = item["name"].GetString();
    if (!l.code.empty() && !l.name.empty())
      out.push_back(std::move(l));
  }
  return true;
}

bool IPTVOrgMetadataManager::ParseCategories(const std::string &json,
                                             std::vector<Category> &out,
                                             std::string *errorText) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  if (doc.HasParseError()) {
    const char *en = rapidjson::GetParseError_En(doc.GetParseError());
    LOG_ERROR("IPTVOrgMetadataManager: Failed to parse categories JSON: %s",
              en);
    if (errorText)
      *errorText = std::string("Parse failed: JSON parse error: ") + en;
    return false;
  }
  if (!doc.IsArray()) {
    LOG_ERROR("IPTVOrgMetadataManager: categories JSON is not an array");
    if (errorText)
      *errorText = "Parse failed: unexpected JSON format (not an array)";
    return false;
  }
  out.clear();
  for (const auto &item : doc.GetArray()) {
    if (!item.IsObject())
      continue;
    Category c;
    if (item.HasMember("id") && item["id"].IsString())
      c.id = item["id"].GetString();
    if (item.HasMember("name") && item["name"].IsString())
      c.name = item["name"].GetString();
    if (!c.id.empty() && !c.name.empty())
      out.push_back(std::move(c));
  }
  return true;
}

bool IPTVOrgMetadataManager::FetchCountries(std::vector<Country> &out,
                                            std::string *errorText,
                                            std::atomic<bool> *cancel) {
  const uint64_t gen = m_generation.load(std::memory_order_acquire);
  {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    if (m_countriesLoaded) {
      out = m_cachedCountries;
      return true;
    }
  }
  std::string json;
  if (!FetchJson("https://iptv-org.github.io/api/countries.json", json,
                 errorText, cancel))
    return false;
  if (cancel && cancel->load(std::memory_order_acquire))
    return false;

  std::vector<Country> parsed;
  if (!ParseCountries(json, parsed, errorText))
    return false;

  {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    if (m_generation.load(std::memory_order_acquire) == gen) {
      m_cachedCountries = std::move(parsed);
      m_countriesLoaded = true;
      out = m_cachedCountries;
    } else {
      out = std::move(parsed);
    }
  }
  return true;
}

bool IPTVOrgMetadataManager::FetchLanguages(std::vector<Language> &out,
                                            std::string *errorText,
                                            std::atomic<bool> *cancel) {
  const uint64_t gen = m_generation.load(std::memory_order_acquire);
  {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    if (m_languagesLoaded) {
      out = m_cachedLanguages;
      return true;
    }
  }
  std::string json;
  if (!FetchJson("https://iptv-org.github.io/api/languages.json", json,
                 errorText, cancel))
    return false;
  if (cancel && cancel->load(std::memory_order_acquire))
    return false;

  std::vector<Language> parsed;
  if (!ParseLanguages(json, parsed, errorText))
    return false;

  {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    if (m_generation.load(std::memory_order_acquire) == gen) {
      m_cachedLanguages = std::move(parsed);
      m_languagesLoaded = true;
      out = m_cachedLanguages;
    } else {
      out = std::move(parsed);
    }
  }
  return true;
}

bool IPTVOrgMetadataManager::FetchCategories(std::vector<Category> &out,
                                             std::string *errorText,
                                             std::atomic<bool> *cancel) {
  const uint64_t gen = m_generation.load(std::memory_order_acquire);
  {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    if (m_categoriesLoaded) {
      out = m_cachedCategories;
      return true;
    }
  }
  std::string json;
  if (!FetchJson("https://iptv-org.github.io/api/categories.json", json,
                 errorText, cancel))
    return false;
  if (cancel && cancel->load(std::memory_order_acquire))
    return false;

  std::vector<Category> parsed;
  if (!ParseCategories(json, parsed, errorText))
    return false;

  {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    if (m_generation.load(std::memory_order_acquire) == gen) {
      m_cachedCategories = std::move(parsed);
      m_categoriesLoaded = true;
      out = m_cachedCategories;
    } else {
      out = std::move(parsed);
    }
  }
  return true;
}

void IPTVOrgMetadataManager::InvalidateCache() {
  // Bump BEFORE taking the mutex: a fetch that observed the old gen and is
  // about to lock will not store stale data.
  m_generation.fetch_add(1, std::memory_order_acq_rel);
  std::lock_guard<std::mutex> lk(m_cacheMutex);
  m_countriesLoaded = false;
  m_languagesLoaded = false;
  m_categoriesLoaded = false;
  m_cachedCountries.clear();
  m_cachedLanguages.clear();
  m_cachedCategories.clear();
  LOG_DEBUG("IPTVOrgMetadataManager: cache invalidated");
}