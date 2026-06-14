#include "FavoritesManager.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <wx/stdpaths.h>
#include <wx/filefn.h>
#include <wx/filename.h>

#include <fstream>
#include <filesystem>

using namespace rapidjson;

FavoritesManager::FavoritesManager(const std::string& storagePath)
    : m_storagePath(storagePath)
{
    loadFromFile();
}

void FavoritesManager::loadFromFile()
{
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

    for (auto& item : doc.GetArray())
    {
        if (!item.IsObject()) continue;

        Channel ch;
        if (item.HasMember("name"))     ch.setName(item["name"].GetString());
        if (item.HasMember("url"))      ch.setUrl(item["url"].GetString());
        if (item.HasMember("playlist")) ch.setPlaylistName(item["playlist"].GetString());
        if (item.HasMember("logo"))     ch.setLogo(item["logo"].GetString());
        if (item.HasMember("group"))    ch.setGroupTitle(item["group"].GetString());
        if (item.HasMember("country"))  ch.setCountry(item["country"].GetString());
        if (item.HasMember("language")) ch.setLanguage(item["language"].GetString());

        m_favorites.push_back(ch);
    }
}

void FavoritesManager::saveToFile()
{
    namespace fs = std::filesystem;

    fs::path path = m_storagePath;
    fs::create_directories(path.parent_path());

    Document doc;
    doc.SetArray();
    auto& alloc = doc.GetAllocator();

    for (const auto& c : m_favorites)
    {
        Value obj(kObjectType);

        Value nameVal;
        nameVal.SetString(c.getName().c_str(), alloc);
        obj.AddMember("name", nameVal, alloc);

        Value urlVal; 
        urlVal.SetString(c.getUrl().c_str(), alloc); 
        obj.AddMember("url", urlVal, alloc);

        Value playlistVal;
        playlistVal.SetString(c.getPlaylistName().c_str(), alloc);
        obj.AddMember("playlist", playlistVal, alloc);

        Value logoVal;
        logoVal.SetString(c.getLogo().c_str(), alloc);
        obj.AddMember("logo", logoVal, alloc);

        Value groupVal;
        groupVal.SetString(c.getGroupTitle().c_str(), alloc);
        obj.AddMember("group", groupVal, alloc);

        Value countryVal;
        countryVal.SetString(c.getCountry().c_str(), alloc);
        obj.AddMember("country", countryVal, alloc);

        Value languageVal;
        languageVal.SetString(c.getLanguage().c_str(), alloc);
        obj.AddMember("language", languageVal, alloc);

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

  auto it = std::find_if(m_favorites.begin(), m_favorites.end(),
                         [&](const Channel &c) {
                           return c.getName() == ch.getName() &&
                                  c.getPlaylistName() == ch.getPlaylistName();
                         });

  if (it == m_favorites.end())
    m_favorites.push_back(ch);

  saveToFile();
}

void FavoritesManager::remove(const std::string &name,
                              const std::string &playlist) {
  std::lock_guard<std::mutex> lock(m_mutex);

  m_favorites.erase(std::remove_if(m_favorites.begin(), m_favorites.end(),
                                   [&](const Channel &c) {
                                     return c.getName() == name &&
                                            c.getPlaylistName() == playlist;
                                   }),
                    m_favorites.end());

  saveToFile();
}

bool FavoritesManager::isFavorite(const Channel &ch) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  return std::any_of(m_favorites.begin(), m_favorites.end(),
                     [&](const Channel &c) {
                       return c.getName() == ch.getName() &&
                              c.getPlaylistName() == ch.getPlaylistName();
                     });
}

std::vector<Channel> FavoritesManager::list() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_favorites;
}

std::vector<std::string> FavoritesManager::listNames() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::string> names;
    names.reserve(m_favorites.size());

    for (const auto& c : m_favorites)
        names.push_back(c.getName());

    return names;
}

void FavoritesManager::removeByPlaylist(const std::string& playlistName)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_favorites.erase(
        std::remove_if(m_favorites.begin(), m_favorites.end(),
            [&](const Channel& c){
                return c.getPlaylistName() == playlistName;
            }),
        m_favorites.end()
    );

    saveToFile();
}
