#include "IPTVOrgMetadataManager.h"
#include "../LogControl.h"
#include "../PlaylistManager.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

IPTVOrgMetadataManager::IPTVOrgMetadataManager(PlaylistManager *playlistMgr)
    : m_playlistMgr(playlistMgr) {}

bool IPTVOrgMetadataManager::FetchJson(const std::string &url,
                                       std::string &outContent) {
  if (!m_playlistMgr) {
    LOG_ERROR("IPTVOrgMetadataManager: PlaylistManager is null");
    return false;
  }
  ErrorCode ec = m_playlistMgr->downloadUrl(url, outContent, "");
  if (ec != ErrorCode::OK) {
    LOG_ERROR("IPTVOrgMetadataManager: Failed to download %s, error: %s",
              url.c_str(), m_playlistMgr->getLastError().c_str());
    return false;
  }
  return true;
}

bool IPTVOrgMetadataManager::ParseCountries(const std::string &json,
                                            std::vector<Country> &out) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  if (doc.HasParseError()) {
    LOG_ERROR("IPTVOrgMetadataManager: Failed to parse countries JSON: %s",
              rapidjson::GetParseError_En(doc.GetParseError()));
    return false;
  }
  if (!doc.IsArray())
    return false;
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
                                            std::vector<Language> &out) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  if (doc.HasParseError()) {
    LOG_ERROR("IPTVOrgMetadataManager: Failed to parse languages JSON: %s",
              rapidjson::GetParseError_En(doc.GetParseError()));
    return false;
  }
  if (!doc.IsArray())
    return false;
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
                                             std::vector<Category> &out) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  if (doc.HasParseError()) {
    LOG_ERROR("IPTVOrgMetadataManager: Failed to parse categories JSON: %s",
              rapidjson::GetParseError_En(doc.GetParseError()));
    return false;
  }
  if (!doc.IsArray())
    return false;
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

bool IPTVOrgMetadataManager::FetchCountries(std::vector<Country> &out) {
  if (m_countriesLoaded) {
    out = m_cachedCountries;
    return true;
  }
  std::string json;
  if (!FetchJson("https://iptv-org.github.io/api/countries.json", json))
    return false;
  if (!ParseCountries(json, m_cachedCountries))
    return false;
  m_countriesLoaded = true;
  out = m_cachedCountries;
  return true;
}

bool IPTVOrgMetadataManager::FetchLanguages(std::vector<Language> &out) {
  if (m_languagesLoaded) {
    out = m_cachedLanguages;
    return true;
  }
  std::string json;
  if (!FetchJson("https://iptv-org.github.io/api/languages.json", json))
    return false;
  if (!ParseLanguages(json, m_cachedLanguages))
    return false;
  m_languagesLoaded = true;
  out = m_cachedLanguages;
  return true;
}

bool IPTVOrgMetadataManager::FetchCategories(std::vector<Category> &out) {
  if (m_categoriesLoaded) {
    out = m_cachedCategories;
    return true;
  }
  std::string json;
  if (!FetchJson("https://iptv-org.github.io/api/categories.json", json))
    return false;
  if (!ParseCategories(json, m_cachedCategories))
    return false;
  m_categoriesLoaded = true;
  out = m_cachedCategories;
  return true;
}

void IPTVOrgMetadataManager::InvalidateCache() {
  m_countriesLoaded = false;
  m_languagesLoaded = false;
  m_categoriesLoaded = false;
  m_cachedCountries.clear();
  m_cachedLanguages.clear();
  m_cachedCategories.clear();
  LOG_DEBUG("IPTVOrgMetadataManager: cache invalidated");
}
