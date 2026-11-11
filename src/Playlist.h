#pragma once

#include "Channel.h"

#include <ctime>
#include <string>
#include <vector>

std::string GenerateUUID();

struct XtreamMeta {
  std::string userInfoJson;
  std::string serverInfoJson;
  std::string baseUrl;   // scheme://host[:port][/parent-path], без credentials
  std::string username;
  std::string password;

  bool hasUserInfo() const { return !userInfoJson.empty(); }
  bool hasEndpoint() const {
    return !baseUrl.empty() && !username.empty() && !password.empty();
  }
};

class Playlist {
public:
  Playlist() noexcept;
  explicit Playlist(const std::string &title, const std::string &source,
                    bool isUrl);
  ~Playlist();

  Playlist(const Playlist &) = default;
  Playlist(Playlist &&) noexcept = default;
  Playlist &operator=(const Playlist &) = default;
  Playlist &operator=(Playlist &&) noexcept = default;

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

  const std::string &getUniqueId() const noexcept { return m_uniqueId; }
  void setUniqueId(const std::string &id) { m_uniqueId = id; }

  bool isXtream() const noexcept { return m_xtream; }
  void setXtream(bool v) noexcept { m_xtream = v; }

  const XtreamMeta &getXtreamMeta() const noexcept { return m_xtreamMeta; }
  void setXtreamMeta(const XtreamMeta &m) noexcept { m_xtreamMeta = m; }
  void setXtreamMeta(XtreamMeta &&m) noexcept { m_xtreamMeta = std::move(m); }

  bool hasVod() const noexcept { return m_hasVod; }
  bool hasSeries() const noexcept { return m_hasSeries; }
  bool hasRadio() const noexcept { return m_hasRadio; }
  void setHasVod(bool v) noexcept { m_hasVod = v; }
  void setHasSeries(bool v) noexcept { m_hasSeries = v; }
  void setHasRadio(bool v) noexcept { m_hasRadio = v; }

  void setTitle(const std::string &title) noexcept { m_title = title; }
  void setSource(const std::string &source) noexcept { m_source = source; }
  void setUserAgent(const std::string &ua) noexcept { m_userAgent = ua; }
  void setAutoUpdate(bool v) noexcept { m_autoUpdate = v; }
  void setLastUpdate(std::time_t t) noexcept { m_lastUpdate = t; }
  void setChannels(std::vector<Channel> channels) noexcept;

  void addChannel(const Channel &channel);
  void clearChannels();
  bool removeChannel(const Channel &ch);
  bool removeChannel(const std::string &name, const std::string &url);

  std::vector<std::string> getChannelTitles() const;
  std::vector<std::string> getChannelUrls() const;

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

  std::string m_uniqueId;

  bool m_xtream = false;
  XtreamMeta m_xtreamMeta;
  bool m_hasVod = false;
  bool m_hasSeries = false;
  bool m_hasRadio = false;
};