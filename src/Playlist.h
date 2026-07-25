#pragma once

#include "Channel.h"

#include <string>
#include <vector>
#include <ctime>

class Playlist {
public:
    Playlist();
    Playlist(const std::string&, const std::string&, bool);
    ~Playlist();

    const std::string& getTitle() const noexcept;
    const std::string& getSource() const noexcept;
    const std::string& getUserAgent() const noexcept;
    bool isUrl() const noexcept;
    bool getAutoUpdate() const noexcept;
    std::time_t getLastUpdate() const noexcept;
    const std::vector<Channel>& getChannels() const noexcept;
    std::size_t getChannelCount() const noexcept;

    void setTitle(const std::string&) noexcept;
    void setSource(const std::string&) noexcept;
    void setUserAgent(const std::string&) noexcept;
    void setAutoUpdate(bool) noexcept;
    void setLastUpdate(std::time_t) noexcept;
    void setChannels(std::vector<Channel>) noexcept;

    void addChannel(const Channel&);
    void clearChannels();

    std::vector<std::string> getChannelTitles() const;
    std::vector<std::string> getChannelUrls() const;

    std::string toJson() const;
    bool fromJson(const std::string &);

    bool removeChannel(const Channel &ch);
    bool removeChannel(const std::string &name, const std::string &url);

  private:
    std::string m_title;
    std::string m_source;
    std::string m_userAgent;
    bool m_isUrl;
    bool m_autoUpdate;
    std::time_t m_lastUpdate;
    std::vector<Channel> m_channels;
};

