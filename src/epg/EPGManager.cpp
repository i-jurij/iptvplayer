#include "EPGManager.h"
#include "../Channel.h"
#include "../ConfigManager.h"
#include "../LogControl.h"
#include "../PlaylistManager.h"
#include "EPGParserExpat.h"
#include "HashUtils.h"
#include "Utils.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <wx/event.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/stdpaths.h>
#include <wx/stream.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/window.h>
#include <wx/zipstrm.h>

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <memory>
#include <regex>
#include <unordered_set>

namespace {
  // Сжатие множественных пробелов и удаление ведущих/завершающих пробелов
  static void TrimAndCollapseSpaces(std::string &s) {
    s = std::regex_replace(s, std::regex("\\s+"), " ");
    s = std::regex_replace(s, std::regex("^\\s+|\\s+$"), "");
  }

  // Удаление суффикса качества (число+p/i в скобках) в конце строки
  static std::string RemoveQualityNumericSuffix(const std::string &s) {
    static std::regex pattern(R"(\s*\(\s*\d+[pi]\s*\)\s*$)");
    return std::regex_replace(s, pattern, "");
  }

  // --------------------------------------------------------------------------
  // Вспомогательные статические функции
  // --------------------------------------------------------------------------
  static bool IsValidXmltv(const std::string &data) {
    if (data.empty())
      return false;
    std::string lower = data;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.find("<!doctype html") != std::string::npos ||
        lower.find("<html") != std::string::npos) {
      LOG_ERROR("IsValidXmltv: Data appears to be HTML (not XMLTV)");
      return false;
    }
    return lower.find("<?xml") != std::string::npos ||
          lower.find("<tv") != std::string::npos;
  }

  static bool DecompressIfNeeded(std::string &data) {
    // GZIP
    if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1F &&
        static_cast<unsigned char>(data[1]) == 0x8B) {
      std::vector<unsigned char> inbuf(data.begin(), data.end());
      z_stream zs;
      memset(&zs, 0, sizeof(zs));
      if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        LOG_ERROR("DecompressIfNeeded: inflateInit2 failed");
        return false;
      }
      zs.next_in = inbuf.data();
      zs.avail_in = inbuf.size();

      std::string out;
      char buf[16384];
      int ret;
      do {
        zs.next_out = reinterpret_cast<Bytef *>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
          inflateEnd(&zs);
          LOG_ERROR("DecompressIfNeeded: inflate error %d", ret);
          return false;
        }
        out.append(buf, sizeof(buf) - zs.avail_out);
      } while (ret != Z_STREAM_END);
      inflateEnd(&zs);

      if (out.size() > 1024 * 1024 * 1024) {
        LOG_ERROR("DecompressIfNeeded: decompressed data exceeds 1 GB");
        return false;
      }
      if (out.empty()) {
        LOG_ERROR("DecompressIfNeeded: decompressed GZIP data is empty");
        return false;
      }
      if (!IsValidXmltv(out)) {
        LOG_ERROR("DecompressIfNeeded: decompressed gzip is not valid XMLTV");
        return false;
      }
      data.swap(out);
      return true;
    }

    // ZIP
    if (data.size() >= 4 && static_cast<unsigned char>(data[0]) == 0x50 &&
        static_cast<unsigned char>(data[1]) == 0x4B &&
        static_cast<unsigned char>(data[2]) == 0x03 &&
        static_cast<unsigned char>(data[3]) == 0x04) {
      wxMemoryInputStream memIn(data.data(), data.size());
      wxZipInputStream zipIn(memIn);
      if (!zipIn.IsOk()) {
        LOG_ERROR("DecompressIfNeeded: wxZipInputStream not OK");
        return false;
      }

      std::string extracted;
      bool foundXml = false;
      std::unique_ptr<wxZipEntry> entry(zipIn.GetNextEntry());
      while (entry) {
        if (!entry->IsDir()) {
          wxString name = entry->GetName();
          bool isXml = name.EndsWith(".xml") || name.EndsWith(".XML") ||
                      name.EndsWith(".xml.gz") || name.EndsWith(".XML.GZ");
          if (isXml) {
            wxMemoryOutputStream memOut;
            zipIn.Read(memOut);
            if (zipIn.GetLastError() != wxSTREAM_NO_ERROR) {
              LOG_ERROR("DecompressIfNeeded: error reading ZIP entry");
              return false;
            }
            wxStreamBuffer *buf = memOut.GetOutputStreamBuffer();
            if (buf) {
              extracted.assign(static_cast<const char *>(buf->GetBufferStart()),
                              buf->GetBufferSize());
              foundXml = true;
              break;
            }
          }
        }
        entry.reset(zipIn.GetNextEntry());
      }

      if (!foundXml) {
        wxMemoryInputStream memIn2(data.data(), data.size());
        wxZipInputStream zipIn2(memIn2);
        if (zipIn2.IsOk()) {
          std::unique_ptr<wxZipEntry> entry2(zipIn2.GetNextEntry());
          while (entry2) {
            if (!entry2->IsDir()) {
              wxMemoryOutputStream memOut2;
              zipIn2.Read(memOut2);
              if (zipIn2.GetLastError() == wxSTREAM_NO_ERROR) {
                wxStreamBuffer *buf2 = memOut2.GetOutputStreamBuffer();
                if (buf2) {
                  extracted.assign(
                      static_cast<const char *>(buf2->GetBufferStart()),
                      buf2->GetBufferSize());
                  if (IsValidXmltv(extracted)) {
                    foundXml = true;
                    break;
                  }
                }
              }
            }
            entry2.reset(zipIn2.GetNextEntry());
          }
        }
      }

      if (!foundXml) {
        LOG_ERROR("DecompressIfNeeded: ZIP archive does not contain XMLTV file");
        return false;
      }
      if (extracted.empty()) {
        LOG_ERROR("DecompressIfNeeded: ZIP archive contains no data");
        return false;
      }
      if (extracted.size() > 1024 * 1024 * 1024) {
        LOG_ERROR("DecompressIfNeeded: decompressed ZIP data exceeds 1 GB");
        return false;
      }
      data.swap(extracted);
      return true;
    }

    if (!IsValidXmltv(data)) {
      LOG_ERROR("DecompressIfNeeded: data is not valid XMLTV (no <?xml or <tv)");
      return false;
    }
    return true;
  }
} // namespace

// --------------------------------------------------------------------------
// Конструктор / Деструктор
// --------------------------------------------------------------------------
EPGManager::EPGManager(ConfigManager *configManager,
                       PlaylistManager *playlistManager)
    : wxEvtHandler(), m_configManager(configManager),
      m_playlistManager(playlistManager) {

  // 1. Копирование файлов настроек из ресурсов в конфиг-каталог при первом запуске
  EnsureConfigFile("matching_rules.json");
  EnsureConfigFile("channel_aliases.json");

  // 2. Загрузка основных настроек
  LoadSourcesFromConfig(); 
  InitializeDefaultRegionalSuffixes();

  if (m_configManager) {
    LoadMatchSettings(); 
  }

  // 3. Загрузка правил и алиасов 
  LoadMatchingRules();  // читает matching_rules.json из конфиг-каталога
  LoadChannelAliases(); // читает channel_aliases.json из конфиг-каталога

  // 4. Открытие БД
  if (!m_dbPath.empty()) {
    OpenDatabase();
  }

  // 5. Остальная инициализация (таймеры, потоки)
  m_autoUpdateTimer = new wxTimer(this);
  m_autoUpdateTimer->Bind(wxEVT_TIMER, &EPGManager::OnAutoUpdateTimer, this);

  m_progressTimer = new wxTimer(this);
  m_progressTimer->Bind(wxEVT_TIMER, &EPGManager::OnProgressTimer, this);

  m_startupUpdateTimer = new wxTimer(this);
  m_startupUpdateTimer->Bind(wxEVT_TIMER, &EPGManager::OnStartupUpdateTimer,
                             this);

  unsigned cores = std::max(1u, std::thread::hardware_concurrency());
  size_t availMB = GetAvailableRAM_MB();
  auto tuning = GetPerformanceTuning(availMB, cores, 0);
  int recommended = tuning.maxConcurrentLoads;
  m_matchThreads = std::min(recommended, 16);
  m_matchThreads = std::max(1, m_matchThreads);

  LOG_DEBUG("EPGManager: Match threads = %d", m_matchThreads);
}

EPGManager::~EPGManager() {
  CancelMatching();
  if (m_matchFuture.valid()) {
    auto status = m_matchFuture.wait_for(std::chrono::seconds(2));
    if (status == std::future_status::timeout) {
      LOG_WARN(
          "MatchChannelsAsync did not finish in 2 seconds, proceeding anyway");
    }
  }

  // Ожидаем завершения фоновой задачи обновления одного источника
  if (m_singleRefreshFuture.valid()) {
    auto status = m_singleRefreshFuture.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::timeout) {
      LOG_WARN(
          "Single refresh task did not finish in 5 seconds, proceeding anyway");
    }
  }

  if (m_startupUpdateTimer) {
    m_startupUpdateTimer->Stop();
    delete m_startupUpdateTimer;
  }
  if (m_progressTimer) {
    m_progressTimer->Stop();
    delete m_progressTimer;
  }
  if (m_autoUpdateTimer) {
    m_autoUpdateTimer->Stop();
    delete m_autoUpdateTimer;
  }

  auto start = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();
  if (elapsed >= 5000) {
    LOG_WARN(
        "EPGManager::~EPGManager: WaitForRefresh exceeded 5 seconds (%lld ms)",
        elapsed);
  }
}

wxString EPGManager::GetConfigDirectory() const {
  if (m_configManager)
    return m_configManager->getConfigDirectory();
  return wxString();
}

