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
#include <algorithm>
#include <cctype>
#include <unordered_set>

// ---------- Конструктор / Деструктор ----------
EPGManager::EPGManager(ConfigManager *configManager,
                       PlaylistManager *playlistManager)
    : m_configManager(configManager), m_playlistManager(playlistManager) {
    LoadSourcesFromConfig();
    InitializeDefaultRegionalSuffixes();
}

EPGManager::~EPGManager() {
    WaitForRefresh();
    SaveToCache();
}

// ---------- Региональные суффиксы ----------
void EPGManager::InitializeDefaultRegionalSuffixes() {
    m_regionalSuffixes = {
        "(us)", "(uk)", "(ca)", "(au)", "(nz)", "(ie)",
        "us", "uk", "ca", "au", "nz", "ie",
        "(de)", "(fr)", "(es)", "(it)", "(pt)", "(nl)", "(be)",
        "de", "fr", "es", "it", "pt", "nl", "be",
        "(ru)", "(ua)", "(by)", "(kz)",
        "ru", "ua", "by", "kz",
        "(jp)", "(kr)", "(cn)", "(tw)", "(hk)",
        "jp", "kr", "cn", "tw", "hk"
    };
}

void EPGManager::LoadRegionalSuffixes(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_DEBUG("EPGManager: Regional suffixes file not found, using defaults");
        InitializeDefaultRegionalSuffixes();
        return;
    }

    std::string json((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    file.close();

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsArray()) {
        LOG_WARN("EPGManager: Failed to parse regional_suffixes.json, using defaults");
        InitializeDefaultRegionalSuffixes();
        return;
    }

    m_regionalSuffixes.clear();
    for (const auto& item : doc.GetArray()) {
        if (item.IsString()) {
            m_regionalSuffixes.push_back(item.GetString());
        }
    }

    if (m_regionalSuffixes.empty()) {
        LOG_WARN("EPGManager: regional_suffixes.json is empty, using defaults");
        InitializeDefaultRegionalSuffixes();
    } else {
        LOG_DEBUG("EPGManager: Loaded %zu regional suffixes", m_regionalSuffixes.size());
    }
}

void EPGManager::SetRegionalSuffixes(const std::vector<std::string>& suffixes) {
    m_regionalSuffixes = suffixes;
}

