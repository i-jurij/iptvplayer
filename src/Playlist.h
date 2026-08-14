#pragma once

#include "Channel.h"

#include <ctime>
#include <string>
#include <vector>

// Объявление функции генерации UUID (определена в Playlist.cpp)
std::string GenerateUUID();

class Playlist {
public:
  Playlist() noexcept;
  explicit Playlist(const std::string &title, const std::string &source,
                    bool isUrl);
  ~Playlist();

  // Копирование/перемещение
  Playlist(const Playlist &) = default;
  Playlist(Playlist &&) noexcept = default;
  Playlist &operator=(const Playlist &) = default;
  Playlist &operator=(Playlist &&) noexcept = default;

  // --- Геттеры ---
  const std::string &getTitle() const noexcept { return m_title; }
  const std::string &getSource() const noexcept { return m_source; }
  const std::string &getUserAgent() const noexcept { return m_userAgent; }
  bool isUrl() const noexcept { return m_isUrl; }
  bool getAutoUpdate() const noexcept { return m_autoUpdate; }
  std::time_t getLastUpdate() const noexcept { return m_lastUpdate; }
  const std::vector<Channel> &getChannels() const noexcept {
    return m_channels;
  }
  size_t getChannelCount() const noexcept { return m_channels.size(); }

  // --- Уникальный ID ---
  const std::string &getUniqueId() const noexcept { return m_uniqueId; }
  void setUniqueId(const std::string &id) { m_uniqueId = id; }

  // --- Сеттеры ---
  void setTitle(const std::string &title) noexcept { m_title = title; }
  void setSource(const std::string &source) noexcept { m_source = source; }
  void setUserAgent(const std::string &ua) noexcept { m_userAgent = ua; }
  void setAutoUpdate(bool v) noexcept { m_autoUpdate = v; }
  void setLastUpdate(std::time_t t) noexcept { m_lastUpdate = t; }
  void setChannels(std::vector<Channel> channels) noexcept;

  // --- Управление каналами ---
  void addChannel(const Channel &channel);
  void clearChannels();
  bool removeChannel(const Channel &ch);
  bool removeChannel(const std::string &name, const std::string &url);

  // --- Вспомогательные ---
  std::vector<std::string> getChannelTitles() const;
  std::vector<std::string> getChannelUrls() const;

  // --- JSON ---
  std::string toJson() const;
  bool fromJson(const std::string &json);

private:
  std::string m_title;
  std::string m_source;
  std::string m_userAgent;
  bool m_isUrl;
  bool m_autoUpdate;
  std::time_t m_lastUpdate;
  std::vector<Channel> m_channels;

  std::string m_uniqueId; // уникальный идентификатор (UUID v4)
};