void EPGManager::EnsureConfigFile(const wxString &filename) {
  if (!m_configManager)
    return;
  wxString configDir = m_configManager->getConfigDirectory();
  if (configDir.IsEmpty())
    return;
  wxString configPath = configDir + wxFileName::GetPathSeparator() + filename;
  if (wxFileExists(configPath))
    return;

  wxString resourcePath = FindResourceFile(filename);
  if (resourcePath.IsEmpty())
    return;

  wxCopyFile(resourcePath, configPath, false);
}

void EPGManager::SetOnProgress(ProgressCallback callback) {
  m_progressCallbacks.clear();
  m_progressCallbacks.push_back(callback);
}

void EPGManager::AddOnProgress(ProgressCallback callback) {
  m_progressCallbacks.push_back(callback);
}

void EPGManager::UpdateProgress(EpgProgressStage stage, int percent,
                                const std::string &stageText, double downloaded,
                                double total, double speed, int matched,
                                int totalChannels) {
  if (m_progressCallbacks.empty())
    return;

  EpgProgressInfo info;
  info.stage = stage;
  info.percent = percent;
  info.stageText = stageText;
  info.downloadedBytes = downloaded;
  info.totalBytes = total;
  info.speedBytesPerSec = speed;
  info.matched = matched;
  info.totalChannels = totalChannels;

  for (auto &cb : m_progressCallbacks) {
    if (cb)
      cb(info);
  }
}

void EPGManager::StartProgressTimer() {
  m_downloadProgress.abort = false;
  m_downloadProgress.downloadedBytes = 0;
  m_downloadProgress.totalBytes = 0;
  m_lastDownloadedBytes = 0;
  m_lastProgressTime = std::chrono::steady_clock::now();

  wxTheApp->CallAfter([this]() {
    if (m_progressTimer && !m_progressTimer->IsRunning()) {
      m_progressTimer->Start(200); // 200 мс
    }
  });
}

void EPGManager::StopProgressTimer() {
  wxTheApp->CallAfter([this]() {
    if (m_progressTimer && m_progressTimer->IsRunning()) {
      m_progressTimer->Stop();
    }
  });
}

void EPGManager::OnProgressTimer(wxTimerEvent &) {
  auto now = std::chrono::steady_clock::now();
  double elapsed =
      std::chrono::duration<double>(now - m_lastProgressTime).count();
  double downloaded = m_downloadProgress.downloadedBytes.load();
  double total = m_downloadProgress.totalBytes.load();

  double speed = 0.0;
  if (elapsed > 0.1) {
    speed = (downloaded - m_lastDownloadedBytes) / elapsed;
    m_lastDownloadedBytes = downloaded;
    m_lastProgressTime = now;
  }

  int percent = (total > 0) ? static_cast<int>((downloaded / total) * 100) : 0;

  std::string stageText = std::string(_("Downloading").ToUTF8().data());
  if (m_downloadProgress.abort.load()) {
    stageText = std::string(_("Cancelled").ToUTF8().data());
    UpdateProgress(EpgProgressStage::Cancelled, percent, stageText, downloaded,
                   total, speed);
    StopProgressTimer();
    return;
  }

  UpdateProgress(EpgProgressStage::Downloading, percent, stageText, downloaded,
                 total, speed);
}

void EPGManager::SetFuzzyThreshold(int value) {
  m_fuzzyThreshold = value;
  if (m_configManager) {
    m_configManager->setInt("epg_fuzzy_threshold", value);
    m_configManager->saveSettings();
  }
}

void EPGManager::SetSubstringMinLength(int value) {
  m_substringMinLength = value;
  if (m_configManager) {
    m_configManager->setInt("epg_substring_min_length", value);
    m_configManager->saveSettings();
  }
}

void EPGManager::SetSubstringMinRatio(int value) {
  m_substringMinRatio = value;
  if (m_configManager) {
    m_configManager->setInt("epg_substring_min_ratio", value);
    m_configManager->saveSettings();
  }
}

void EPGManager::SetMinMatchScore(int value) {
  m_minMatchScore = value;
  if (m_configManager) {
    m_configManager->setInt("epg_min_match_score", value);
    m_configManager->saveSettings();
  }
}

void EPGManager::LoadMatchSettings() {
  if (!m_configManager)
    return;
  m_fuzzyThreshold = m_configManager->getInt("epg_fuzzy_threshold", 75);
  m_substringMinLength = m_configManager->getInt("epg_substring_min_length", 6);
  m_substringMinRatio = m_configManager->getInt("epg_substring_min_ratio", 30);
  m_minMatchScore = m_configManager->getInt("epg_min_match_score", 50);
  LOG_DEBUG("EPGManager: Loaded match thresholds: fuzzy=%d, subLen=%d, "
            "subRatio=%d, minScore=%d",
            m_fuzzyThreshold, m_substringMinLength, m_substringMinRatio,
            m_minMatchScore);
}

void EPGManager::UpdateAllSources(bool onlyAutoUpdate) {
  std::vector<EpgSource> sourcesCopy;
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sourcesCopy = m_sources;
  }

  std::vector<std::string> urlsToUpdate;
  for (const auto &src : sourcesCopy) {
    if (!onlyAutoUpdate || src.autoUpdate) {
      urlsToUpdate.push_back(src.url);
    }
  }

  if (urlsToUpdate.empty()) {
    LOG_DEBUG("EPGManager: No sources to update");
    // Сброс флага (если был установлен)
    m_autoUpdateInProgress = false;
    // Уведомление UI о завершении (сбрасывает busy и гейдж)
    UpdateProgress(EpgProgressStage::Done, 100,
                   std::string(_("No EPG sources to update").ToUTF8().data()));
    return;
  }

  auto completed = std::make_shared<std::atomic<int>>(0);
  auto anySuccess = std::make_shared<std::atomic<bool>>(false);
  int total = static_cast<int>(urlsToUpdate.size());

  for (const auto &url : urlsToUpdate) {
    std::string name;
    for (const auto &src : sourcesCopy) {
      if (src.url == url) {
        name = src.name;
        break;
      }
    }
    RefreshSourceAsync(
        url, name,
        [this, completed, anySuccess, total](bool success,
                                             const std::string &) {
          if (success)
            anySuccess->store(true);
          int done = ++(*completed);
          if (done == total) {
            // Все обновления завершены
            m_autoUpdateInProgress = false; // сброс флага
            if (anySuccess->load() && !m_currentPlaylistId.empty() &&
                m_playlistManager) {
              Playlist *pl =
                  m_playlistManager->findByUniqueId(m_currentPlaylistId);
              if (pl && !pl->getChannels().empty()) {
                MatchChannelsAsync(pl->getChannels(), m_currentPlaylistId,
                                   nullptr);
              }
            }
          }
        });
  }
}

void EPGManager::OnStartupUpdateTimer(wxTimerEvent &event) {
  if (m_autoUpdateInProgress) {
    LOG_DEBUG("Startup update skipped – auto-update already in progress");
    return;
  }
  LOG_DEBUG("Startup auto-update timer triggered");
  // Вызываем тот же обработчик, что и для периодического обновления
  OnAutoUpdateTimer(event);
}

bool EPGManager::GetMappingEntry(const std::string &playlistId,
                                 const std::string &key, std::string &channelId,
                                 bool &isManual) {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return false;
  return m_db->GetMappingEntry(playlistId, key, channelId, isManual);
}

// --------------------------------------------------------------------------
// Управление БД
// --------------------------------------------------------------------------
void EPGManager::SetDbPath(const std::string &path) { m_dbPath = path; }

bool EPGManager::OpenDatabase() {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (m_dbPath.empty()) {
    setLastError("Database path not set");
    return false;
  }

  m_db = std::make_unique<EPGDatabase>();
  if (!m_db->Open(m_dbPath)) {
    setLastError("Failed to open EPG database");
    return false;
  }

  m_epgChannelsHash = m_db->GetEpgChannelsHash();
  auto channels = m_db->GetAllChannels();
  m_loaded = !channels.empty();
  if (!m_loaded) {
    LOG_DEBUG(
        "EPGManager: Database opened but no channels found, m_loaded=false");
  } else {
    LOG_DEBUG("EPGManager: Database opened with %zu channels", channels.size());
  }

  // Загружаем last_update
  std::string lastUpdateStr = m_db->LoadGlobalMetadata("last_update");
  if (!lastUpdateStr.empty()) {
    m_lastUpdate = static_cast<time_t>(std::stoll(lastUpdateStr));
  } else {
    m_lastUpdate = 0;
  }

  // Очистка старых программ
  CleanExpiredPrograms();

  UpdateEpgNameCache();

  // Проверка необходимости автообновления
  time_t now = std::time(nullptr);
  bool needUpdate = (m_lastUpdate == 0) ||
                    (now < m_lastUpdate) || // время переведено назад
                    ((now - m_lastUpdate) >= m_updateIntervalHours * 3600);
  if (needUpdate && !m_startupUpdateTimer->IsRunning()) {
    m_startupUpdateTimer->StartOnce(30000); // 30 секунд
    LOG_DEBUG("Startup auto-update scheduled in 30 seconds");
  }

  return true;
}

