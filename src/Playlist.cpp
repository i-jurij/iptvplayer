#include "Playlist.h"

#include <algorithm>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

Playlist::Playlist()
    : m_title("Untitled Playlist"), m_source(""), m_userAgent(""),
      m_isUrl(false), m_autoUpdate(false), m_lastUpdate(0) {}

Playlist::Playlist(const std::string &title, const std::string &source,
                   bool isUrl)
    : m_title(title), m_source(source), m_userAgent(""), m_isUrl(isUrl),
      m_autoUpdate(false), m_lastUpdate(std::time(nullptr)) {}

Playlist::~Playlist() = default;

// --- Getters ---
const std::string &Playlist::getTitle() const noexcept { return m_title; }
const std::string &Playlist::getSource() const noexcept { return m_source; }
const std::string &Playlist::getUserAgent() const noexcept {
  return m_userAgent;
}
bool Playlist::isUrl() const noexcept { return m_isUrl; }
bool Playlist::getAutoUpdate() const noexcept { return m_autoUpdate; }
std::time_t Playlist::getLastUpdate() const noexcept { return m_lastUpdate; }
const std::vector<Channel> &Playlist::getChannels() const noexcept {
  return m_channels;
}
size_t Playlist::getChannelCount() const noexcept { return m_channels.size(); }

// --- Setters ---
void Playlist::setTitle(const std::string &title) noexcept { m_title = title; }
void Playlist::setSource(const std::string &source) noexcept {
  m_source = source;
}
void Playlist::setUserAgent(const std::string &ua) noexcept {
  m_userAgent = ua;
}
void Playlist::setAutoUpdate(bool v) noexcept { m_autoUpdate = v; }
void Playlist::setLastUpdate(std::time_t t) noexcept { m_lastUpdate = t; }
void Playlist::setChannels(std::vector<Channel> channels) noexcept {
  m_channels = std::move(channels);
  m_lastUpdate = std::time(nullptr);
}

// --- Channel management ---
void Playlist::addChannel(const Channel &channel) {
  m_channels.push_back(channel);
  m_lastUpdate = std::time(nullptr);
}

void Playlist::clearChannels() {
  m_channels.clear();
  m_lastUpdate = std::time(nullptr);
}

// --- Convenience helpers ---
std::vector<std::string> Playlist::getChannelTitles() const {
  std::vector<std::string> out;
  for (const auto &ch : m_channels)
    out.push_back(ch.getName());
  return out;
}

std::vector<std::string> Playlist::getChannelUrls() const {
  std::vector<std::string> out;
  for (const auto &ch : m_channels)
    out.push_back(ch.getUrl());
  return out;
}

// --- JSON serialization ---
std::string Playlist::toJson() const {
  rapidjson::Document doc;
  doc.SetObject();
  auto &alloc = doc.GetAllocator();

  doc.AddMember("title", rapidjson::Value(m_title.c_str(), alloc), alloc);
  doc.AddMember("source", rapidjson::Value(m_source.c_str(), alloc), alloc);
  doc.AddMember("userAgent", rapidjson::Value(m_userAgent.c_str(), alloc),
                alloc);
  doc.AddMember("isUrl", m_isUrl, alloc);
  doc.AddMember("autoUpdate", m_autoUpdate, alloc);
  doc.AddMember("lastUpdate", static_cast<int64_t>(m_lastUpdate), alloc);

  rapidjson::Value channelsArr(rapidjson::kArrayType);
  for (const auto &ch : m_channels) {
    rapidjson::Value chObj(rapidjson::kObjectType);
    chObj.AddMember("name", rapidjson::Value(ch.getName().c_str(), alloc),
                    alloc);
    chObj.AddMember("url", rapidjson::Value(ch.getUrl().c_str(), alloc), alloc);
    chObj.AddMember("groupTitle",
                    rapidjson::Value(ch.getGroupTitle().c_str(), alloc), alloc);
    chObj.AddMember("country", rapidjson::Value(ch.getCountry().c_str(), alloc),
                    alloc);
    chObj.AddMember("language",
                    rapidjson::Value(ch.getLanguage().c_str(), alloc), alloc);
    chObj.AddMember("category",
                    rapidjson::Value(ch.getCategory().c_str(), alloc), alloc);
    chObj.AddMember("tvgLogo", rapidjson::Value(ch.getLogo().c_str(), alloc),
                    alloc);

    for (const auto &[key, value] : ch.attributes()) {
      chObj.AddMember(rapidjson::Value(key.c_str(), alloc),
                      rapidjson::Value(value.c_str(), alloc), alloc);
    }

    channelsArr.PushBack(chObj, alloc);
  }
  doc.AddMember("channels", channelsArr, alloc);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return buffer.GetString();
}

