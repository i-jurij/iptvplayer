#pragma once
#include "ErrorCode.h"
#include "M3UParser.h"
#include "Playlist.h"
#include <curl/curl.h>
#include "xtream/XtreamApiClient.h"
#include "xtream/XtreamDatabase.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct DownloadProgress {
  std::atomic<bool> abort{false};
  std::atomic<double> totalBytes{0.0};
  std::atomic<double> downloadedBytes{0.0};
  std::chrono::steady_clock::time_point lastProgressTime;
  double lastDownloadedBytes = 0.0;
  bool stalled = false;

  // --- Optional advanced options; consumed only when progress != nullptr ---
  // External cancel flag, checked on each curl progress tick.
  // Lifetime contract: caller guarantees the pointed-to atomic outlives
  // the downloadUrl call. Null (default) means "no external cancel".
  std::atomic<bool> *externalCancel = nullptr;

  // curl timeouts (seconds). 0 means "do not set / keep current behaviour".
  long connectTimeoutSec = 0;
  long totalTimeoutSec = 0;
};

class CurlGlobal {
public:
  static CurlGlobal &instance() {
    static CurlGlobal inst;
    return inst;
  }

private:
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
  CurlGlobal(const CurlGlobal &) = delete;
  CurlGlobal &operator=(const CurlGlobal &) = delete;
};

class CurlSession {
public:
  CurlSession() : m_handle(curl_easy_init()) {}
  ~CurlSession() {
    if (m_handle)
      curl_easy_cleanup(m_handle);
  }
  CURL *get() const { return m_handle; }
  CurlSession(const CurlSession &) = delete;
  CurlSession &operator=(const CurlSession &) = delete;

private:
  CURL *m_handle = nullptr;
};

class PlaylistManager {
public:
  explicit PlaylistManager(const std::string &configDir = "");
  ~PlaylistManager();

  ErrorCode addPlaylistFromFile(const std::wstring &filePathW,
                                const std::wstring &titleW,
                                bool autoUpdate = true);
  ErrorCode addPlaylistFromUrl(const std::string &url, std::string &title,
                               const std::string &userAgent,
                               bool autoUpdate = true,
                               bool isXtream = false,
                               XtreamMeta xtreamMeta = {});
  ErrorCode downloadUrl(const std::string &url, std::string &content,
                        const std::string &userAgent,
                        DownloadProgress *progress = nullptr);
                        
  std::string derivePlaylistTitleFromUrl(const std::string &url) const;
  std::string makePlaylistFileName(const std::string &title) const;
  std::string normalizePlaylistTitle(const std::string &rawTitle) const;

  ErrorCode updatePlaylist(size_t index);

  // Возвращает индексы плейлистов, подлежащих автообновлению.
  // includeAll = true  → все плейлисты (per-playlist флаг игнорируется).
  // includeAll = false → только плейлисты с getAutoUpdate() == true.
  // Потокобезопасно (под m_playlistsMutex).
  std::vector<size_t> getAutoUpdateIndices(bool includeAll) const;

  ErrorCode removePlaylist(size_t index);
  ErrorCode removePlaylist(size_t index, bool removeSource);
  ErrorCode exportPlaylist(size_t index, const std::string &outputPath);
  ErrorCode editPlaylist(size_t index, const std::string &title,
                         const std::string &source,
                         const std::string &userAgent, bool autoUpdate);

  Playlist *getPlaylist(std::size_t idx);
  std::vector<std::unique_ptr<Playlist>> &getPlaylists() { return m_playlists; }
  std::size_t size() const;

  Playlist *findByTitle(const std::string &title);

  ErrorCode savePlaylist(const Playlist *pl, size_t idx = (size_t)-1) const;
  ErrorCode savePlaylists() const;
  ErrorCode loadPlaylists();

  bool isDuplicate(const std::string &title, const std::string &source) const;

  void WaitForAddFromUrl();

  std::string getLastError() const;
  void setLastError(const std::string &msg) const;

  Playlist *findByUniqueId(const std::string &uniqueId);

  // Ленивое открытие/получение xtream.db для плейлиста.
  // Возвращает shared_ptr<XtreamDatabase>; nullptr, если playlist не Xtream
  // или БД не открылась. Worker'ы ОБЯЗАНЫ использовать этот метод и
  // захватывать shared_ptr в лямбду — объект переживёт смену плейлиста
  // и CloseAndRemoveXtreamDb.
  std::shared_ptr<xtream::XtreamDatabase>
  GetXtreamDbShared(const Playlist &playlist);

  // Deprecated: для main-thread синхронных операций. Возвращает raw из
  // m_xtreamDbs; объект жив, пока его держит map или какой-то worker.
  // НЕ использовать в фоновых потоках — используйте GetXtreamDbShared.
  xtream::XtreamDatabase *GetXtreamDb(const Playlist &playlist);
  // закрывает xtream.db, если открыт для этого uuid,
  // и удаляет <configDir>/xtream/<uuid>.db + -wal + -shm.
  // Безопасен для не-Xtream плейлистов (no-op, если файлов нет).
  void CloseAndRemoveXtreamDb(const std::string &uuid);
  // закрывает текущую xtream.db (если открыта) и удаляет
  // все <configDir>/xtream/*.db, *.db-wal, *.db-shm.
  // Возвращает FileNotFound, если директории/файлов нет.
  // OK — если что-то удалено (даже частично).
  ErrorCode ClearAllXtreamCaches();
  // суммарный размер всех xtream.db (+ -wal, -shm)
  // в <configDir>/xtream/. Возвращает 0, если файлов нет.
  long long GetTotalXtreamCacheBytes() const;

  // Аксессор для UI/тестов.
  xtream::XtreamApiClient *GetXtreamClient() { return m_xtreamClient.get(); }

private:
  mutable std::mutex m_lastErrorMutex;
  mutable std::string m_lastError;

  std::future<void> m_addFromUrlFuture;

  std::string m_configDir;
  std::vector<std::unique_ptr<Playlist>> m_playlists;
  mutable std::mutex m_playlistsMutex;
  M3UParser m_parser;

  std::unique_ptr<xtream::XtreamApiClient> m_xtreamClient;
  // Один экземпляр XtreamDatabase на UUID. shared_ptr позволяет worker'ам
  // (FetchVodInfo, RefreshVodCategory, FetchSeriesInfo, RefreshSeriesCategory)
  // держать объект живым после CloseAndRemoveXtreamDb: файл БД удаляется
  // при последнем Close() (см. XtreamDatabase::MarkForDeferredDelete).
  std::unordered_map<std::string, std::shared_ptr<xtream::XtreamDatabase>>
      m_xtreamDbs;
  mutable std::mutex m_xtreamDbMutex;

  void setError(const std::string &msg) const;

  ErrorCode loadPlaylistContent(Playlist *playlist);
  bool readFile(const std::string &path, std::string &outContent);

  bool atomicWrite(const std::string &path, const std::string &data) const;
};