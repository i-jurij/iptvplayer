#include "EPGManager.h"
#include "../Channel.h"
#include "../ConfigManager.h"
#include "../EventIDs.h"
#include "../LogControl.h"
#include "../PlaylistManager.h"
#include "EPGParserExpat.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <wx/event.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/window.h>

#include <chrono>
#include <fstream>
#include <future>
#include <thread>

EPGManager::EPGManager(ConfigManager *configManager,
                       PlaylistManager *playlistManager)
    : m_configManager(configManager), m_playlistManager(playlistManager) {
  LoadSourcesFromConfig();
}

EPGManager::~EPGManager() {
  WaitForRefresh();
  SaveToCache();
}

void EPGManager::SetCachePath(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_cachePath = path;
  wxFileName dir(wxString::FromUTF8(path));
  if (!dir.DirExists()) {
    dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  m_cachePath = (wxString::FromUTF8(path) + "/epg_cache.json").ToUTF8().data();
}

bool EPGManager::LoadFromCache() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);

  std::ifstream file(m_cachePath);
  if (!file.is_open()) {
    LOG_DEBUG("EPGManager: Cache file not found, will load from network.");
    return false;
  }

  std::string json((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  file.close();

  rapidjson::Document doc;
  if (doc.Parse(json.c_str()).HasParseError()) {
    LOG_ERROR("EPGManager: Failed to parse cache JSON");
    setLastError("Failed to parse cache JSON");
    m_channels.clear();
    m_channelMapping.clear();
    m_loaded = false;
    return false;
  }

  if (!doc.IsObject()) {
    setLastError("Cache JSON is not an object");
    m_channels.clear();
    m_channelMapping.clear();
    m_loaded = false;
    return false;
  }

  if (doc.HasMember("lastUpdate") && doc["lastUpdate"].IsInt64()) {
    m_lastUpdate = doc["lastUpdate"].GetInt64();
  }

  if (doc.HasMember("channels") && doc["channels"].IsArray()) {
    const auto &chArray = doc["channels"].GetArray();
    m_channels.clear();
    for (const auto &chVal : chArray) {
      EpgChannel ch;
      if (chVal.HasMember("id") && chVal["id"].IsString()) {
        ch.id = chVal["id"].GetString();
      }
      if (chVal.HasMember("displayName") && chVal["displayName"].IsString()) {
        ch.displayName = chVal["displayName"].GetString();
      }
      if (chVal.HasMember("programs") && chVal["programs"].IsArray()) {
        const auto &progArray = chVal["programs"].GetArray();
        for (const auto &progVal : progArray) {
          EpgProgram prog;
          if (progVal.HasMember("title") && progVal["title"].IsString()) {
            prog.title = progVal["title"].GetString();
          }
          if (progVal.HasMember("description") &&
              progVal["description"].IsString()) {
            prog.description = progVal["description"].GetString();
          }
          if (progVal.HasMember("category") && progVal["category"].IsString()) {
            prog.category = progVal["category"].GetString();
          }
          if (progVal.HasMember("startTime") &&
              progVal["startTime"].IsInt64()) {
            prog.startTime = progVal["startTime"].GetInt64();
          }
          if (progVal.HasMember("stopTime") && progVal["stopTime"].IsInt64()) {
            prog.stopTime = progVal["stopTime"].GetInt64();
          }
          if (progVal.HasMember("channelId") &&
              progVal["channelId"].IsString()) {
            prog.channelId = progVal["channelId"].GetString();
          }
          ch.programs.push_back(prog);
        }
      }
      m_channels[ch.id] = ch;
    }
  }

  if (doc.HasMember("mapping") && doc["mapping"].IsObject()) {
    const auto &mapObj = doc["mapping"].GetObject();
    m_channelMapping.clear();
    for (const auto &m : mapObj) {
      if (m.value.IsString()) {
        m_channelMapping[m.name.GetString()] = m.value.GetString();
      }
    }
  }

  m_loaded = true;
  CleanExpiredPrograms();
  LOG_DEBUG("EPGManager: Loaded from cache, %zu channels", m_channels.size());
  return true;
}

bool EPGManager::LoadFromUrl(const std::string &url,
                             const std::string &userAgent) {
  if (!m_playlistManager) {
    setLastError("PlaylistManager is null");
    LOG_ERROR("EPGManager: PlaylistManager is null");
    return false;
  }

  std::string xmlData;
  ErrorCode ec = m_playlistManager->downloadUrl(url, xmlData, userAgent);
  if (ec != ErrorCode::OK) {
    setLastError("Failed to download EPG from " + url + ": " +
                 m_playlistManager->getLastError());
    LOG_ERROR("EPGManager: Failed to download EPG from %s", url.c_str());
    return false;
  }

  return ParseAndMerge(xmlData, url);
}

void EPGManager::CleanExpiredPrograms() {
  time_t now = std::time(nullptr);
  time_t threshold = now - m_daysToKeep * 24 * 3600;
  for (auto &chPair : m_channels) {
    auto &programs = chPair.second.programs;
    programs.erase(std::remove_if(programs.begin(), programs.end(),
                                  [threshold](const EpgProgram &prog) {
                                    return prog.stopTime < threshold;
                                  }),
                   programs.end());
  }
}

void EPGManager::Refresh() {
  if (m_refreshFuture.valid()) {
    auto status = m_refreshFuture.wait_for(std::chrono::seconds(1));
    if (status == std::future_status::timeout) {
      LOG_WARN("EPGManager::Refresh: previous refresh task is still running, "
               "starting new one anyway.");
    }
  }

  m_refreshFuture = std::async(std::launch::async, [this]() {
    std::vector<EpgSource> sourcesCopy;
    {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      sourcesCopy = m_sources;
    }
    bool anySuccess = false;
    std::string lastError;
    if (sourcesCopy.empty()) {
      lastError = "No EPG sources configured";
      setLastError(lastError);
    } else {
      for (const auto &src : sourcesCopy) {
        if (LoadFromUrl(src.url, "")) {
          anySuccess = true;
        } else {
          lastError = getLastError();
        }
      }
    }
    if (anySuccess) {
      SaveToCache();
    }
    int status = anySuccess ? EPG_STATUS_OK
                            : (sourcesCopy.empty() ? EPG_STATUS_NO_SOURCES
                                                   : EPG_STATUS_ERROR);
    wxTheApp->CallAfter([status, lastError]() {
      wxCommandEvent evt(EVT_EPG_UPDATED);
      evt.SetInt(status);
      evt.SetString(wxString::FromUTF8(lastError));
      wxTheApp->GetTopWindow()->GetEventHandler()->ProcessEvent(evt);
    });
  });
}

void EPGManager::WaitForRefresh() {
  if (m_refreshFuture.valid()) {
    auto status = m_refreshFuture.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::timeout) {
      LOG_WARN("EPGManager::WaitForRefresh: timeout waiting for refresh task, "
               "proceeding anyway.");
    }
  }
}

