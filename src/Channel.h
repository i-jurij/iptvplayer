#pragma once
#include <string>
#include <map>

class Channel {
public:
    Channel() = default;

    Channel(const std::string& name,
            const std::string& url,
            const std::string& groupTitle = "",
            const std::string& country = "",
            const std::string& language = "",
            const std::string& category = "",
            const std::string& tvgLogo = "",
            const std::string& tvgId = "",
            const std::string& tvgName = "",
            const std::string& playlistName = "")
        : m_name(name),
          m_url(url),
          m_groupTitle(groupTitle),
          m_country(country),
          m_language(language),
          m_category(category),
          m_tvgLogo(tvgLogo),
          m_tvgId(tvgId),
          m_tvgName(tvgName),
          m_playlistName(playlistName)
    {}

    // --- Геттеры ---
    const std::string& getName() const { return m_name; }
    const std::string& getUrl() const { return m_url; }
    const std::string& getGroupTitle() const { return m_groupTitle; }
    const std::string& getCountry() const { return m_country; }
    const std::string& getLanguage() const { return m_language; }
    const std::string& getCategory() const { return m_category; }
    const std::string& getLogo() const { return m_tvgLogo; }
    const std::string& getTvgId() const { return m_tvgId; }
    const std::string& getTvgName() const { return m_tvgName; }
    const std::string& getPlaylistName() const { return m_playlistName; }

    // --- Сеттеры ---
    void setName(const std::string& v) { m_name = v; }
    void setUrl(const std::string& v) { m_url = v; }
    void setGroupTitle(const std::string& v) { m_groupTitle = v; }
    void setCountry(const std::string& v) { m_country = v; }
    void setLanguage(const std::string& v) { m_language = v; }
    void setCategory(const std::string& v) { m_category = v; }
    void setLogo(const std::string& v) { m_tvgLogo = v; }
    void setTvgId(const std::string& v) { m_tvgId = v; }
    void setTvgName(const std::string& v) { m_tvgName = v; }
    void setPlaylistName(const std::string& v) { m_playlistName = v; }

    // --- Атрибуты ---
    std::map<std::string, std::string>& attributes() { return m_attributes; }
    const std::map<std::string, std::string>& attributes() const { return m_attributes; }

private:
    std::string m_name;
    std::string m_url;
    std::string m_groupTitle;
    std::string m_country;
    std::string m_language;
    std::string m_category;
    std::string m_tvgLogo;
    std::string m_tvgId;
    std::string m_tvgName;

    // ВАЖНО: новое поле
    std::string m_playlistName;

    std::map<std::string, std::string> m_attributes;
};