// --------------------------------------------------------------------------
// Загрузка из URL и файлов
// --------------------------------------------------------------------------
bool EPGManager::LoadFromUrl(const std::string &url,
                             const std::string &userAgent) {
  if (!m_playlistManager) {
    setLastError("PlaylistManager is null");
    LOG_ERROR("EPGManager: PlaylistManager is null");
    return false;
  }

  UrlAvailabilityResult check =
      CheckUrlAvailability(url, userAgent, 5, 250 * 1024 * 1024);
  if (!check.available) {
    std::string err = "Availability check failed: " + check.errorText;
    setLastError(err);
    LOG_ERROR("EPGManager: %s", err.c_str());
    return false;
  }

  if (check.contentLength > 0) {
    LOG_DEBUG("EPGManager: URL %s, Content-Length: %lld bytes", url.c_str(),
              check.contentLength);
  }

  UpdateProgress(EpgProgressStage::Downloading, -1,
                 std::string(_("Downloading").ToUTF8().data()), 0,
                 check.contentLength > 0 ? check.contentLength : 0, 0, 0, 0);

  StartProgressTimer();

  m_downloadProgress.abort = false;
  m_downloadProgress.totalBytes =
      check.contentLength > 0 ? check.contentLength : 0;
  m_downloadProgress.downloadedBytes = 0;

  std::string xmlData;
  ErrorCode ec = m_playlistManager->downloadUrl(url, xmlData, userAgent,
                                                &m_downloadProgress);
  if (ec != ErrorCode::OK) {
    setLastError("Failed to download EPG from " + url + ": " +
                 m_playlistManager->getLastError());
    LOG_ERROR("EPGManager: Failed to download EPG from %s", url.c_str());
    return false;
  }

  StopProgressTimer();

  if (m_downloadProgress.abort.load()) {
    UpdateProgress(EpgProgressStage::Cancelled, -1,
                   std::string(_("Cancelled").ToUTF8().data()));
    setLastError("Download cancelled");
    return false;
  }

  UpdateProgress(EpgProgressStage::Extracting, -1,
                 std::string(_("Extracting").ToUTF8().data()));

  // ... дальше распаковка и парсинг
  if (xmlData.size() > 100 * 1024 * 1024) {
    setLastError("Downloaded EPG exceeds 100 MB");
    LOG_ERROR("EPGManager: Downloaded data exceeds 100 MB from %s",
              url.c_str());
    return false;
  }

  if (!DecompressIfNeeded(xmlData)) {
    setLastError("Failed to decompress data from " + url);
    LOG_ERROR("EPGManager: Failed to decompress data from %s", url.c_str());
    return false;
  }

  LOG_DEBUG("EPGManager: Downloaded %zu bytes from %s", xmlData.size(),
            url.c_str());
  if (xmlData.size() > 200) {
    LOG_DEBUG("EPGManager: First 200 bytes: %.200s", xmlData.c_str());
  }

  return ParseAndMerge(xmlData, url);
}

