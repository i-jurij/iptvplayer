#include "Playlist.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <random>
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
  for (auto &ch : channels)
    ch.setPlaylistId(m_uniqueId);
  m_channels = std::move(channels);
  m_lastUpdate = std::time(nullptr);
}

// ----------------------------------------------------------------------
// Управление каналами
// ----------------------------------------------------------------------
void Playlist::addChannel(const Channel &channel) {
  Channel ch = channel;
  ch.setPlaylistId(m_uniqueId);
  m_channels.push_back(std::move(ch));
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