// --- JSON deserialization ---
bool Playlist::fromJson(const std::string &json) {
  rapidjson::Document doc;
  if (doc.Parse(json.c_str()).HasParseError())
    return false;

  if (!doc.HasMember("channels") || !doc["channels"].IsArray())
    return false;

  if (doc.HasMember("title") && doc["title"].IsString())
    m_title = doc["title"].GetString();
  if (doc.HasMember("source") && doc["source"].IsString())
    m_source = doc["source"].GetString();
  if (doc.HasMember("userAgent") && doc["userAgent"].IsString())
    m_userAgent = doc["userAgent"].GetString();
  if (doc.HasMember("isUrl") && doc["isUrl"].IsBool())
    m_isUrl = doc["isUrl"].GetBool();
  if (doc.HasMember("autoUpdate") && doc["autoUpdate"].IsBool())
    m_autoUpdate = doc["autoUpdate"].GetBool();
  if (doc.HasMember("lastUpdate") && doc["lastUpdate"].IsInt64())
    m_lastUpdate = static_cast<std::time_t>(doc["lastUpdate"].GetInt64());

  m_channels.clear();
  for (auto &chVal : doc["channels"].GetArray()) {
    Channel ch;
    if (chVal.HasMember("name") && chVal["name"].IsString())
      ch.setName(chVal["name"].GetString());
    if (chVal.HasMember("url") && chVal["url"].IsString())
      ch.setUrl(chVal["url"].GetString());
    if (chVal.HasMember("groupTitle") && chVal["groupTitle"].IsString())
      ch.setGroupTitle(chVal["groupTitle"].GetString());
    if (chVal.HasMember("country") && chVal["country"].IsString())
      ch.setCountry(chVal["country"].GetString());
    if (chVal.HasMember("language") && chVal["language"].IsString())
      ch.setLanguage(chVal["language"].GetString());
    if (chVal.HasMember("category") && chVal["category"].IsString())
      ch.setCategory(chVal["category"].GetString());
    if (chVal.HasMember("tvgLogo") && chVal["tvgLogo"].IsString())
      ch.setLogo(chVal["tvgLogo"].GetString());

    for (auto it = chVal.MemberBegin(); it != chVal.MemberEnd(); ++it) {
      std::string key = it->name.GetString();
      if (key != "name" && key != "url" && key != "groupTitle" &&
          key != "country" && key != "language" && key != "category" &&
          key != "tvgLogo") {
        if (it->value.IsString()) {
          ch.attributes()[key] = it->value.GetString();
        }
      }
    }

    m_channels.push_back(ch);
  }
  return true;
}

bool Playlist::removeChannel(const Channel &ch) {
  auto it =
      std::find_if(m_channels.begin(), m_channels.end(), [&](const Channel &c) {
        return c.getName() == ch.getName() && c.getUrl() == ch.getUrl();
      });
  if (it == m_channels.end())
    return false;
  m_channels.erase(it);
  return true;
}

bool Playlist::removeChannel(const std::string &name, const std::string &url) {
  auto it =
      std::find_if(m_channels.begin(), m_channels.end(), [&](const Channel &c) {
        return c.getName() == name && c.getUrl() == url;
      });
  if (it == m_channels.end())
    return false;
  m_channels.erase(it);
  return true;
}
