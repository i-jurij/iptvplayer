#pragma once
#include "Playlist.h"
#include "M3UParser.h"
#include "ErrorCode.h"
#include <curl/curl.h>

#include <memory>
#include <vector>
#include <mutex>
#include <string>

// Singleton для глобальной инициализации CURL
class CurlGlobal {
public:
    static CurlGlobal& instance() {
        static CurlGlobal inst;
        return inst;
    }
private:
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

// RAII-обёртка для CURL easy handle
class CurlSession {
public:
    CurlSession() : m_handle(curl_easy_init()) {}
    ~CurlSession() { if (m_handle) curl_easy_cleanup(m_handle); }
    CURL* get() const { return m_handle; }
    CurlSession(const CurlSession&) = delete;
    CurlSession& operator=(const CurlSession&) = delete;
private:
    CURL* m_handle = nullptr;
};

class PlaylistManager {
public:
    explicit PlaylistManager(const std::string& configDir = "");
    ~PlaylistManager();

    // Добавление плейлистов
    ErrorCode addPlaylistFromFile(const std::wstring& filePathW,
                                  const std::wstring& titleW);
    ErrorCode addPlaylistFromUrl(const std::string& url,
                                 std::string& title,
                                 const std::string& userAgent);

    std::string derivePlaylistTitleFromFile(const std::string& path) const;
    std::string derivePlaylistTitleFromUrl(const std::string& url) const;
    std::string makePlaylistFileName(const std::string& title) const;
    std::string normalizePlaylistTitle(const std::string& rawTitle) const;

    // Обновление
    ErrorCode updatePlaylist(size_t index);
    int updateUrlPlaylists();
    int updateAutoUpdatePlaylists();

    // Управление
    ErrorCode removePlaylist(size_t index);
    ErrorCode removePlaylist(size_t index, bool removeSource);
    ErrorCode exportPlaylist(size_t index, const std::string& outputPath);
    ErrorCode editPlaylist(size_t index,
                           const std::string& title,
                           const std::string& source,
                           const std::string& userAgent,
                           bool autoUpdate);

    // Доступ
    Playlist* getPlaylist(std::size_t idx);
    std::vector<std::unique_ptr<Playlist>>& getPlaylists() { return m_playlists; }
    std::size_t size() const;

    // Методы поиска
    Playlist* findByTitle(const std::string& title);
    std::vector<Playlist*> findByCategory(const std::string& category);
    void clearAll();

    // Сохранение/загрузка плейлистов в отдельные JSON‑файлы
    ErrorCode savePlaylist(const Playlist* pl) const;
	ErrorCode savePlaylists() const; // оставить для совместимости

    ErrorCode loadPlaylists();

    const std::string& getLastError() const { return m_lastError; }

    bool isDuplicate(const std::string& title,
                     const std::string& source) const;

private:
    std::string m_configDir;
    std::vector<std::unique_ptr<Playlist>> m_playlists;
    mutable std::mutex m_playlistsMutex;
    M3UParser m_parser;

    mutable std::string m_lastError;

    void setError(const std::string& msg) const;

    ErrorCode loadPlaylistContent(Playlist* playlist);
    bool readFile(const std::string& path, std::string& outContent);
    ErrorCode downloadUrl(const std::string& url,
                          std::string& content,
                          const std::string& userAgent);
    std::string copyIntoConfigFolder(const std::string& srcPath);
    bool atomicWrite(const std::string& path, const std::string& data) const;

};

