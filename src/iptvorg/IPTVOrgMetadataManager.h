#ifndef IPTVORGMETADATAMANAGER_H
#define IPTVORGMETADATAMANAGER_H

#include <atomic>
#include <cstdint>
#include <mutex>
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

  // cancel: optional; checked inside curl progress callback. Default arg
  // preserves existing 1-2 argument call sites (GetDisplayName in older code).
  bool FetchCountries(std::vector<Country> &out,
                      std::string *errorText = nullptr,
                      std::atomic<bool> *cancel = nullptr);
  bool FetchLanguages(std::vector<Language> &out,
                      std::string *errorText = nullptr,
                      std::atomic<bool> *cancel = nullptr);
  bool FetchCategories(std::vector<Category> &out,
                       std::string *errorText = nullptr,
                       std::atomic<bool> *cancel = nullptr);

  void InvalidateCache();

private:
  PlaylistManager *m_playlistMgr;

  // Protects m_cached* + m_*Loaded. Never held across network I/O.
  mutable std::mutex m_cacheMutex;
  // Bumped by InvalidateCache. A fetch started before the bump will not
  // store its (now stale) result to the cache.
  std::atomic<uint64_t> m_generation{0};

  std::vector<Country> m_cachedCountries;
  std::vector<Language> m_cachedLanguages;
  std::vector<Category> m_cachedCategories;

  bool m_countriesLoaded = false;
  bool m_languagesLoaded = false;
  bool m_categoriesLoaded = false;

  bool FetchJson(const std::string &url, std::string &outContent,
                 std::string *errorText, std::atomic<bool> *cancel);
  bool ParseCountries(const std::string &json, std::vector<Country> &out,
                      std::string *errorText = nullptr);
  bool ParseLanguages(const std::string &json, std::vector<Language> &out,
                      std::string *errorText = nullptr);
  bool ParseCategories(const std::string &json, std::vector<Category> &out,
                       std::string *errorText = nullptr);
};

#endif