void EPGManager::MatchChannels(const std::vector<Channel> &playlistChannels) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    // Полная перестройка маппинга на основе переданных каналов
    m_channelMapping.clear();
    
    for (const auto &ch : playlistChannels) {
        std::string tvgId = ch.getTvgId();
        if (tvgId.empty())
            continue;
        auto it = m_channels.find(tvgId);
        if (it != m_channels.end()) {
            m_channelMapping[tvgId] = tvgId;
        }
    }
    
    LOG_DEBUG("EPGManager: Matched %zu channels", m_channelMapping.size());
    SaveToCache();
}

EpgProgram EPGManager::GetCurrentProgram(const std::string &tvgId) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);

  auto mapIt = m_channelMapping.find(tvgId);
  if (mapIt == m_channelMapping.end()) {
    return EpgProgram();
  }

  const std::string &channelId = mapIt->second;
  auto chIt = m_channels.find(channelId);
  if (chIt == m_channels.end()) {
    return EpgProgram();
  }

  time_t now = std::time(nullptr);
  const auto &programs = chIt->second.programs;
  for (const auto &prog : programs) {
    if (prog.startTime <= now && prog.stopTime > now) {
      return prog;
    }
  }
  return EpgProgram();
}

std::vector<EpgProgram>
EPGManager::GetProgramsForChannel(const std::string &tvgId, time_t date) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);

  std::vector<EpgProgram> result;

  auto mapIt = m_channelMapping.find(tvgId);
  if (mapIt == m_channelMapping.end()) {
    return result;
  }

  const std::string &channelId = mapIt->second;
  auto chIt = m_channels.find(channelId);
  if (chIt == m_channels.end()) {
    return result;
  }

  time_t startOfDay = EpgTime::GetStartOfDay(date);
  time_t endOfDay = EpgTime::GetEndOfDay(date);

  const auto &programs = chIt->second.programs;
  for (const auto &prog : programs) {
    if (prog.startTime <= endOfDay && prog.stopTime >= startOfDay) {
      result.push_back(prog);
    }
  }
  return result;
}

