#include "Playlist.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>

// ----------------------------------------------------------------------
// Реализация GenerateUUID (v4, RFC4122)
// ----------------------------------------------------------------------
std::string GenerateUUID() {
  thread_local std::mt19937_64 gen(std::random_device{}());
  std::uniform_int_distribution<uint64_t> dist;

  std::array<std::uint8_t, 16> bytes;
  uint64_t r1 = dist(gen);
  uint64_t r2 = dist(gen);
  for (int i = 0; i < 8; ++i)
    bytes[i] = static_cast<std::uint8_t>((r1 >> (i * 8)) & 0xFFu);
  for (int i = 0; i < 8; ++i)
    bytes[8 + i] = static_cast<std::uint8_t>((r2 >> (i * 8)) & 0xFFu);

  // версия 4 и вариант RFC4122
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0Fu) | 0x40u);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3Fu) | 0x80u);

  std::ostringstream ss;
  ss << std::hex << std::setfill('0');

  auto put_hex = [&](int idx, int count) {
    for (int i = 0; i < count; ++i) {
      ss << std::setw(2) << (static_cast<unsigned int>(bytes[idx + i]) & 0xFFu);
    }
  };

  put_hex(0, 4);
  ss << "-";
  put_hex(4, 2);
  ss << "-";
  put_hex(6, 2);
  ss << "-";
  put_hex(8, 2);
  ss << "-";
  put_hex(10, 6);

  return ss.str();
}

// ----------------------------------------------------------------------
// Конструкторы / деструктор
// ----------------------------------------------------------------------
Playlist::Playlist() noexcept
    : m_title("Untitled Playlist"), m_source(""), m_userAgent(""),
      m_isUrl(false), m_autoUpdate(false), m_lastUpdate(0) {
  m_uniqueId = GenerateUUID();
}

Playlist::Playlist(const std::string &title, const std::string &source,
                   bool isUrl)
    : m_title(title), m_source(source), m_userAgent(""), m_isUrl(isUrl),
      m_autoUpdate(false), m_lastUpdate(std::time(nullptr)) {
  m_uniqueId = GenerateUUID();
}

Playlist::~Playlist() = default;

// ----------------------------------------------------------------------
// setChannels
// ----------------------------------------------------------------------
void Playlist::setChannels(std::vector<Channel> channels) noexcept {
  m_channels = std::move(channels);
  m_lastUpdate = std::time(nullptr);
}

// ----------------------------------------------------------------------
// Управление каналами
// ----------------------------------------------------------------------
void Playlist::addChannel(const Channel &channel) {
  m_channels.push_back(channel);
  m_lastUpdate = std::time(nullptr);
}

void Playlist::clearChannels() {
  m_channels.clear();
  m_lastUpdate = std::time(nullptr);
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

// ----------------------------------------------------------------------
// Вспомогательные методы
// ----------------------------------------------------------------------
std::vector<std::string> Playlist::getChannelTitles() const {
  std::vector<std::string> out;
  out.reserve(m_channels.size());
  for (const auto &ch : m_channels)
    out.push_back(ch.getName());
  return out;
}

std::vector<std::string> Playlist::getChannelUrls() const {
  std::vector<std::string> out;
  out.reserve(m_channels.size());
  for (const auto &ch : m_channels)
    out.push_back(ch.getUrl());
  return out;
}

// ----------------------------------------------------------------------
// JSON сериализация
// ----------------------------------------------------------------------
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
  doc.AddMember("uniqueId", rapidjson::Value(m_uniqueId.c_str(), alloc),
                alloc); // <-- добавлено

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

bool Playlist::fromJson(const std::string &json) {
  rapidjson::Document doc;
  if (doc.Parse(json.c_str()).HasParseError())
    return false;

  if (!doc.HasMember("channels") || !doc["channels"].IsArray())
    return false;

  // Основные поля
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

  // Уникальный ID
  if (doc.HasMember("uniqueId") && doc["uniqueId"].IsString()) {
    m_uniqueId = doc["uniqueId"].GetString();
  } else {
    // Если поле отсутствует – генерируем новый ID
    m_uniqueId = GenerateUUID();
  }

  // Каналы
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

    // Дополнительные атрибуты
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