bool EPGManager::LoadFromFile(const std::string &filePath) {
  // Уведомление о начале
  UpdateProgress(EpgProgressStage::Extracting, -1,
                 std::string(_("Extracting").ToUTF8().data()));

  std::ifstream file(filePath, std::ios::binary);
  if (!file.is_open()) {
    setLastError("Cannot open file: " + filePath);
    LOG_ERROR("EPGManager: Cannot open file: %s", filePath.c_str());
    UpdateProgress(EpgProgressStage::Error, 0,
                   std::string(_("Error: Cannot open file").ToUTF8().data()));
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();

  if (!DecompressIfNeeded(content)) {
    setLastError("Failed to decompress file: " + filePath);
    LOG_ERROR("EPGManager: Failed to decompress file: %s", filePath.c_str());
    UpdateProgress(
        EpgProgressStage::Error, 0,
        std::string(_("Error: Decompression failed").ToUTF8().data()));
    return false;
  }

  return ParseAndMerge(content, "file://" + filePath);
}

void EPGManager::RefreshSourceAsync(
    const std::string &url, const std::string &name,
    std::function<void(bool, const std::string &)> callback) {
  // Если предыдущая задача ещё выполняется, пропускаем новую
  if (m_singleRefreshFuture.valid()) {
    auto status = m_singleRefreshFuture.wait_for(std::chrono::seconds(0));
    if (status != std::future_status::ready) {
      if (callback)
        callback(false,
                 std::string(
                     _("Previous refresh still in progress").ToUTF8().data()));
      return;
    }
  }

  m_singleRefreshFuture =
      std::async(std::launch::async, [this, url, name, callback]() {
        bool success = false;
        std::string error;
        if (IsNetworkUrl(url)) {
          success = LoadFromUrl(url, "");
        } else {
          success = LoadFromFile(url);
        }
        if (!success) {
          error = getLastError();
        }
        if (callback) {
          wxTheApp->CallAfter(
              [callback, success, error]() { callback(success, error); });
        }
      });
}

void EPGManager::AbortDownload() { m_downloadProgress.abort = true; }

// --------------------------------------------------------------------------
// Парсинг и сохранение в БД
// --------------------------------------------------------------------------
bool EPGManager::ParseAndMerge(const std::string &xmlData,
                               const std::string &sourceUrl) {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen()) {
    setLastError("Database not open");
    return false;
  }

  // Начало парсинга
  UpdateProgress(EpgProgressStage::Parsing, -1,
                 std::string(_("Parsing").ToUTF8().data()));

  EPGParserExpat parser;
  if (!parser.Parse(xmlData)) {
    setLastError("Failed to parse XML from " + sourceUrl);
    LOG_ERROR("EPGManager: Failed to parse XML from %s", sourceUrl.c_str());
    return false;
  }

  const auto &newChannels = parser.GetChannels();
  if (newChannels.empty()) {
    setLastError("No channels found in XMLTV from " + sourceUrl);
    LOG_ERROR("EPGManager: No channels found in XMLTV from %s",
              sourceUrl.c_str());
    return false;
  }

  bool success = true;
  m_db->BeginTransaction();

  size_t total = newChannels.size();
  size_t processed = 0;
  for (const auto &ch : newChannels) {
    if (!m_db->InsertOrUpdateChannel(ch)) {
      LOG_ERROR("Failed to insert channel %s", ch.id.c_str());
      success = false;
      break;
    }
    if (!m_db->InsertPrograms(ch.id, ch.programs)) {
      LOG_ERROR("Failed to insert programs for channel %s", ch.id.c_str());
      success = false;
      break;
    }
    ++processed;

    // Обновляем прогресс каждые 10 каналов или после последнего
    if (processed % 10 == 0 || processed == total) {
      int percent = static_cast<int>((processed * 100) / total);
      UpdateProgress(EpgProgressStage::Parsing, percent,
                     std::string(_("Parsing").ToUTF8().data()));
    }
  }

  if (success) {
    // Сохраняем last_update (в той же транзакции)
    m_lastUpdate = std::time(nullptr);
    bool saved = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
      if (m_db->SaveGlobalMetadata("last_update",
                                   std::to_string(m_lastUpdate))) {
        saved = true;
        break;
      }
      LOG_WARN("Failed to save last_update, attempt %d/2", attempt + 1);
      if (attempt == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    if (!saved) {
      LOG_ERROR("Failed to save last_update after 2 attempts");
    }

    m_db->CommitTransaction();
    m_epgChannelsHash = m_db->GetEpgChannelsHash();
    m_loaded = true;
    CleanExpiredPrograms();
    UpdateEpgNameCache();
    LOG_DEBUG("EPGManager: Parsed and merged data from %s, %zu channels",
              sourceUrl.c_str(), newChannels.size());

    // Завершение
    UpdateProgress(EpgProgressStage::Done, 100,
                   std::string(_("Done").ToUTF8().data()));
  } else {
    m_db->RollbackTransaction();
    setLastError("Failed to insert data into database");
    // Можно добавить Error, но оставим как есть
    return false;
  }

  return true;
}

void EPGManager::CleanExpiredPrograms() {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return;
  time_t now = std::time(nullptr);
  time_t threshold = now - m_daysToKeep * 24 * 3600;
  m_db->DeleteProgramsOlderThan(threshold);
}

// --------------------------------------------------------------------------
// Получение программ
// --------------------------------------------------------------------------
std::vector<EpgProgram>
EPGManager::GetProgramsForChannel(const std::string &tvgId,
                                  const std::string &channelName,
                                  time_t date) const {

  std::vector<EpgProgram> result;
  std::string channelId;

  {
    std::shared_lock lock(m_mappingMutex);
    if (!tvgId.empty()) {
      auto it = m_channelMapping.find(tvgId);
      if (it != m_channelMapping.end())
        channelId = it->second;
    }
    if (channelId.empty() && !channelName.empty()) {
      std::string normalized = NormalizeName(channelName);
      auto it = m_channelMapping.find("name:" + normalized);
      if (it != m_channelMapping.end())
        channelId = it->second;
    }
  }

  if (channelId.empty())
    return result;

  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return result;

  time_t startOfDay = EpgTime::GetStartOfDay(date);
  time_t endOfDay = EpgTime::GetEndOfDay(date);
  if (startOfDay == 0 || endOfDay == 0)
    return result;

  return m_db->GetProgramsForChannel(channelId, startOfDay, endOfDay);
}

EpgProgram EPGManager::GetCurrentProgram(const std::string &tvgId) const {
  EpgProgram result;
  std::string channelId;

  {
    std::shared_lock lock(m_mappingMutex);
    auto it = m_channelMapping.find(tvgId);
    if (it != m_channelMapping.end())
      channelId = it->second;
  }

  if (channelId.empty())
    return result;

  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return result;

  time_t now = std::time(nullptr);
  return m_db->GetCurrentProgram(channelId, now);
}

std::vector<std::string> EPGManager::GetChannelIdsWithEpg() const {
  std::vector<std::string> ids;
  {
    std::shared_lock lock(m_mappingMutex);
    ids.reserve(m_channelMapping.size());
    for (const auto &[key, val] : m_channelMapping) {
      if (key.find("name:") != 0)
        ids.push_back(val);
    }
  }
  return ids;
}

// --------------------------------------------------------------------------
// Нормализация
// --------------------------------------------------------------------------
void EPGManager::NormalizeTvgId(std::string &id) const {
  std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  id.erase(std::remove(id.begin(), id.end(), '.'), id.end());
  id.erase(std::remove(id.begin(), id.end(), ' '), id.end());
  id.erase(std::remove(id.begin(), id.end(), '_'), id.end());
}

std::string EPGManager::NormalizeName(const std::string &name) const {
  std::string result = ToLower(name);

  // Удаляем возрастные рейтинги
  result = RemoveRatingSuffixes(result);

  // Удаляем суффиксы качества и версионные (НО НЕ региональные)
  std::string dummy;
  result = ExtractSuffix(result, m_qualitySuffixes, dummy);
  result = ExtractSuffix(result, m_versionSuffixes, dummy);

  // Заменяем & на and
  size_t pos = result.find('&');
  while (pos != std::string::npos) {
    result.replace(pos, 1, "and");
    pos = result.find('&', pos + 3);
  }

  // Очистка пунктуации и пробелов
  result = CleanPunctuation(result);
  return result;
}

std::vector<std::string> EPGManager::Tokenize(const std::string &name) const {
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

// --------------------------------------------------------------------------
// Сопоставление (Match)
// --------------------------------------------------------------------------
void EPGManager::RebuildAliasIndex() {
  m_aliasIndex.clear();
  std::lock_guard<std::mutex> cacheLock(m_normalizedCacheMutex);
  for (const auto &epg : m_normalizedCache) {
    std::string key = NormalizeAliasKey(epg.displayName);
    if (!key.empty()) {
      m_aliasIndex[key] = epg.id;
    }
  }
}

void EPGManager::RebuildNormalizedCache() {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return;

  auto channels = m_db->GetAllChannels(); // vector<pair<id, display_name>>
  std::vector<NormalizedChannel> newCache;
  newCache.reserve(channels.size());

  for (const auto &p : channels) {
    NormalizedChannel nc;
    nc.id = p.first;
    nc.displayName = p.second;             // сохраняем оригинальное имя
    NormalizeWithAttributes(p.second, nc); // заполняет baseName, регион и т.д.
    newCache.push_back(std::move(nc));
  }

  {
    std::lock_guard<std::mutex> cacheLock(m_normalizedCacheMutex);
    m_normalizedCache = std::move(newCache);
  }

  // После обновления кэша перестраиваем индекс для алиасов
  RebuildAliasIndex();
}

EPGManager::MatchResult
EPGManager::FindBestMatch(const Channel &playlistChannel) const {
  MatchResult result;

  const std::string channelName = playlistChannel.getName();
  LOG_DEBUG("FindBestMatch: START for channel '%s'", channelName.c_str());

  // ========================================================================
  // ЭТАП 0: Алиас
  // ========================================================================
  MatchResult aliasResult = MatchByAlias(channelName);
  if (!aliasResult.channelId.empty()) {
    LOG_DEBUG("FindBestMatch: ALIAS MATCH -> epgId='%s'",
              aliasResult.channelId.c_str());
    return aliasResult;
  }

  // ========================================================================
  // Нормализация имени канала для остальных этапов (без алиасов)
  // ========================================================================
  NormalizedChannel playlistNorm;
  NormalizeWithAttributes(channelName, playlistNorm);
  LOG_DEBUG("FindBestMatch: normalized: baseName='%s', region='%s', "
            "quality='%s', version='%s'",
            playlistNorm.baseName.c_str(), playlistNorm.region.c_str(),
            playlistNorm.quality.c_str(), playlistNorm.version.c_str());

  // ========================================================================
  // ЭТАП 1: tvg‑id (точное совпадение после нормализации)
  // ========================================================================
  std::string tvgId = playlistChannel.getTvgId();
  if (!tvgId.empty()) {
    std::string tvgIdNorm = tvgId;
    std::transform(tvgIdNorm.begin(), tvgIdNorm.end(), tvgIdNorm.begin(),
                   ::tolower);
    tvgIdNorm.erase(std::remove(tvgIdNorm.begin(), tvgIdNorm.end(), ' '),
                    tvgIdNorm.end());
    tvgIdNorm.erase(std::remove(tvgIdNorm.begin(), tvgIdNorm.end(), '.'),
                    tvgIdNorm.end());
    tvgIdNorm.erase(std::remove(tvgIdNorm.begin(), tvgIdNorm.end(), '-'),
                    tvgIdNorm.end());
    tvgIdNorm.erase(std::remove(tvgIdNorm.begin(), tvgIdNorm.end(), '_'),
                    tvgIdNorm.end());
    LOG_DEBUG("FindBestMatch: tvgIdNorm='%s'", tvgIdNorm.c_str());

    std::shared_lock lock(m_tvgIndexMutex);
    auto it = m_tvgIdIndex.find(tvgIdNorm);
    if (it != m_tvgIdIndex.end()) {
      result.channelId = it->second;
      result.method = "tvg-id";
      result.score = 100;
      result.confidence = "high";
      LOG_DEBUG("FindBestMatch: TVG-ID MATCH -> epgId='%s'",
                result.channelId.c_str());
      return result;
    } else {
      LOG_DEBUG("FindBestMatch: tvgIdNorm NOT found in m_tvgIdIndex");
    }
  } else {
    LOG_DEBUG("FindBestMatch: no tvg-id present");
  }

  // ========================================================================
  // ЭТАП 2: Точное совпадение baseName (с учётом региона и версии, НО БЕЗ
  // КАЧЕСТВА)
  // ========================================================================
  {
    std::lock_guard<std::mutex> cacheLock(m_normalizedCacheMutex);
    LOG_DEBUG("FindBestMatch: checking exact match for baseName='%s'",
              playlistNorm.baseName.c_str());
    for (const auto &epg : m_normalizedCache) {
      if (epg.baseName == playlistNorm.baseName) {
        bool regionOk = true;
        if (!playlistNorm.region.empty() && !epg.region.empty() &&
            playlistNorm.region != epg.region)
          regionOk = false;
        bool versionOk = true;
        if (!playlistNorm.version.empty() && !epg.version.empty() &&
            playlistNorm.version != epg.version)
          versionOk = false;
        // quality НЕ проверяем – HD и SD считаем одним каналом
        if (regionOk && versionOk) {
          result.channelId = epg.id;
          result.method = "exact_name";
          result.score = 100;
          result.confidence = "high";
          LOG_DEBUG("FindBestMatch: EXACT NAME MATCH -> epgId='%s'",
                    result.channelId.c_str());
          return result;
        }
      }
    }
    LOG_DEBUG("FindBestMatch: exact match not found");
  }

  // ========================================================================
  // ЭТАП 3: Фильтрация кандидатов по региону/версии (БЕЗ КАЧЕСТВА)
  // ========================================================================
  struct Candidate {
    std::string id;
    std::string baseName;
    std::vector<std::string> tokens;
    std::string region, version, quality;
    int score = 0;
    double ratio = 0.0;
    bool fromSubstring = false;
  };
  std::vector<Candidate> candidates;

  {
    std::lock_guard<std::mutex> cacheLock(m_normalizedCacheMutex);
    for (const auto &epg : m_normalizedCache) {
      bool regionOk = true;
      if (!playlistNorm.region.empty() && !epg.region.empty() &&
          playlistNorm.region != epg.region)
        regionOk = false;
      bool versionOk = true;
      if (!playlistNorm.version.empty() && !epg.version.empty() &&
          playlistNorm.version != epg.version)
        versionOk = false;
      // quality НЕ проверяем
      if (regionOk && versionOk) {
        Candidate cand;
        cand.id = epg.id;
        cand.baseName = epg.baseName;
        cand.tokens = epg.tokens;
        cand.region = epg.region;
        cand.version = epg.version;
        cand.quality = epg.quality;
        candidates.push_back(cand);
      }
    }
  }
  LOG_DEBUG("FindBestMatch: candidates after filtering = %zu",
            candidates.size());

  if (candidates.empty()) {
    LOG_DEBUG("FindBestMatch: no candidates, returning empty result");
    return result;
  }

  // ========================================================================
  // ЭТАП 4: Substring (если одно имя содержит другое)
  // ========================================================================
  for (auto &cand : candidates) {
    const std::string &shortName =
        (playlistNorm.baseName.length() <= cand.baseName.length())
            ? playlistNorm.baseName
            : cand.baseName;
    const std::string &longName =
        (playlistNorm.baseName.length() > cand.baseName.length())
            ? playlistNorm.baseName
            : cand.baseName;
    if (shortName.length() >= static_cast<size_t>(m_substringMinLen) &&
        longName.find(shortName) != std::string::npos) {
      bool isStop = false;
      for (const auto &sw : m_stopwords) {
        if (shortName == sw) {
          isStop = true;
          break;
        }
      }
      if (!isStop) {
        cand.score = 75;
        cand.fromSubstring = true;
        LOG_DEBUG("FindBestMatch: substring candidate '%s' (score=75)",
                  cand.baseName.c_str());
      }
    }
  }

  // ========================================================================
  // ЭТАП 5: Token‑sort (процент общих слов)
  // ========================================================================
  for (auto &cand : candidates) {
    const auto &t1 = playlistNorm.tokens;
    const auto &t2 = cand.tokens;
    if (t1.empty() || t2.empty()) {
      cand.ratio = 0.0;
    } else {
      std::unordered_set<std::string> set1(t1.begin(), t1.end());
      std::unordered_set<std::string> set2(t2.begin(), t2.end());
      size_t inter = 0;
      for (const auto &t : set1) {
        if (set2.find(t) != set2.end())
          inter++;
      }
      size_t uni = set1.size() + set2.size() - inter;
      cand.ratio = (uni > 0) ? static_cast<double>(inter) / uni : 0.0;
    }

    if (cand.ratio >= m_tokenHigh) {
      cand.score = 85;
      LOG_DEBUG("FindBestMatch: token-high candidate '%s' (ratio=%.2f)",
                cand.baseName.c_str(), cand.ratio);
    } else if (cand.ratio >= m_tokenLow) {
      cand.score = 65;
      LOG_DEBUG("FindBestMatch: token-low candidate '%s' (ratio=%.2f)",
                cand.baseName.c_str(), cand.ratio);
    } else {
      cand.score = 0;
    }
  }

  // ========================================================================
  // ЭТАП 6: Jaro‑Winkler и бонусы за качество/регион (бонусы – только
  // поощрение)
  // ========================================================================
  for (auto &cand : candidates) {
    if (cand.score == 0)
      continue;
    if (cand.score == 85) {
      // уже высокий, можно добавить бонусы позже
    } else {
      double jaro = ComputeJaroWinkler(playlistNorm.baseName, cand.baseName);
      LOG_DEBUG("FindBestMatch: Jaro-Winkler for '%s' = %.4f",
                cand.baseName.c_str(), jaro);
      if (jaro >= m_jaroHigh)
        cand.score += 5;
      else if (jaro >= m_jaroMedium)
        cand.score += 3;
      else if (jaro < m_jaroLow)
        cand.score -= 5;
      if (cand.score > 100)
        cand.score = 100;
      if (cand.score < 0)
        cand.score = 0;
    }

    if (cand.score > 0) {
      // Бонус за совпадение качества (НЕ отсеивает)
      if (!playlistNorm.quality.empty() && !cand.quality.empty() &&
          playlistNorm.quality == cand.quality)
        cand.score += 2;
      if (!playlistNorm.region.empty() && !cand.region.empty() &&
          playlistNorm.region == cand.region)
        cand.score += 2;
      if (cand.score > 100)
        cand.score = 100;
      LOG_DEBUG("FindBestMatch: final score for '%s' = %d",
                cand.baseName.c_str(), cand.score);
    }
  }

  // ========================================================================
  // ЭТАП 7: Выбор лучшего кандидата
  // ========================================================================
  Candidate *best = nullptr;
  int bestScore = m_minMatchScore - 1;
  for (auto &cand : candidates) {
    if (cand.score > bestScore) {
      bestScore = cand.score;
      best = &cand;
    }
  }

  LOG_DEBUG("FindBestMatch: best score=%d, threshold=%d", bestScore,
            m_minMatchScore);

  if (best && bestScore >= m_minMatchScore) {
    result.channelId = best->id;
    if (best->score >= 85) {
      result.method = "tokens";
    } else if (best->score >= 70) {
      result.method = "jaro";
    } else {
      result.method = "substring";
    }
    result.score = bestScore;
    if (bestScore >= 85)
      result.confidence = "high";
    else if (bestScore >= 70)
      result.confidence = "medium";
    else if (bestScore >= 60)
      result.confidence = "low";
    else
      result.confidence = "none";
    LOG_DEBUG("FindBestMatch: MATCH FOUND for '%s' -> epgId='%s', method='%s', "
              "score=%d",
              channelName.c_str(), result.channelId.c_str(),
              result.method.c_str(), result.score);
    return result;
  }

  LOG_DEBUG("FindBestMatch: NO MATCH for '%s'", channelName.c_str());
  return result;
}

std::vector<std::pair<std::string, std::string>>
EPGManager::GetAllEpgChannels() const {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return {};
  return m_db->GetAllChannels();
}

// --------------------------------------------------------------------------
// Матчинг
// --------------------------------------------------------------------------
void EPGManager::MatchChannels(const std::vector<Channel> &playlistChannels,
                               const std::string &playlistId,
                               MatchCallback callback) {
  // Проверка наличия источников EPG
  if (m_sources.empty()) {
    LOG_WARN("MatchChannels: no EPG sources, skipping");
    if (callback) {
      wxTheApp->CallAfter([callback]() { callback(0, 0, 0, false); });
    }
    return;
  }

  if (m_cancelMatching.exchange(false)) {
    LOG_DEBUG("MatchChannels cancelled");
    return;
  }

  RebuildNormalizedCache();

  if (m_normalizedCache.empty()) {
    LOG_DEBUG("MatchChannels: normalized cache is empty, finishing early");
    UpdateProgress(EpgProgressStage::Done, 100,
                   std::string(_("No EPG channels in cache").ToUTF8().data()));
    if (callback) {
      wxTheApp->CallAfter([callback]() { callback(0, 0, 0, true); });
    }
    return;
  }

  int total = static_cast<int>(playlistChannels.size());
  if (total == 0) {
    LOG_DEBUG("MatchChannels: no channels to match");
    if (callback) {
      wxTheApp->CallAfter([callback]() { callback(0, 0, 0, true); });
    }
    return;
  }

  int threads = m_matchThreads;
  if (threads <= 0)
    threads = 4;
  if (total < 200)
    threads = std::min(threads, 2);
  else if (total < 1000)
    threads = std::min(threads, 4);

  int batchSize = std::max(1, total / threads);
  if (batchSize < 10)
    batchSize = 10;

  std::vector<std::vector<Channel>> batches;
  batches.reserve(threads);
  for (int i = 0; i < total; i += batchSize) {
    int end = std::min(i + batchSize, total);
    batches.emplace_back(playlistChannels.begin() + i,
                         playlistChannels.begin() + end);
  }

  std::vector<std::future<std::unordered_map<std::string, std::string>>>
      futures;
  futures.reserve(batches.size());

  std::atomic<int> processed{0};
  std::atomic<int> totalMatched{0};
  int reportInterval = std::max(1, std::min(50, total / 20));

  // Начало матчинга
  UpdateProgress(EpgProgressStage::Matching, -1,
                 std::string(_("Matching").ToUTF8().data()), -1, -1, -1, 0,
                 total);

  for (const auto &batch : batches) {
    futures.push_back(
        std::async(std::launch::async, [this, batch, &processed, &totalMatched,
                                        reportInterval, total, callback]() {
          std::unordered_map<std::string, std::string> localMapping;
          localMapping.reserve(batch.size());
          int matched = 0;

          for (const auto &ch : batch) {
            if (m_cancelMatching) {
              wxTheApp->CallAfter([this]() {
                UpdateProgress(EpgProgressStage::Cancelled, 0,
                               std::string(_("Cancelled").ToUTF8().data()));
              });
              break;
            }

            if (m_cancelMatching)
              break;

            MatchResult match = FindBestMatch(ch);
            if (!match.channelId.empty()) {
              std::string key = ch.getTvgId();
              if (key.empty())
                key = "name:" + NormalizeName(ch.getName());
              localMapping[key] = match.channelId;
              matched++;
              totalMatched++;
            }

            int current = processed.fetch_add(1) + 1;
            if (current % reportInterval == 0 || current == total) {
              int percent = (total > 0) ? (current * 100) / total : 0;
              wxTheApp->CallAfter(
                  [this, totalMatched = totalMatched.load(), total, percent]() {
                    UpdateProgress(EpgProgressStage::Matching, percent,
                                   std::string(_("Matching").ToUTF8().data()),
                                   -1, -1, -1, totalMatched, total);
                  });
              if (callback) {
                wxTheApp->CallAfter([callback, current, total]() {
                  callback(0, total, current, true);
                });
              }
            }
          }
          return localMapping;
        }));
  }

  std::unordered_map<std::string, std::string> newMapping;
  std::unordered_map<std::string, std::string> newManualMapping;
  size_t matchedCount = 0;

  for (auto &fut : futures) {
    try {
      auto localMap = fut.get();
      for (const auto &[key, epgId] : localMap) {
        newMapping[key] = epgId;
        matchedCount++;
      }
    } catch (const std::exception &e) {
      LOG_ERROR("MatchChannels: exception in async task: %s", e.what());
    }
  }

  std::string channelHash = ComputePlaylistHash(playlistChannels);
  {
    std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
    if (!m_db->SavePlaylistMapping(playlistId, newMapping, newManualMapping,
                                   playlistChannels.size(), channelHash,
                                   m_epgChannelsHash)) {
      LOG_ERROR("Failed to save playlist mapping to DB");
      return;
    }
  }
  {
    std::unique_lock lock(m_mappingMutex);
    m_channelMapping = newMapping;
    m_manualMapping = newManualMapping;
  }

  RebuildTvgIdIndex();

  LOG_INFO("EPGManager: Matched %zu/%d channels (parallel)", matchedCount,
           total);

  if (callback) {
    wxTheApp->CallAfter([callback, matchedCount, total]() {
      callback(static_cast<int>(matchedCount), total, total, true);
    });
  }

  // Завершение матчинга
  UpdateProgress(EpgProgressStage::Done, 100,
                 std::string(_("Done").ToUTF8().data()), -1, -1, -1,
                 static_cast<int>(matchedCount), total);
}

void EPGManager::MatchChannelsAsync(
    const std::vector<Channel> &playlistChannels, const std::string &playlistId,
    MatchCallback callback) {
  m_cancelMatching = false;
  m_matchFuture = std::async(
      std::launch::async, [this, playlistChannels, playlistId, callback]() {
        if (m_cancelMatching) {
          LOG_DEBUG("MatchChannelsAsync: cancelled before start");
          return;
        }
        bool success = true;
        try {
          // Pass callback to MatchChannels (it will report progress)
          this->MatchChannels(playlistChannels, playlistId, callback);
        } catch (...) {
          success = false;
          LOG_ERROR("MatchChannelsAsync: exception in MatchChannels");
        }
        // Final callback with success flag
        if (callback && !m_cancelMatching) {
          int matched, total;
          {
            std::shared_lock lock(m_mappingMutex);
            matched = static_cast<int>(m_channelMapping.size());
          }
          total = static_cast<int>(playlistChannels.size());
          wxTheApp->CallAfter([callback, matched, total, success]() {
            callback(matched, total, total,
                     success); // progress = total means finished
          });
        }
      });
}

void EPGManager::ReMatchCurrentPlaylist() {
  if (m_currentPlaylistId.empty()) {
    LOG_DEBUG("EPGManager::ReMatchCurrentPlaylist: no current playlist");
    return;
  }
  if (!m_playlistManager) {
    LOG_ERROR("EPGManager::ReMatchCurrentPlaylist: PlaylistManager is null");
    return;
  }
  auto *pl = m_playlistManager->findByUniqueId(m_currentPlaylistId);
  if (!pl) {
    LOG_ERROR("EPGManager::ReMatchCurrentPlaylist: playlist not found: %s",
              m_currentPlaylistId.c_str());
    return;
  }
  const auto &channels = pl->getChannels();
  if (channels.empty()) {
    LOG_DEBUG("EPGManager::ReMatchCurrentPlaylist: no channels in playlist");
    return;
  }
  // Отменяем предыдущий матчинг и запускаем новый асинхронно
  CancelMatching();
  MatchChannelsAsync(channels, m_currentPlaylistId, nullptr);
}

std::unordered_map<std::string, std::string>
EPGManager::ProcessChannelBatch(const std::vector<Channel> &batch) const {
  std::unordered_map<std::string, std::string> localMapping;
  localMapping.reserve(batch.size());

  for (const auto &ch : batch) {
    MatchResult match = FindBestMatch(ch);
    if (!match.channelId.empty()) {
      std::string key = ch.getTvgId();
      if (key.empty()) {
        key = "name:" + ch.getName();
      }
      localMapping[key] = match.channelId;
    }
  }
  return localMapping;
}

// Обновляет кэш имён EPG-каналов из БД
void EPGManager::UpdateEpgNameCache() {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return;

  // Получаем все пары (id, display_name) из таблицы channels
  auto channels = m_db->GetAllChannels();
  m_epgNameCache.clear();
  for (const auto &pair : channels) {
    m_epgNameCache[pair.first] = pair.second;
  }
  LOG_DEBUG("EPGManager: Updated EPG name cache with %zu channels",
            m_epgNameCache.size());
}

std::string EPGManager::GetEpgName(const std::string &epgId) const {
  auto it = m_epgNameCache.find(epgId);
  if (it != m_epgNameCache.end())
    return it->second;

  return "";
}

void EPGManager::RemoveManualMapping(const std::string &playlistId,
                                     const std::string &tvgId) {
  if (playlistId.empty() || tvgId.empty()) {
    LOG_WARN("EPGManager::RemoveManualMapping: empty playlistId or tvgId");
    return;
  }

  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen()) {
    LOG_ERROR("EPGManager::RemoveManualMapping: database not open");
    return;
  }

  // Проверяем, существует ли ручной маппинг для этого плейлиста и tvg-id
  std::string existingEpgId;
  bool isManual = false;
  if (!m_db->GetMappingEntry(playlistId, tvgId, existingEpgId, isManual) ||
      !isManual) {
    LOG_DEBUG("EPGManager::RemoveManualMapping: no manual mapping found for "
              "playlist '%s', tvgId '%s'",
              playlistId.c_str(), tvgId.c_str());
    return;
  }

  // Удаляем из БД
  if (m_db->DeleteManualMapping(playlistId, tvgId)) {
    // Удаляем из локального кэша ручных маппингов (ключ — только tvgId, т.к. он
    // уникален в рамках плейлиста)
    std::unique_lock lock(m_mappingMutex);
    m_manualMapping.erase(tvgId);
    LOG_DEBUG(
        "EPGManager: Removed manual mapping for playlist '%s', tvgId '%s'",
        playlistId.c_str(), tvgId.c_str());
  } else {
    LOG_ERROR("EPGManager::RemoveManualMapping: failed to delete from DB");
  }
}

// --------------------------------------------------------------------------
// Управление mapping по плейлистам
// --------------------------------------------------------------------------
bool EPGManager::LoadMappingForPlaylist(const std::string &playlistId,
                                        const std::vector<Channel> &channels) {
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return false;

  std::unordered_map<std::string, std::string> mapping;
  std::unordered_map<std::string, std::string> manualMapping;
  size_t channelCount = 0;
  std::string channelHash, epgHashAtMatch;

  if (!m_db->LoadPlaylistMapping(playlistId, mapping, manualMapping,
                                 channelCount, channelHash, epgHashAtMatch)) {
    return false;
  }

  if (channelCount != channels.size())
    return false;
  std::string currentHash = ComputePlaylistHash(channels);
  if (currentHash != channelHash)
    return false;

  {
    std::unique_lock lock(m_mappingMutex);
    m_channelMapping = mapping;
    m_manualMapping = manualMapping;
  }
  m_currentPlaylistId = playlistId;

  RebuildTvgIdIndex();

  return true;
}

void EPGManager::SaveMappingForPlaylist(const std::string &playlistId,
                                        const std::vector<Channel> &channels) {
  MatchChannels(channels, playlistId);
}

void EPGManager::InvalidatePlaylistMapping(const std::string &playlistId) {
  {
    std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
    if (m_db && m_db->IsOpen()) {
      m_db->DeletePlaylistMapping(playlistId);
    }
  }
  if (m_currentPlaylistId == playlistId) {
    std::unique_lock lock(m_mappingMutex);
    m_channelMapping.clear();
    m_manualMapping.clear();
  }
  RebuildTvgIdIndex();
}

// --------------------------------------------------------------------------
// Ручные маппинги
// --------------------------------------------------------------------------
void EPGManager::SetManualMapping(const std::string &playlistId,
                                  const std::string &tvgId,
                                  const std::string &epgId,
                                  const std::string &channelName) {
  if (playlistId.empty() || epgId.empty()) {
    LOG_WARN("EPGManager::SetManualMapping: empty playlistId or epgId");
    return;
  }
  std::string key;
  if (!tvgId.empty()) {
    key = tvgId;
  } else if (!channelName.empty()) {
    key = "name:" + NormalizeName(channelName);
  } else {
    LOG_WARN(
        "EPGManager::SetManualMapping: both tvgId and channelName are empty");
    return;
  }

  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen()) {
    LOG_ERROR("EPGManager::SetManualMapping: database not open");
    return;
  }

  m_db->DeleteMappingEntry(playlistId, key);

  if (m_db->UpdateManualMapping(playlistId, key, epgId)) {
    std::unique_lock lock(m_mappingMutex);
    m_manualMapping[key] = epgId;
    auto it = m_channelMapping.find(key);
    if (it != m_channelMapping.end())
      m_channelMapping.erase(it);
    LOG_DEBUG("Manual mapping set: playlist '%s', key '%s' -> epgId '%s'",
              playlistId.c_str(), key.c_str(), epgId.c_str());
  } else {
    LOG_ERROR("Failed to set manual mapping");
  }
}