std::vector<std::string> EPGManager::GetChannelIdsWithEpg() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  std::vector<std::string> ids;
  ids.reserve(m_channels.size());
  for (const auto &ch : m_channels) {
    ids.push_back(ch.first);
  }
  return ids;
}

void EPGManager::LoadSourcesFromConfig() {
  if (!m_configManager) {
    LOG_ERROR("EPGManager: ConfigManager is null");
    return;
  }

  std::string sourcesJson = m_configManager->getSetting("epg_sources", "[]");
  rapidjson::Document doc;
  if (doc.Parse(sourcesJson.c_str()).HasParseError()) {
    LOG_ERROR("EPGManager: Failed to parse epg_sources JSON");
    return;
  }
  if (!doc.IsArray()) {
    LOG_ERROR("EPGManager: epg_sources is not an array");
    return;
  }

  m_sources.clear();
  for (const auto &item : doc.GetArray()) {
    EpgSource src;
    if (item.HasMember("url") && item["url"].IsString()) {
      src.url = item["url"].GetString();
    }
    if (item.HasMember("name") && item["name"].IsString()) {
      src.name = item["name"].GetString();
    }
    if (item.HasMember("lastUpdate") && item["lastUpdate"].IsInt64()) {
      src.lastUpdate = item["lastUpdate"].GetInt64();
    }
    m_sources.push_back(src);
  }
}

void EPGManager::SaveSourcesToConfig() const {
  if (!m_configManager) {
    LOG_ERROR("EPGManager: ConfigManager is null");
    return;
  }

  rapidjson::Document doc;
  doc.SetArray();
  auto &allocator = doc.GetAllocator();
  for (const auto &src : m_sources) {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("url", rapidjson::Value(src.url.c_str(), allocator),
                  allocator);
    obj.AddMember("name", rapidjson::Value(src.name.c_str(), allocator),
                  allocator);
    obj.AddMember("lastUpdate", static_cast<int64_t>(src.lastUpdate),
                  allocator);
    doc.PushBack(obj, allocator);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  m_configManager->setSetting("epg_sources", buffer.GetString());
  m_configManager->saveSettings();
}

void EPGManager::SetSources(const std::vector<EpgSource> &sources) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_sources = sources;
  SaveSourcesToConfig();
}

std::vector<EpgSource> EPGManager::GetSources() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return m_sources;
}

std::string
EPGManager::GetEpgChannelIdForTvgId(const std::string &tvgId) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  auto it = m_channelMapping.find(tvgId);
  if (it != m_channelMapping.end()) {
    return it->second;
  }
  return "";
}