// ---------- Кэширование ----------
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
        m_manualMapping.clear();
        m_loaded = false;
        return false;
    }

    if (!doc.IsObject()) {
        setLastError("Cache JSON is not an object");
        m_channels.clear();
        m_channelMapping.clear();
        m_manualMapping.clear();
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

    if (doc.HasMember("manualMapping") && doc["manualMapping"].IsObject()) {
        const auto &mapObj = doc["manualMapping"].GetObject();
        m_manualMapping.clear();
        for (const auto &m : mapObj) {
            if (m.value.IsString()) {
                m_manualMapping[m.name.GetString()] = m.value.GetString();
            }
        }
    }

    m_loaded = true;
    CleanExpiredPrograms();
    LOG_DEBUG("EPGManager: Loaded from cache, %zu channels", m_channels.size());
    return true;
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

    rapidjson::Value manualObj(rapidjson::kObjectType);
    for (const auto &m : m_manualMapping) {
        manualObj.AddMember(rapidjson::Value(m.first.c_str(), allocator),
                            rapidjson::Value(m.second.c_str(), allocator), allocator);
    }
    doc.AddMember("manualMapping", manualObj, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

// ---------- Загрузка из URL и парсинг ----------
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

// ---------- Очистка устаревших программ ----------
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

// ---------- Обновление ----------
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

// ---------- Нормализация ----------
void EPGManager::NormalizeTvgId(std::string& id) const {
    std::transform(id.begin(), id.end(), id.begin(), ::tolower);
    id.erase(std::remove(id.begin(), id.end(), '.'), id.end());
    id.erase(std::remove(id.begin(), id.end(), ' '), id.end());
    id.erase(std::remove(id.begin(), id.end(), '_'), id.end());
}

std::string EPGManager::NormalizeName(const std::string& name) const {
    std::string result = name;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);

    // Суффиксы качества
    const std::vector<std::string> qualitySuffixes = {
        "(hd)", "(fhd)", "(4k)", "(uhd)", "(sd)",
        "hd", "fhd", "4k", "uhd", "sd",
        "[hd]", "[fhd]", "[4k]", "[uhd]", "[sd]"
    };
    for (const auto& suffix : qualitySuffixes) {
        size_t pos = result.rfind(suffix);
        if (pos != std::string::npos && pos + suffix.length() == result.length()) {
            result = result.substr(0, pos);
            while (!result.empty() && result.back() == ' ')
                result.pop_back();
            break;
        }
    }

    // Региональные суффиксы (из конфига или дефолтные)
    for (const auto& suffix : m_regionalSuffixes) {
        size_t pos = result.rfind(suffix);
        if (pos != std::string::npos && pos + suffix.length() == result.length()) {
            result = result.substr(0, pos);
            while (!result.empty() && result.back() == ' ')
                result.pop_back();
            break;
        }
    }

    // Заменяем & на and
    size_t pos = result.find('&');
    while (pos != std::string::npos) {
        result.replace(pos, 1, "and");
        pos = result.find('&', pos + 3);
    }

    // Удаляем лишние пробелы
    result.erase(std::unique(result.begin(), result.end(),
        [](char a, char b) { return a == ' ' && b == ' '; }), result.end());

    // Удаляем пунктуацию (кроме +)
    result.erase(std::remove_if(result.begin(), result.end(),
        [](char c) { return std::ispunct(c) && c != '+'; }), result.end());

    return result;
}

std::vector<std::string> EPGManager::Tokenize(const std::string& name) const {
    std::vector<std::string> tokens;
    std::string token;
    for (char c : name) {
        if (c == ' ') {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

int EPGManager::LevenshteinDistance(const std::string& s1, const std::string& s2) const {
    const size_t m = s1.size();
    const size_t n = s2.size();
    if (m == 0) return static_cast<int>(n);
    if (n == 0) return static_cast<int>(m);
    std::vector<int> v0(n+1), v1(n+1);
    for (size_t i = 0; i <= n; ++i) v0[i] = static_cast<int>(i);
    for (size_t i = 0; i < m; ++i) {
        v1[0] = static_cast<int>(i + 1);
        for (size_t j = 0; j < n; ++j) {
            int cost = (s1[i] == s2[j]) ? 0 : 1;
            v1[j+1] = std::min({v1[j] + 1, v0[j+1] + 1, v0[j] + cost});
        }
        v0.swap(v1);
    }
    return v0[n];
}

int EPGManager::CalculateNameScore(const std::string& name1, const std::string& name2) const {
    if (name1.empty() || name2.empty()) return 0;

    if (name1 == name2) return 100;

    if (name1.find(name2) != std::string::npos || name2.find(name1) != std::string::npos) {
        return 85;
    }

    auto tokens1 = Tokenize(name1);
    auto tokens2 = Tokenize(name2);
    if (tokens1.size() == tokens2.size()) {
        std::sort(tokens1.begin(), tokens1.end());
        std::sort(tokens2.begin(), tokens2.end());
        if (tokens1 == tokens2) return 80;
    }

    int dist = LevenshteinDistance(name1, name2);
    int maxLen = std::max(name1.size(), name2.size());
    if (maxLen == 0) return 0;
    int similarity = 100 - (dist * 100) / maxLen;
    if (similarity >= 60) return similarity;

    std::unordered_set<std::string> set1(tokens1.begin(), tokens1.end());
    std::unordered_set<std::string> set2(tokens2.begin(), tokens2.end());
    int common = 0;
    for (const auto& t : set1) {
        if (set2.find(t) != set2.end()) common++;
    }
    int total = std::max(set1.size(), set2.size());
    if (total > 0 && common > 0) {
        int overlapScore = (common * 100) / total;
        if (overlapScore >= 70) return overlapScore;
    }

    return similarity;
}

// ---------- Сопоставление ----------
void EPGManager::RebuildNormalizedCache() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_normalizedCache.clear();
    for (const auto& ch : m_channels) {
        NormalizedChannel nc;
        nc.id = ch.first;
        nc.normalizedName = NormalizeName(ch.second.displayName);
        nc.tokens = Tokenize(nc.normalizedName);
        m_normalizedCache.push_back(nc);
    }
}

EPGManager::MatchResult EPGManager::FindBestMatch(const Channel& playlistChannel) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    MatchResult result;
    result.score = 0;
    result.confidence = "low";

    std::string tvgId = playlistChannel.getTvgId();
    std::string channelName = playlistChannel.getName();

    // TIER 1: Ручное сопоставление
    if (!tvgId.empty()) {
        auto manualIt = m_manualMapping.find(tvgId);
        if (manualIt != m_manualMapping.end()) {
            auto it = m_channels.find(manualIt->second);
            if (it != m_channels.end()) {
                result.channelId = it->first;
                result.method = "manual";
                result.score = 100;
                result.confidence = "high";
                return result;
            }
        }
    }

    // TIER 2: Точное совпадение по нормализованному имени (ОСНОВНОЙ МЕТОД)
    std::string plName = NormalizeName(channelName);
    if (!plName.empty()) {
        for (const auto& nc : m_normalizedCache) {
            if (nc.normalizedName == plName) {
                result.channelId = nc.id;
                result.method = "exact_name";
                result.score = 95;
                result.confidence = "high";
                return result;
            }
        }
    }

    // TIER 3: Token-sort
    auto plTokens = Tokenize(plName);
    std::sort(plTokens.begin(), plTokens.end());
    for (const auto& nc : m_normalizedCache) {
        auto epgTokens = nc.tokens;
        std::sort(epgTokens.begin(), epgTokens.end());
        if (plTokens.size() == epgTokens.size() && plTokens == epgTokens) {
            result.channelId = nc.id;
            result.method = "token_sort";
            result.score = 85;
            result.confidence = "medium";
            return result;
        }
    }

    // TIER 4: Нечёткое сравнение
    int bestScore = 0;
    std::string bestId;
    for (const auto& nc : m_normalizedCache) {
        int similarity = CalculateNameScore(plName, nc.normalizedName);
        if (similarity > bestScore && similarity >= 60) {
            bestScore = similarity;
            bestId = nc.id;
        }
    }
    if (!bestId.empty()) {
        result.channelId = bestId;
        result.method = "fuzzy";
        result.score = bestScore;
        result.confidence = (bestScore >= 80) ? "medium" : "low";
        return result;
    }

    // TIER 5: Substring (last resort по имени)
    if (plName.length() > 3) {
        for (const auto& nc : m_normalizedCache) {
            if (nc.normalizedName.find(plName) != std::string::npos ||
                plName.find(nc.normalizedName) != std::string::npos) {
                result.channelId = nc.id;
                result.method = "substring";
                result.score = 50;
                result.confidence = "low";
                return result;
            }
        }
    }

    // TIER 6: Точное совпадение по TVG-ID (FALLBACK)
    if (!tvgId.empty()) {
        std::string normalizedTvgId = tvgId;
        NormalizeTvgId(normalizedTvgId);
        for (const auto& ch : m_channels) {
            std::string epgId = ch.first;
            NormalizeTvgId(epgId);
            if (epgId == normalizedTvgId) {
                result.channelId = ch.first;
                result.method = "exact_tvgid_fallback";
                result.score = 70;
                result.confidence = "medium";
                return result;
            }
        }
    }

    return result;
}

void EPGManager::MatchChannels(const std::vector<Channel>& playlistChannels) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    RebuildNormalizedCache();
    
    m_channelMapping.clear();
    int matched = 0;
    int total = static_cast<int>(playlistChannels.size());
    
    for (const auto& ch : playlistChannels) {
        MatchResult match = FindBestMatch(ch);
        if (!match.channelId.empty()) {
            std::string key = ch.getTvgId();
            if (key.empty()) {
                key = "name:" + ch.getName();
            }
            m_channelMapping[key] = match.channelId;
            matched++;
            
            LOG_DEBUG("EPGManager: Matched '%s' → EPG '%s' (method=%s, score=%d, conf=%s)",
                      ch.getName().c_str(), match.channelId.c_str(),
                      match.method.c_str(), match.score, match.confidence.c_str());
        } else {
            LOG_DEBUG("EPGManager: No match for '%s' (tvgId='%s')",
                      ch.getName().c_str(), ch.getTvgId().c_str());
        }
    }
    
    LOG_INFO("EPGManager: Matched %d/%d channels (%.1f%%)",
             matched, total, (total > 0) ? (matched * 100.0 / total) : 0);
    
    SaveToCache();
}

// ---------- Получение программ ----------
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

std::vector<EpgProgram> EPGManager::GetProgramsForChannel(const std::string &tvgId,
                                                         const std::string &channelName,
                                                         time_t date) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<EpgProgram> result;
    std::string channelId;

    // Сначала по маппингу (ключ = tvgId или "name:...")
    if (!tvgId.empty()) {
        auto mapIt = m_channelMapping.find(tvgId);
        if (mapIt != m_channelMapping.end()) {
            channelId = mapIt->second;
        }
    }
    if (channelId.empty() && !channelName.empty()) {
        auto mapIt = m_channelMapping.find("name:" + channelName);
        if (mapIt != m_channelMapping.end()) {
            channelId = mapIt->second;
        }
    }
    // Если всё ещё не найдено, пробуем найти по имени напрямую (на всякий случай)
    if (channelId.empty() && !channelName.empty()) {
        std::string plName = NormalizeName(channelName);
        for (const auto& nc : m_normalizedCache) {
            if (nc.normalizedName == plName) {
                channelId = nc.id;
                break;
            }
        }
    }

    if (channelId.empty()) {
        return result;
    }

    auto chIt = m_channels.find(channelId);
    if (chIt == m_channels.end()) {
        return result;
    }

    time_t startOfDay = EpgTime::GetStartOfDay(date);
    time_t endOfDay = EpgTime::GetEndOfDay(date);

    const auto& programs = chIt->second.programs;
    for (const auto& prog : programs) {
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

// ---------- Управление источниками ----------
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

// ---------- Ручное сопоставление ----------
void EPGManager::SetManualMapping(const std::string &tvgId, const std::string &epgId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_manualMapping[tvgId] = epgId;
    SaveToCache();
}

void EPGManager::SetMapping(const std::string &tvgId, const std::string &channelId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_channelMapping[tvgId] = channelId;
    SaveToCache();
}

void EPGManager::RemoveChannelMapping(const std::string &tvgId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_channelMapping.erase(tvgId);
    SaveToCache();
}

std::string EPGManager::GetEpgChannelIdForTvgId(const std::string &tvgId) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_channelMapping.find(tvgId);
    if (it != m_channelMapping.end()) {
        return it->second;
    }
    return "";
}

// ---------- Удаление кэша ----------
bool EPGManager::DeleteCache() {
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_channels.clear();
        m_channelMapping.clear();
        m_manualMapping.clear();
        m_loaded = false;
    }
    SaveToCache();
    if (!m_cachePath.empty()) {
        std::remove(m_cachePath.c_str());
    }
    LOG_DEBUG("EPGManager: Cache deleted");
    return true;
}

// ---------- Ошибки ----------
std::string EPGManager::getLastError() const {
    std::lock_guard<std::mutex> lock(m_lastErrorMutex);
    return m_lastError;
}

void EPGManager::setLastError(const std::string &msg) const {
    std::lock_guard<std::mutex> lock(m_lastErrorMutex);
    m_lastError = msg;
}