void EPGManager::RemoveChannelMapping(const std::string &tvgId) {
  if (m_currentPlaylistId.empty()) {
    LOG_ERROR("No current playlist set, cannot remove manual mapping");
    return;
  }
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen()) {
    LOG_ERROR("Database not open, cannot remove manual mapping");
    return;
  }
  if (m_db->DeleteManualMapping(m_currentPlaylistId, tvgId)) {
    std::unique_lock lock(m_mappingMutex);
    m_manualMapping.erase(tvgId);
    LOG_DEBUG("Manual mapping removed: playlist '%s', tvgId '%s'",
              m_currentPlaylistId.c_str(), tvgId.c_str());
  } else {
    LOG_ERROR("Failed to remove manual mapping (key not found)");
  }
}

std::string
EPGManager::GetEpgChannelIdForTvgId(const std::string &tvgId) const {
  std::shared_lock lock(m_mappingMutex);
  auto it = m_channelMapping.find(tvgId);
  if (it != m_channelMapping.end())
    return it->second;
  return "";
}

// --------------------------------------------------------------------------
// Вспомогательные
// --------------------------------------------------------------------------
std::string
EPGManager::ComputePlaylistHash(const std::vector<Channel> &channels) const {
  std::vector<Channel> sorted = channels;
  std::sort(sorted.begin(), sorted.end(),
            [](const Channel &a, const Channel &b) {
              int cmp = a.getName().compare(b.getName());
              if (cmp != 0)
                return cmp < 0;
              return a.getUrl() < b.getUrl();
            });
  std::string concatenated;
  for (const auto &ch : sorted) {
    concatenated += ch.getUrl() + "|" + ch.getName() + "|";
  }
  return stable_hash(concatenated);
}

