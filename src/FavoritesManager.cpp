#include "FavoritesManager.h"
#include <filesystem>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

using namespace rapidjson;

FavoritesManager::FavoritesManager(const std::string &storagePath)
    : m_storagePath(storagePath) {
  loadFromFile();
}

void FavoritesManager::loadFromFile() {
  namespace fs = std::filesystem;
  if (!fs::exists(m_storagePath))
    return;

  std::ifstream file(m_storagePath);
  if (!file.is_open())
    return;

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  Document doc;
  if (doc.Parse(content.c_str()).HasParseError() || !doc.IsArray())
    return;

  m_favorites.clear();
  for (auto &item : doc.GetArray()) {
    if (!item.IsObject())
      continue;
    Channel ch;
    if (item.HasMember("uniqueId") && item["uniqueId"].IsString())
      ch.setUniqueId(item["uniqueId"].GetString());
    if (item.HasMember("name"))
      ch.setName(item["name"].GetString());
    if (item.HasMember("url"))
      ch.setUrl(item["url"].GetString());
    if (item.HasMember("playlist"))
      ch.setPlaylistName(item["playlist"].GetString());
    if (item.HasMember("logo"))
      ch.setLogo(item["logo"].GetString());
    if (item.HasMember("group"))
      ch.setGroupTitle(item["group"].GetString());
    if (item.HasMember("country"))
      ch.setCountry(item["country"].GetString());
    if (item.HasMember("language"))
      ch.setLanguage(item["language"].GetString());

    if (!ch.getUniqueId().empty())
      m_favorites[ch.getUniqueId()] = ch;
    else {
      // старый формат – генерируем ID на основе имени+playlist
      ch.ensureUniqueId();
      m_favorites[ch.getUniqueId()] = ch;
    }
  }
}

void FavoritesManager::saveToFile() {
  namespace fs = std::filesystem;
  fs::path path = m_storagePath;
  fs::create_directories(path.parent_path());

  Document doc;
  doc.SetArray();
  auto &alloc = doc.GetAllocator();

  for (const auto &pair : m_favorites) {
    const Channel &c = pair.second;
    Value obj(kObjectType);
    obj.AddMember("uniqueId", Value(c.getUniqueId().c_str(), alloc), alloc);
    obj.AddMember("name", Value(c.getName().c_str(), alloc), alloc);
    obj.AddMember("url", Value(c.getUrl().c_str(), alloc), alloc);
    obj.AddMember("playlist", Value(c.getPlaylistName().c_str(), alloc), alloc);
    obj.AddMember("logo", Value(c.getLogo().c_str(), alloc), alloc);
    obj.AddMember("group", Value(c.getGroupTitle().c_str(), alloc), alloc);
    obj.AddMember("country", Value(c.getCountry().c_str(), alloc), alloc);
    obj.AddMember("language", Value(c.getLanguage().c_str(), alloc), alloc);
    doc.PushBack(obj, alloc);
  }

  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  doc.Accept(writer);

  std::ofstream file(m_storagePath);
  file << buffer.GetString();
}

void FavoritesManager::add(const Channel &ch) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const std::string &id = ch.getUniqueId();
  if (id.empty())
    return;
  m_favorites[id] = ch;
  saveToFile();
}

void FavoritesManager::remove(const std::string &uniqueId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_favorites.erase(uniqueId);
  saveToFile();
}

void FavoritesManager::remove(const Channel &ch) { remove(ch.getUniqueId()); }

void FavoritesManager::remove(const std::string &name,
                              const std::string &playlist) {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Ищем по паре и удаляем
  for (auto it = m_favorites.begin(); it != m_favorites.end();) {
    const Channel &c = it->second;
    if (c.getName() == name && c.getPlaylistName() == playlist)
      it = m_favorites.erase(it);
    else
      ++it;
  }
  saveToFile();
}

bool FavoritesManager::isFavorite(const Channel &ch) const {
  return isFavorite(ch.getUniqueId());
}

bool FavoritesManager::isFavorite(const std::string &uniqueId) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_favorites.find(uniqueId) != m_favorites.end();
}

bool FavoritesManager::isFavoriteByName(const std::string &name,
                                        const std::string &playlist) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto &pair : m_favorites) {
    const Channel &c = pair.second;
    if (c.getName() == name && c.getPlaylistName() == playlist)
      return true;
  }
  return false;
}

std::vector<Channel> FavoritesManager::list() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<Channel> result;
  result.reserve(m_favorites.size());
  for (const auto &pair : m_favorites)
    result.push_back(pair.second);
  return result;
}

std::vector<std::string> FavoritesManager::listNames() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::string> names;
  names.reserve(m_favorites.size());
  for (const auto &pair : m_favorites)
    names.push_back(pair.second.getName());
  return names;
}

void FavoritesManager::removeByPlaylist(const std::string &playlistName) {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_favorites.begin(); it != m_favorites.end();) {
    if (it->second.getPlaylistName() == playlistName)
      it = m_favorites.erase(it);
    else
      ++it;
  }
  saveToFile();
}

void FavoritesManager::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_favorites.clear();
  saveToFile();
}
