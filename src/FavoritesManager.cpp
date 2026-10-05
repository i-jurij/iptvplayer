#include "FavoritesManager.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <filesystem>
#include <fstream>

using namespace rapidjson;

FavoritesManager::FavoritesManager(const std::string &storagePath)
    : m_storagePath(storagePath) {
  loadFromFile();
}

std::string FavoritesManager::MakeKey(const Channel &ch) {
  const std::string &n  = ch.getName();
  const std::string &p  = ch.getPlaylistId();
  const std::string &u  = ch.getUrl();
  const std::string &s  = ch.getSeries();
  const std::string &se = ch.getSeason();
  return std::to_string(n.size()) + ":" + n +
         std::to_string(p.size()) + ":" + p +
         std::to_string(u.size()) + ":" + u +
         std::to_string(s.size()) + ":" + s +
         std::to_string(se.size()) + ":" + se;
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
    if (item.HasMember("name") && item["name"].IsString())
      ch.setName(item["name"].GetString());
    if (item.HasMember("url") && item["url"].IsString())
      ch.setUrl(item["url"].GetString());
    if (item.HasMember("playlistId") && item["playlistId"].IsString())
      ch.setPlaylistId(item["playlistId"].GetString());
    if (item.HasMember("playlistName") && item["playlistName"].IsString())
      ch.setPlaylistName(item["playlistName"].GetString());
    if (item.HasMember("series") && item["series"].IsString())
      ch.setSeries(item["series"].GetString());
    if (item.HasMember("season") && item["season"].IsString())
      ch.setSeason(item["season"].GetString());
    if (item.HasMember("logo") && item["logo"].IsString())
      ch.setLogo(item["logo"].GetString());
    if (item.HasMember("group") && item["group"].IsString())
      ch.setGroupTitle(item["group"].GetString());
    if (item.HasMember("country") && item["country"].IsString())
      ch.setCountry(item["country"].GetString());
    if (item.HasMember("language") && item["language"].IsString())
      ch.setLanguage(item["language"].GetString());
    if (item.HasMember("tvgId") && item["tvgId"].IsString())
      ch.setTvgId(item["tvgId"].GetString());
    if (item.HasMember("tvgName") && item["tvgName"].IsString())
      ch.setTvgName(item["tvgName"].GetString());
    if (item.HasMember("category") && item["category"].IsString())
      ch.setCategory(item["category"].GetString());

    m_favorites[MakeKey(ch)] = ch;
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
    obj.AddMember("name",       Value(c.getName().c_str(), alloc), alloc);
    obj.AddMember("url",        Value(c.getUrl().c_str(), alloc), alloc);
    obj.AddMember("playlistId", Value(c.getPlaylistId().c_str(), alloc), alloc);
    obj.AddMember("playlistName", Value(c.getPlaylistName().c_str(), alloc),
                  alloc);
    obj.AddMember("series",     Value(c.getSeries().c_str(), alloc), alloc);
    obj.AddMember("season",     Value(c.getSeason().c_str(), alloc), alloc);
    obj.AddMember("logo",       Value(c.getLogo().c_str(), alloc), alloc);
    obj.AddMember("group",      Value(c.getGroupTitle().c_str(), alloc), alloc);
    obj.AddMember("country",    Value(c.getCountry().c_str(), alloc), alloc);
    obj.AddMember("language",   Value(c.getLanguage().c_str(), alloc), alloc);
    obj.AddMember("tvgId", Value(c.getTvgId().c_str(), alloc), alloc);
    obj.AddMember("tvgName", Value(c.getTvgName().c_str(), alloc), alloc);
    obj.AddMember("category", Value(c.getCategory().c_str(), alloc), alloc);

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
  m_favorites[MakeKey(ch)] = ch;
  saveToFile();
}

void FavoritesManager::remove(const Channel &ch) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_favorites.erase(MakeKey(ch));
  saveToFile();
}

bool FavoritesManager::isFavorite(const Channel &ch) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_favorites.find(MakeKey(ch)) != m_favorites.end();
}

std::vector<Channel> FavoritesManager::list() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<Channel> result;
  result.reserve(m_favorites.size());
  for (const auto &pair : m_favorites)
    result.push_back(pair.second);
  return result;
}

void FavoritesManager::removeByPlaylistId(const std::string &playlistId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_favorites.begin(); it != m_favorites.end();) {
    if (it->second.getPlaylistId() == playlistId)
      it = m_favorites.erase(it);
    else
      ++it;
  }
  saveToFile();
}

bool FavoritesManager::syncWithPlaylist(const std::string &playlistId,
                                        const std::vector<Channel> &channels) {
  if (playlistId.empty())
    return false;

  std::lock_guard<std::mutex> lock(m_mutex);

  // Индекс каналов по имени для быстрого поиска
  std::unordered_map<std::string, std::vector<const Channel *>> byName;
  for (const auto &c : channels)
    byName[c.getName()].push_back(&c);

  std::vector<std::string> toRemove;
  std::vector<Channel> toAdd;

  for (const auto &kv : m_favorites) {
    const Channel &fav = kv.second;
    if (fav.getPlaylistId() != playlistId)
      continue;

    auto nameIt = byName.find(fav.getName());
    if (nameIt == byName.end() || nameIt->second.empty()) {
      // Канал исчез из плейлиста
      toRemove.push_back(kv.first);
      continue;
    }

    // Предпочитаем совпадение по URL; иначе первый по имени
    const Channel *match = nullptr;
    for (const Channel *c : nameIt->second) {
      if (c->getUrl() == fav.getUrl()) {
        match = c;
        break;
      }
    }
    if (!match)
      match = nameIt->second.front();

    // Если значимые поля не изменились — оставляем как есть
    if (match->getUrl() == fav.getUrl() && match->getLogo() == fav.getLogo() &&
        match->getGroupTitle() == fav.getGroupTitle()) {
      continue;
    }

    // Обновляем запись, сохраняя playlistId / playlistName
    Channel updated = *match;
    if (updated.getPlaylistId().empty())
      updated.setPlaylistId(playlistId);
    if (updated.getPlaylistName().empty())
      updated.setPlaylistName(fav.getPlaylistName());

    toRemove.push_back(kv.first);
    toAdd.push_back(std::move(updated));
  }

  if (toRemove.empty() && toAdd.empty())
    return false;

  for (const auto &k : toRemove)
    m_favorites.erase(k);
  for (const auto &c : toAdd)
    m_favorites[MakeKey(c)] = c;

  saveToFile();
  return true;
}

void FavoritesManager::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_favorites.clear();
  saveToFile();
}