// --------------------------------------------------------------------------
// Региональные суффиксы
// --------------------------------------------------------------------------
void EPGManager::InitializeDefaultRegionalSuffixes() {
  m_regionalSuffixes = {
      "(us)", "(uk)", "(ca)", "(au)", "(nz)", "(ie)", "us",   "uk",   "ca",
      "au",   "nz",   "ie",   "(de)", "(fr)", "(es)", "(it)", "(pt)", "(nl)",
      "(be)", "de",   "fr",   "es",   "it",   "pt",   "nl",   "be",   "(ru)",
      "(ua)", "(by)", "(kz)", "ru",   "ua",   "by",   "kz",   "(jp)", "(kr)",
      "(cn)", "(tw)", "(hk)", "jp",   "kr",   "cn",   "tw",   "hk"};
}

void EPGManager::LoadMatchingRules() {
  // Дефолты
  m_qualitySuffixes = {"hd",   "fhd",  "sd",   "4k",      "uhd",     "1080p",
                       "576p", "480p", "720p", "full hd", "ultra hd"};
  m_versionSuffixes = {
      "plus",      "premium",       "extra",     "gold",      "classic",
      "deluxe",    "international", "europe",    "asia",      "world",
      "global",    "ultimate",      "platinum",  "exclusive", "special",
      "edition",   "hit",           "series",    "serial",    "cinema",
      "film",      "movie",         "live",      "news",      "sport",
      "music",     "doc",           "kids",      "family",    "travel",
      "nature",    "wildlife",      "adventure", "history",   "science",
      "education", "entertainment"};
  m_stopwords = {"tv",
                 "тв",
                 "канал",
                 "телеканал",
                 "channel",
                 "television",
                 "телекомпания",
                 "live",
                 "online",
                 "трансляция",
                 "broadcast",
                 "streaming",
                 "прямая трансляция",
                 "not 247",
                 "geoblocked"};
  m_tokenHigh = 0.7;
  m_tokenLow = 0.5;
  m_jaroHigh = 0.9;
  m_jaroMedium = 0.85;
  m_jaroLow = 0.8;
  m_substringMinLen = 4;

  wxString jsonPath;
  if (m_configManager) {
    wxString configDir = m_configManager->getConfigDirectory();
    if (!configDir.IsEmpty()) {
      wxFileName fn(configDir, "matching_rules.json");
      if (fn.FileExists())
        jsonPath = fn.GetFullPath();
    }
  }
  wxString jsonContent;
  if (!jsonPath.IsEmpty()) {
    wxFile file(jsonPath);
    if (file.IsOpened() && file.ReadAll(&jsonContent)) {
      rapidjson::Document doc;
      doc.Parse(jsonContent.ToUTF8().data());
      if (!doc.HasParseError() && doc.IsObject()) {
        auto loadArray = [&](const char *key,
                             std::vector<std::string> &target) {
          if (doc.HasMember(key) && doc[key].IsArray()) {
            target.clear();
            for (const auto &val : doc[key].GetArray()) {
              if (val.IsString())
                target.push_back(val.GetString());
            }
          }
        };
        loadArray("regional_suffixes", m_regionalSuffixes);
        loadArray("quality_suffixes", m_qualitySuffixes);
        loadArray("version_suffixes", m_versionSuffixes);
        loadArray("stopwords", m_stopwords);

        if (doc.HasMember("token_high") && doc["token_high"].IsNumber())
          m_tokenHigh = doc["token_high"].GetDouble();
        if (doc.HasMember("token_low") && doc["token_low"].IsNumber())
          m_tokenLow = doc["token_low"].GetDouble();
        if (doc.HasMember("jaro_high") && doc["jaro_high"].IsNumber())
          m_jaroHigh = doc["jaro_high"].GetDouble();
        if (doc.HasMember("jaro_medium") && doc["jaro_medium"].IsNumber())
          m_jaroMedium = doc["jaro_medium"].GetDouble();
        if (doc.HasMember("jaro_low") && doc["jaro_low"].IsNumber())
          m_jaroLow = doc["jaro_low"].GetDouble();
        if (doc.HasMember("substring_min_len") &&
            doc["substring_min_len"].IsInt())
          m_substringMinLen = doc["substring_min_len"].GetInt();
      }
    }
  }
  LOG_DEBUG("EPGManager: Loaded matching rules (quality=%zu, version=%zu, "
            "stopwords=%zu)",
            m_qualitySuffixes.size(), m_versionSuffixes.size(),
            m_stopwords.size());
}

