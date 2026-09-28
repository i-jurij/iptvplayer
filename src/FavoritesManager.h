#pragma once
#include "Channel.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class FavoritesManager {
public:
  FavoritesManager(const std::string &storagePath);

  // --- Основные операции — только по объекту Channel ---
  void add(const Channel &ch);
  void remove(const Channel &ch);
  bool isFavorite(const Channel &ch) const;

  // --- Список всех избранных каналов ---
  std::vector<Channel> list() const;

  // --- Удаление всех записей с указанным playlistId ---
  void removeByPlaylistId(const std::string &playlistId);

  // --- Полная очистка ---
  void clear();

  // Синхронизировать избранные каналы плейлиста с обновлённым списком.
  // - каналы, которых больше нет, удаляются
  // - у существующих обновляются url, logo, groupTitle
  // - если url изменился, ключ перестраивается
  // Возвращает true, если что-то изменилось (для последующего
  // refreshFavorites).
  bool syncWithPlaylist(const std::string &playlistId,
                        const std::vector<Channel> &channels);

private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, Channel> m_favorites;
  std::string m_storagePath;

  // Ключ (name, playlistId, url, series, season) в length-prefix формате
  static std::string MakeKey(const Channel &ch);

  void loadFromFile();
  void saveToFile();
};
