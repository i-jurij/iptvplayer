#pragma once
#include "Channel.h"
#include <vector>
#include <string>
#include <mutex>

class FavoritesManager {
public:
    FavoritesManager(const std::string& storagePath);

    void add(const Channel& ch);
    void remove(const std::string &name, const std::string &playlist);
    bool isFavorite(const Channel &ch) const;

    std::vector<Channel> list() const;
    std::vector<std::string> listNames() const;

    // NEW: удалить все избранные каналы плейлиста
    void removeByPlaylist(const std::string& playlistName);

private:
    mutable std::mutex m_mutex;
    std::vector<Channel> m_favorites;
    std::string m_storagePath;

    void loadFromFile();
    void saveToFile();
};