void EPGManager::LoadChannelAliases() {
  m_channelAliases.clear();
  wxString jsonPath;
  if (m_configManager) {
    wxString configDir = m_configManager->getConfigDirectory();
    if (!configDir.IsEmpty()) {
      wxFileName fn(configDir, "channel_aliases.json");
      if (fn.FileExists())
        jsonPath = fn.GetFullPath();
    }
  }
  wxString jsonContent;
  if (!jsonPath.IsEmpty()) {
    wxFile file(jsonPath);
    if (file.IsOpened() && file.ReadAll(&jsonContent)) {
      rapidjson::Document doc;
      doc.Parse(jsonContent.ToUTF8().data());
      if (!doc.HasParseError() && doc.IsObject()) {
        for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
          if (it->value.IsString()) {
            std::string key = it->name.GetString();
            std::string value = it->value.GetString();
            std::string normKey = NormalizeAliasKey(key);
            m_channelAliases[normKey] = value;
          }
        }
      }
    }
  }
  LOG_DEBUG("EPGManager: Loaded %zu channel aliases", m_channelAliases.size());
}

void EPGManager::NormalizeWithAttributes(const std::string &rawName,
                                         NormalizedChannel &out) const {
  out.baseName.clear();
  out.region.clear();
  out.quality.clear();
  out.version.clear();
  out.tokens.clear();

  std::string name = ToLower(rawName);
  name = RemoveRatingSuffixes(name);
  name = CleanPunctuation(name); // очистка перед удалением стоп‑слов

  name = RemoveStopwords(name);
  name = ExtractSuffix(name, m_regionalSuffixes, out.region);
  name = ExtractSuffix(name, m_qualitySuffixes, out.quality);
  name = ExtractSuffix(name, m_versionSuffixes, out.version);
  name = CleanPunctuation(name);
  out.baseName = name;

  // Токены (без стоп‑слов)
  std::istringstream iss(name);
  std::string token;
  while (iss >> token) {
    bool isStop = false;
    for (const auto &sw : m_stopwords) {
      if (token == sw) {
        isStop = true;
        break;
      }
    }
    if (!isStop && !token.empty())
      out.tokens.push_back(token);
  }
}

double EPGManager::ComputeJaroWinkler(const std::string &s1,
                                      const std::string &s2) const {
  if (s1.empty() && s2.empty())
    return 1.0;
  if (s1.empty() || s2.empty())
    return 0.0;

  size_t len1 = s1.size(), len2 = s2.size();
  int matchWindow = std::max(0, static_cast<int>(std::max(len1, len2) / 2) - 1);
  std::vector<bool> matches1(len1, false), matches2(len2, false);
  int matches = 0;
  for (size_t i = 0; i < len1; ++i) {
    int start = std::max(0, static_cast<int>(i) - matchWindow);
    int end =
        std::min(static_cast<int>(len2), static_cast<int>(i) + matchWindow + 1);
    for (int j = start; j < end; ++j) {
      if (matches2[j])
        continue;
      if (s1[i] == s2[j]) {
        matches1[i] = true;
        matches2[j] = true;
        matches++;
        break;
      }
    }
  }
  if (matches == 0)
    return 0.0;
  int transpositions = 0;
  int k = 0;
  for (size_t i = 0; i < len1; ++i) {
    if (!matches1[i])
      continue;
    while (!matches2[k])
      k++;
    if (s1[i] != s2[k])
      transpositions++;
    k++;
  }
  double jaro = (static_cast<double>(matches) / len1 +
                 static_cast<double>(matches) / len2 +
                 static_cast<double>(matches - transpositions / 2) / matches) /
                3.0;

  int prefix = 0;
  int maxPrefix = std::min(4, static_cast<int>(std::min(len1, len2)));
  for (int i = 0; i < maxPrefix; ++i) {
    if (s1[i] == s2[i])
      prefix++;
    else
      break;
  }
  return jaro + (prefix * 0.1 * (1.0 - jaro));
}

