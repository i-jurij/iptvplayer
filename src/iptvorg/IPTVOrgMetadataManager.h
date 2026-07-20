#ifndef IPTVORGMETADATAMANAGER_H
#define IPTVORGMETADATAMANAGER_H

#include <string>
#include <vector>

struct Country {
  std::string code;
  std::string name;
  std::vector<std::string> languages;
};

struct Language {
  std::string code;
  std::string name;
};

struct Category {
  std::string id;
  std::string name;
};

class PlaylistManager;

class IPTVOrgMetadataManager {
public:
  explicit IPTVOrgMetadataManager(PlaylistManager *playlistMgr);
  ~IPTVOrgMetadataManager() = default;

  bool FetchCountries(std::vector<Country> &out);
  bool FetchLanguages(std::vector<Language> &out);
  bool FetchCategories(std::vector<Category> &out);

  void InvalidateCache();

private:
  PlaylistManager *m_playlistMgr;

  std::vector<Country> m_cachedCountries;
  std::vector<Language> m_cachedLanguages;
  std::vector<Category> m_cachedCategories;

  bool m_countriesLoaded = false;
  bool m_languagesLoaded = false;
  bool m_categoriesLoaded = false;

  bool FetchJson(const std::string &url, std::string &outContent);
  bool ParseCountries(const std::string &json, std::vector<Country> &out);
  bool ParseLanguages(const std::string &json, std::vector<Language> &out);
  bool ParseCategories(const std::string &json, std::vector<Category> &out);
};

#endif
