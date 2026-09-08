#pragma once
#include "Channel.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class FavoritesManager {
public:
  FavoritesManager(const std::string &storagePath);

  // Добавление/удаление по объекту Channel (использует его uniqueId)
  void add(const Channel &ch);
  void remove(const Channel &ch); // по uniqueId
  void remove(const std::string &uniqueId);
  bool isFavorite(const Channel &ch) const;
  bool isFavorite(const std::string &uniqueId) const;

  // Старые методы для совместимости (по имени+playlist)
  void remove(const std::string &name, const std::string &playlist);
  bool isFavoriteByName(const std::string &name,
                        const std::string &playlist) const;

  std::vector<Channel> list() const;
  std::vector<std::string> listNames() const;
  void removeByPlaylist(const std::string &playlistName);
  void clear();

private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, Channel> m_favorites; // key = uniqueId
  std::string m_storagePath;

  void loadFromFile();
  void saveToFile();
};