// --------------------------------------------------------------------------
// Источники EPG (config)
// --------------------------------------------------------------------------
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
    if (item.HasMember("autoUpdate") && item["autoUpdate"].IsBool()) {
      src.autoUpdate = item["autoUpdate"].GetBool();
    } else {
      src.autoUpdate = false; // значение по умолчанию
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
    obj.AddMember("autoUpdate", src.autoUpdate, allocator);
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

// --------------------------------------------------------------------------
// DeleteCache
// --------------------------------------------------------------------------
bool EPGManager::DeleteCache() {
  {
    std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
    if (m_db && m_db->IsOpen()) {
      m_db->Close();
      m_db.reset();
    }
  }

  if (m_dbPath.empty()) {
    LOG_ERROR("EPGManager: DeleteCache called but dbPath is empty");
    return false;
  }

  // Удаляем файл БД
  if (std::remove(m_dbPath.c_str()) != 0) {
    // Если файла нет — не страшно
    if (errno != ENOENT) {
      LOG_ERROR("EPGManager: Failed to remove database file: %s",
                m_dbPath.c_str());
      return false;
    }
  }

  // Создаём новую БД
  m_db = std::make_unique<EPGDatabase>();
  if (!m_db->Open(m_dbPath)) {
    LOG_ERROR("EPGManager: Failed to reopen database after deletion");
    m_db.reset();
    return false;
  }

  // Восстанавливаем состояние
  m_epgChannelsHash = m_db->GetEpgChannelsHash();
  m_loaded = false;
  m_lastUpdate = 0;
  UpdateEpgNameCache();

  LOG_DEBUG("EPGManager: Cache deleted and new database created");
  return true;
}

// --------------------------------------------------------------------------
// Автообновление (таймер)
// --------------------------------------------------------------------------
void EPGManager::StartAutoUpdate() {
  if (!m_autoUpdateTimer || !m_autoUpdateEnabled)
    return;
  if (m_autoUpdateTimer->IsRunning())
    return;
  int intervalHours = m_updateIntervalHours;
  if (intervalHours < 1)
    intervalHours = 1;
  long intervalMs = intervalHours * 3600 * 1000;
  m_autoUpdateTimer->Start(intervalMs, wxTIMER_CONTINUOUS);
  LOG_DEBUG("EPGManager: Auto-update started (interval %d hours)",
            intervalHours);
}

void EPGManager::StopAutoUpdate() {
  if (m_autoUpdateTimer && m_autoUpdateTimer->IsRunning()) {
    m_autoUpdateTimer->Stop();
    LOG_DEBUG("EPGManager: Auto-update stopped");
  }
}

void EPGManager::RestartAutoUpdate() {
  StopAutoUpdate();
  StartAutoUpdate();
}

void EPGManager::OnAutoUpdateTimer(wxTimerEvent &) {
  if (m_autoUpdateInProgress.exchange(true)) {
    LOG_DEBUG("Auto-update already in progress, skipping");
    return;
  }
  LOG_DEBUG("Auto-update timer triggered");
  UpdateAllSources(true); // обновить только источники с autoUpdate == true
}

// --------------------------------------------------------------------------
// Обработчики колбэков и статус
// --------------------------------------------------------------------------
void EPGManager::SetOnUpdateFinished(
    std::function<void(int, const std::string &)> callback) {
  m_onUpdateFinished = callback;
}

void EPGManager::SetOnRefreshStarted(RefreshStartedCallback callback) {
  m_onRefreshStarted = callback;
}

std::string EPGManager::getLastError() const {
  std::lock_guard<std::mutex> lock(m_lastErrorMutex);
  return m_lastError;
}

void EPGManager::setLastError(const std::string &msg) const {
  std::lock_guard<std::mutex> lock(m_lastErrorMutex);
  m_lastError = msg;
}

bool EPGManager::HasMapping() const {
  std::shared_lock lock(m_mappingMutex);
  return !m_channelMapping.empty();
}

const DownloadProgress &EPGManager::GetDownloadProgress() const {
  return m_downloadProgress;
}

// --------------------------------------------------------------------------
// Настройки
// --------------------------------------------------------------------------
void EPGManager::SetAutoUpdateEnabled(bool enabled) {
  m_autoUpdateEnabled = enabled;
}
bool EPGManager::IsAutoUpdateEnabled() const { return m_autoUpdateEnabled; }
void EPGManager::SetUpdateIntervalHours(int hours) {
  m_updateIntervalHours = hours;
}
int EPGManager::GetUpdateIntervalHours() const { return m_updateIntervalHours; }
void EPGManager::SetDaysToKeep(int days) { m_daysToKeep = days; }
int EPGManager::GetDaysToKeep() const { return m_daysToKeep; }

void EPGManager::RebuildTvgIdIndex() {
  std::unordered_map<std::string, std::string> tmp;
  {
    std::shared_lock lock(m_mappingMutex);
    tmp.reserve(m_channelMapping.size());
    for (const auto &[key, epgId] : m_channelMapping) {
      if (key.rfind("name:", 0) == 0)
        continue;
      std::string normalized = key;
      std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                     ::tolower);
      normalized.erase(std::remove(normalized.begin(), normalized.end(), ' '),
                       normalized.end());
      normalized.erase(std::remove(normalized.begin(), normalized.end(), '.'),
                       normalized.end());
      normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'),
                       normalized.end());
      normalized.erase(std::remove(normalized.begin(), normalized.end(), '_'),
                       normalized.end());
      if (normalized.empty())
        continue;
      if (tmp.find(normalized) == tmp.end()) {
        tmp.emplace(std::move(normalized), epgId);
      }
    }
  }
  {
    std::unique_lock lock(m_tvgIndexMutex);
    m_tvgIdIndex.swap(tmp);
  }
}

void EPGManager::RemoveMappingEntry(const std::string &playlistId,
                                    const std::string &key) {
  if (playlistId.empty() || key.empty()) {
    LOG_WARN("EPGManager::RemoveMappingEntry: empty playlistId or key");
    return;
  }
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen()) {
    LOG_ERROR("EPGManager::RemoveMappingEntry: database not open");
    return;
  }
  if (m_db->DeleteMappingEntry(playlistId, key)) {
    std::unique_lock lock(m_mappingMutex);
    auto itManual = m_manualMapping.find(key);
    if (itManual != m_manualMapping.end())
      m_manualMapping.erase(itManual);
    auto itAuto = m_channelMapping.find(key);
    if (itAuto != m_channelMapping.end())
      m_channelMapping.erase(itAuto);
    LOG_DEBUG("EPGManager: Removed mapping entry for playlist '%s', key '%s'",
              playlistId.c_str(), key.c_str());
  } else {
    LOG_ERROR("EPGManager::RemoveMappingEntry: failed to delete from DB");
  }
}

void EPGManager::IgnoreAutoMapping(const std::string &playlistId,
                                   const std::string &key) {
  if (playlistId.empty() || key.empty())
    return;
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return;

  bool isManual = false;
  std::string epgId;
  if (m_db->GetMappingEntry(playlistId, key, epgId, isManual)) {
    if (!isManual) {
      m_db->SetIgnored(playlistId, key, true);
      LOG_DEBUG("EPGManager: Auto mapping ignored for playlist '%s', key '%s'",
                playlistId.c_str(), key.c_str());
    }
  }
}

void EPGManager::UnignoreAutoMapping(const std::string &playlistId,
                                     const std::string &key) {
  if (playlistId.empty() || key.empty())
    return;
  std::lock_guard<std::recursive_mutex> dbLock(m_dbMutex);
  if (!m_db || !m_db->IsOpen())
    return;

  if (m_db->SetIgnored(playlistId, key, false)) {
    LOG_DEBUG("EPGManager: Auto mapping unignored for playlist '%s', key '%s'",
              playlistId.c_str(), key.c_str());
  }
}

bool EPGManager::IsIgnored(const std::string &playlistId,
                           const std::string &key) const {
  if (playlistId.empty() || key.empty())
    return false;
  std::lock_guard<std::recursive_mutex> dbLock(
      m_dbMutex); // m_dbMutex уже mutable
  if (!m_db || !m_db->IsOpen())
    return false;
  return m_db->IsIgnored(playlistId, key);
}

std::string EPGManager::ToLower(const std::string &str) {
  wxString wstr = wxString::FromUTF8(str);
  wstr.MakeLower();
  return std::string(wstr.utf8_str());
}

// RemoveRatingSuffixes
std::string EPGManager::RemoveRatingSuffixes(const std::string &str) {
  std::regex rating_pattern(R"(\(\s*(\d+)\s*\+\s*\))");
  std::string result = std::regex_replace(str, rating_pattern, "");
  TrimAndCollapseSpaces(result);
  return result;
}

// RemoveStopwords (использует TrimAndCollapseSpaces вместо повторяющегося
// regex)
std::string EPGManager::RemoveStopwords(const std::string &str) const {
  std::string result = str;
  for (const auto &sw : m_stopwords) {
    // В конце: " стоп-слово"
    std::string pattern1 = " " + sw + "$";
    if (result.length() >= pattern1.length() &&
        result.compare(result.length() - pattern1.length(), pattern1.length(),
                       pattern1) == 0) {
      result.erase(result.length() - pattern1.length());
      TrimAndCollapseSpaces(result);
    }
    // В скобках: (стоп-слово)
    std::regex pattern2("\\(" + sw + "\\)");
    result = std::regex_replace(result, pattern2, "");
    TrimAndCollapseSpaces(result);
  }
  return result;
}

// ExtractSuffix (использует TrimAndCollapseSpaces)
std::string EPGManager::ExtractSuffix(const std::string &str,
                                      const std::vector<std::string> &suffixes,
                                      std::string &outSuffix) const {
  std::string result = str;
  outSuffix.clear();

  // Обрезаем завершающие пробелы
  result.erase(result.find_last_not_of(" \t\n\r") + 1);

  for (const auto &suf : suffixes) {
    // Ищем " suf" в конце
    std::string pattern = " " + suf;
    if (result.length() >= pattern.length() &&
        result.compare(result.length() - pattern.length(), pattern.length(),
                       pattern) == 0) {
      outSuffix = suf;
      result.erase(result.length() - pattern.length());
      TrimAndCollapseSpaces(result);
      break;
    }
    // Ищем "(suf)" в любом месте
    std::regex pattern2("\\(" + suf + "\\)");
    if (std::regex_search(result, pattern2)) {
      outSuffix = suf;
      result = std::regex_replace(result, pattern2, "");
      TrimAndCollapseSpaces(result);
      break;
    }
  }
  return result;
}

// CleanPunctuation
std::string EPGManager::CleanPunctuation(const std::string &str) const {
  std::string result = str;
  // Удаляем \r\n
  result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
  result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
  // Заменяем пунктуацию на пробелы, кроме '+'
  std::transform(result.begin(), result.end(), result.begin(), [](char c) {
    if (std::ispunct(static_cast<unsigned char>(c)) && c != '+')
      return ' ';
    return c;
  });
  TrimAndCollapseSpaces(result);
  return result;
}

std::string EPGManager::NormalizeAliasKey(const std::string &name) const {
  std::string result = ToLower(name);
  result = RemoveQualityNumericSuffix(result);
  TrimAndCollapseSpaces(result);
  return result;
}

EPGManager::MatchResult
EPGManager::MatchByAlias(const std::string &playlistName) const {
  MatchResult result;
  std::string normPlaylist = NormalizeAliasKey(playlistName);

  auto aliasIt = m_channelAliases.find(normPlaylist);
  if (aliasIt == m_channelAliases.end())
    return result;

  std::string targetName = aliasIt->second;
  std::string normTarget = NormalizeAliasKey(targetName);

  auto indexIt = m_aliasIndex.find(normTarget);
  if (indexIt != m_aliasIndex.end()) {
    result.channelId = indexIt->second;
    result.method = "alias";
    result.score = 100;
    result.confidence = "high";
  }
  return result;
}
