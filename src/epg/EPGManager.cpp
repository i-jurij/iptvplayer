#include "EPGManager.h"
#include "../Channel.h"
#include "../ConfigManager.h"
#include "../LogControl.h"
#include "../EventIDs.h"
#include "../PlaylistManager.h"
#include "EPGParserExpat.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <wx/event.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/window.h>

#include <fstream>
#include <thread>

EPGManager::EPGManager(ConfigManager *configManager,
                       PlaylistManager *playlistManager)
    : m_configManager(configManager), m_playlistManager(playlistManager) {
  LoadSourcesFromConfig();
}

EPGManager::~EPGManager() { SaveToCache(); }

void EPGManager::SetCachePath(const std::string &path) {
  m_cachePath = path;
  wxFileName dir(wxString::FromUTF8(path));
  if (!dir.DirExists()) {
    dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  m_cachePath = (wxString::FromUTF8(path) + "/epg_cache.json").ToUTF8().data();
}

bool EPGManager::LoadFromCache() {
  std::lock_guard<std::mutex> lock(m_mutex);

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
    return false;
  }

  if (!doc.IsObject())
    return false;

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

bool EPGManager::SaveToCache() const {
  std::lock_guard<std::mutex> lock(m_mutex);

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

  std::ofstream file(m_cachePath);
  if (!file.is_open()) {
    LOG_ERROR("EPGManager: Failed to open cache file for writing: %s",
              m_cachePath.c_str());
    return false;
  }
  file << buffer.GetString();
  file.close();
  LOG_DEBUG("EPGManager: Saved to cache");
  return true;
}

bool EPGManager::LoadFromUrl(const std::string &url,
                             const std::string &userAgent) {
  if (!m_playlistManager) {
    m_lastError = "PlaylistManager is null";
    LOG_ERROR("EPGManager: PlaylistManager is null");
    return false;
  }

  std::string xmlData;
  ErrorCode ec = m_playlistManager->downloadUrl(url, xmlData, userAgent);
  if (ec != ErrorCode::OK) {
    m_lastError = "Failed to download EPG from " + url + ": " +
                  m_playlistManager->getLastError();
    LOG_ERROR("EPGManager: Failed to download EPG from %s", url.c_str());
    return false;
  }

  return ParseAndMerge(xmlData, url);
}

bool EPGManager::ParseAndMerge(const std::string &xmlData,
                               const std::string &sourceUrl) {
  EPGParserExpat parser;
  if (!parser.Parse(xmlData)) {
    m_lastError = "Failed to parse XML from " + sourceUrl;
    LOG_ERROR("EPGManager: Failed to parse XML from %s", sourceUrl.c_str());
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);

  const auto &newChannels = parser.GetChannels();
  for (const auto &newCh : newChannels) {
    auto it = m_channels.find(newCh.id);
    if (it != m_channels.end()) {
      auto &existingCh = it->second;
      existingCh.programs = newCh.programs;
    } else {
      m_channels[newCh.id] = newCh;
    }
  }

  m_lastUpdate = std::time(nullptr);
  m_loaded = true;
  CleanExpiredPrograms();
  SaveToCache();

  LOG_DEBUG("EPGManager: Parsed and merged data from %s, %zu channels",
            sourceUrl.c_str(), m_channels.size());
  return true;
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
  std::thread([this]() {
    bool anySuccess = false;
    std::string lastError;
    if (m_sources.empty()) {
      lastError = "No EPG sources configured";
    } else {
      for (const auto &src : m_sources) {
        if (LoadFromUrl(src.url, "")) {
          anySuccess = true;
        } else {
          lastError = m_lastError;
        }
      }
    }
    if (anySuccess) {
      SaveToCache();
    }
    int status = anySuccess ? EPG_STATUS_OK
                            : (m_sources.empty() ? EPG_STATUS_NO_SOURCES
                                                 : EPG_STATUS_ERROR);
    wxTheApp->CallAfter([ status, lastError]() {
      wxCommandEvent evt(EVT_EPG_UPDATED);
      evt.SetInt(status);
      evt.SetString(wxString::FromUTF8(lastError));
      wxTheApp->GetTopWindow()->GetEventHandler()->ProcessEvent(evt);
    });
  }).detach();
}

void EPGManager::MatchChannels(const std::vector<Channel> &playlistChannels) {
  std::lock_guard<std::mutex> lock(m_mutex);

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
}

EpgProgram EPGManager::GetCurrentProgram(const std::string &tvgId) const {
  std::lock_guard<std::mutex> lock(m_mutex);

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
  std::lock_guard<std::mutex> lock(m_mutex);

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
  std::lock_guard<std::mutex> lock(m_mutex);
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
  std::lock_guard<std::mutex> lock(m_mutex);
  m_sources = sources;
  SaveSourcesToConfig();
}

std::vector<EpgSource> EPGManager::GetSources() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_sources;
}

void EPGManager::SetMapping(const std::string &tvgId,
                            const std::string &channelId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_channelMapping[tvgId] = channelId;
  SaveToCache();
}

std::string
EPGManager::GetEpgChannelIdForTvgId(const std::string &tvgId) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_channelMapping.find(tvgId);
  if (it != m_channelMapping.end()) {
    return it->second;
  }
  return "";
}

bool EPGManager::DeleteCache() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string filePath = m_cachePath;
  if (filePath.empty())
    return false;
  if (std::remove(filePath.c_str()) != 0) {
    if (errno != ENOENT) {
      LOG_ERROR("EPGManager: Failed to delete cache file: %s",
                filePath.c_str());
      return false;
    }
  }
  m_channels.clear();
  m_channelMapping.clear();
  m_loaded = false;
  LOG_DEBUG("EPGManager: Cache deleted");
  return true;
}

void EPGManager::RemoveChannelMapping(const std::string &tvgId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_channelMapping.erase(tvgId);
  SaveToCache();
}