std::string EPGManager::BuildCacheJson() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);

  rapidjson::Document doc;
  doc.SetObject();
  auto &allocator = doc.GetAllocator();

  doc.AddMember("lastUpdate", static_cast<int64_t>(m_lastUpdate), allocator);

  rapidjson::Value channelsArray(rapidjson::kArrayType);
  for (const auto &chPair : m_channels) {
    const auto &ch = chPair.second;
    rapidjson::Value chVal(rapidjson::kObjectType);
    chVal.AddMember("id", rapidjson::Value(ch.id.c_str(), allocator),
                    allocator);
    chVal.AddMember("displayName",
                    rapidjson::Value(ch.displayName.c_str(), allocator),
                    allocator);

    rapidjson::Value progArray(rapidjson::kArrayType);
    for (const auto &prog : ch.programs) {
      rapidjson::Value progVal(rapidjson::kObjectType);
      progVal.AddMember(
          "title", rapidjson::Value(prog.title.c_str(), allocator), allocator);
      progVal.AddMember("description",
                        rapidjson::Value(prog.description.c_str(), allocator),
                        allocator);
      progVal.AddMember("category",
                        rapidjson::Value(prog.category.c_str(), allocator),
                        allocator);
      progVal.AddMember("startTime", static_cast<int64_t>(prog.startTime),
                        allocator);
      progVal.AddMember("stopTime", static_cast<int64_t>(prog.stopTime),
                        allocator);
      progVal.AddMember("channelId",
                        rapidjson::Value(prog.channelId.c_str(), allocator),
                        allocator);
      progArray.PushBack(progVal, allocator);
    }
    chVal.AddMember("programs", progArray, allocator);
    channelsArray.PushBack(chVal, allocator);
  }
  doc.AddMember("channels", channelsArray, allocator);

  rapidjson::Value mapObj(rapidjson::kObjectType);
  for (const auto &m : m_channelMapping) {
    mapObj.AddMember(rapidjson::Value(m.first.c_str(), allocator),
                     rapidjson::Value(m.second.c_str(), allocator), allocator);
  }
  doc.AddMember("mapping", mapObj, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return buffer.GetString();
}

bool EPGManager::SaveToCache() const {
  std::string jsonData = BuildCacheJson();

  std::ofstream file(m_cachePath);
  if (!file.is_open()) {
    LOG_ERROR("EPGManager: Failed to open cache file for writing: %s",
              m_cachePath.c_str());
    return false;
  }
  file << jsonData;
  file.close();
  LOG_DEBUG("EPGManager: Saved to cache");
  return true;
}

bool EPGManager::ParseAndMerge(const std::string &xmlData,
                               const std::string &sourceUrl) {
  EPGParserExpat parser;
  if (!parser.Parse(xmlData)) {
    setLastError("Failed to parse XML from " + sourceUrl);
    LOG_ERROR("EPGManager: Failed to parse XML from %s", sourceUrl.c_str());
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const auto &newChannels = parser.GetChannels();
    for (const auto &newCh : newChannels) {
      auto it = m_channels.find(newCh.id);
      if (it != m_channels.end()) {
        it->second.programs = newCh.programs;
      } else {
        m_channels[newCh.id] = newCh;
      }
    }
    m_lastUpdate = std::time(nullptr);
    m_loaded = true;
    CleanExpiredPrograms();
  }

  SaveToCache();
  LOG_DEBUG("EPGManager: Parsed and merged data from %s, %zu channels",
            sourceUrl.c_str(), m_channels.size());
  return true;
}

void EPGManager::SetMapping(const std::string &tvgId,
                            const std::string &channelId) {
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_channelMapping[tvgId] = channelId;
  }
  SaveToCache();
}

void EPGManager::RemoveChannelMapping(const std::string &tvgId) {
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_channelMapping.erase(tvgId);
  }
  SaveToCache();
}

bool EPGManager::DeleteCache() {
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_channels.clear();
    m_channelMapping.clear();
    m_loaded = false;
  }
  SaveToCache();
  if (!m_cachePath.empty()) {
    std::remove(m_cachePath.c_str());
  }
  LOG_DEBUG("EPGManager: Cache deleted");
  return true;
}

std::string EPGManager::getLastError() const {
  std::lock_guard<std::mutex> lock(m_lastErrorMutex);
  return m_lastError;
}

void EPGManager::setLastError(const std::string &msg) const {
  std::lock_guard<std::mutex> lock(m_lastErrorMutex);
  m_lastError = msg;
}
