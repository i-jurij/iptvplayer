#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <wx/string.h>

namespace xtream {

class XtreamDatabase {
public:
  XtreamDatabase();
  ~XtreamDatabase();

  XtreamDatabase(const XtreamDatabase &) = delete;
  XtreamDatabase &operator=(const XtreamDatabase &) = delete;

  bool Open(const wxString &configDir, const std::string &uuid);
  void Close();
  bool IsOpen() const;

  // ---- meta (key/value) ----
  bool SetMeta(const std::string &key, const std::string &value);
  std::string GetMeta(const std::string &key) const;

  // ---- categories ----
  struct Category {
    int id = 0;
    std::string type;  // "live", "vod", "series"
    std::string name;
    int parentId = 0;
    int sortOrder = 0;
  };
  bool UpsertCategory(const Category &cat);
  std::vector<Category> GetCategories(const std::string &type) const;

  // ---- playback positions  ----
  struct PlaybackPosition {
    std::string mediaKey;
    double position = 0.0;
    double duration = 0.0;
    int64_t updatedAt = 0;
    bool finished = false;

    bool hasPosition() const { return position > 0.0; }
  };
  bool SavePlaybackPosition(const PlaybackPosition &p);
  PlaybackPosition GetPlaybackPosition(const std::string &mediaKey) const;
  std::vector<PlaybackPosition> GetRecentPlaybackPositions(int limit) const;

  // ---- user lists  ----
  enum class ListType { Favorites, Watchlist };
  bool AddToList(const std::string &mediaKey, ListType type);
  bool RemoveFromList(const std::string &mediaKey, ListType type);
  bool IsInList(const std::string &mediaKey, ListType type) const;
  std::vector<std::string> GetList(ListType type) const;

  // ---- VOD ----
  struct VodStream {
    int64_t streamId = 0;
    std::string name;
    std::string nameLower; // пусто → вычислит InsertVodStreams
    std::string posterUrl;
    double rating = 0.0;
    double rating5based = 0.0;
    int64_t added = 0;
    std::string containerExt;
    std::string customSid;
  };

    // ---- Series ----
  struct Series {
    int64_t seriesId = 0;
    std::string name;
    std::string nameLower; // пусто → вычислит InsertSeriesBatch
    std::string coverUrl;
    std::string plot;
    std::string castList;
    std::string director;
    std::string genre;
    std::string releaseDate;
    double rating = 0.0;
    double rating5based = 0.0;
    std::string backdropPath;
    std::string youtubeTrailer;
    std::string episodeRunTime;
    int64_t lastModified = 0;
  };
  
  // VOD-элементы, добавленные в список (favorites/watchlist), в порядке
  // "последние добавленные сверху". JOIN user_lists × vod_streams.
  // Для типичного числа избранного (десятки) — быстро.
  std::vector<VodStream> GetVodByList(ListType type, int limit = 1000) const;
  std::vector<Series> GetSeriesByList(ListType type, int limit = 1000) const;

  // Атомарно перезаписывает содержимое категории:
  //   1) DELETE FROM vod_category_map WHERE category_id = ?
  //   2) UPSERT каждой записи в vod_streams (ON CONFLICT stream_id)
  //   3) INSERT OR IGNORE в vod_category_map
  // НЕ трогает vod_info (привязано к stream_id, не к категории).
  // НЕ открывает транзакцию — вызывающий обязан быть внутри
  // BEGIN IMMEDIATE. При abortFlag=true или ошибке —
  // return false, откат делает вызывающий.
  // Лимиты : > 500000 — отказ; > 100000 — LOG_WARN.
  bool InsertVodStreams(int categoryId, const std::vector<VodStream> &streams,
                        const std::atomic<bool> &abortFlag);

  // Keyset pagination: stream_id > afterStreamId, ORDER BY stream_id
  // LIMIT limit. afterStreamId = 0 — с начала. limit клампится в [1, 5000].
  std::vector<VodStream> GetVodPage(int categoryId, int64_t afterStreamId,
                                    int limit) const;

  int64_t GetVodCount(int categoryId) const;

  // vod_info: один объект на stream_id, кэш ответа get_vod_info.
  // fetched_at — unix epoch, 0 = не задан (вызывающий заполняет).
  bool InsertVodInfo(int64_t streamId, const std::string &infoJson,
                     int64_t fetchedAt = 0);
  std::string GetVodInfo(int64_t streamId) const;

  // Атомарно перезаписывает содержимое категории series:
  //   1) DELETE FROM series_category_map WHERE category_id = ?
  //   2) UPSERT каждой серии (ON CONFLICT series_id)
  //   3) INSERT OR IGNORE в series_category_map
  // НЕ трогает series_seasons / series_episodes (привязаны к series_id).
  // Требует, чтобы вызывающий был внутри BEGIN IMMEDIATE.
  bool InsertSeriesBatch(int categoryId, const std::vector<Series> &items,
                         const std::atomic<bool> &abortFlag);

  std::vector<Series> GetSeriesPage(int categoryId, int64_t afterSeriesId,
                                    int limit) const;
  int64_t GetSeriesCount(int categoryId) const;

  struct Season {
    int64_t seriesId = 0;
    int seasonNumber = 0;
    std::string name;
    std::string coverUrl;
    std::string plot;
    int episodeCount = 0;
    std::string airDate;
  };

  struct Episode {
    std::string id; // get_series_info episodes[*].id (строка)
    int64_t seriesId = 0;
    int seasonNumber = 0;
    int episodeNum = 0;
    std::string title;
    std::string containerExt;
    std::string infoJson;
    std::string customSid;
    int64_t added = 0;
  };

  // Атомарно перезаписывает сезоны и эпизоды одной серии:
  //   1) DELETE FROM series_seasons WHERE series_id = ?
  //   2) DELETE FROM series_episodes WHERE series_id = ?
  //   3) INSERT сезонов и эпизодов
  // НЕ трогает саму series (создаётся InsertSeriesBatch).
  // Если series_id не существует — ошибка FK (ON DELETE CASCADE здесь
  // не помогает, потому что мы DELETE, а не INSERT родителя).
  // Требует BEGIN IMMEDIATE.
  bool InsertSeriesInfo(int64_t seriesId, const std::vector<Season> &seasons,
                        const std::vector<Episode> &episodes,
                        const std::atomic<bool> &abortFlag);

  std::vector<Season> GetSeasons(int64_t seriesId) const;
  std::vector<Episode> GetEpisodes(int64_t seriesId, int seasonNumber) const;

  // ---- Search + Recently Added ----
  // Поиск по name_lower. queryLower должен быть приведён вызывающим
  // через wxString::Lower() — SQLite lower() не работает с кириллицей
  // Спецсимволы LIKE (% и _) НЕ эскейпятся.
  // Пустой queryLower → пустой вектор. limit клампится в [1, 1000].
  std::vector<VodStream> SearchVod(const std::string &queryLower,
                                   int limit = 200) const;
  std::vector<Series> SearchSeries(const std::string &queryLower,
                                   int limit = 200) const;

  // ORDER BY added DESC LIMIT ? (для VOD), ORDER BY last_modified DESC
  // для Series. limit клампится в [1, 500]. типичный limit = 100.
  std::vector<VodStream> GetRecentlyAddedVod(int limit = 100) const;
  std::vector<Series> GetRecentlyAddedSeries(int limit = 100) const;

  // Одиночные карточки. При отсутствии stream_id/series_id возвращают
  // структуру с нулевым ID (вызывающий проверяет).
  VodStream GetVod(int64_t streamId) const;
  Series GetSeries(int64_t seriesId) const;

  // ---- Транзакции ----
  // Открывает BEGIN IMMEDIATE. Не вложенная — повторный вызов вернёт false.
  // Commit/Rollback — без эффекта, если транзакция не открыта.
  bool BeginTransaction();
  bool Commit();
  bool Rollback();

  // ---- VOD batch API для SAX ----
  // ClearVodCategoryMap + многократный UpsertVodBatch = атомарная
  // перезапись категории. InsertVodStreams делает то же в одном
  // вызове, но требует весь массив в памяти заранее — не подходит для SAX.

  bool ClearVodCategoryMap(int categoryId);

  // UPSERT каждой записи в vod_streams + INSERT OR IGNORE в
  // vod_category_map. БЕЗ DELETE. Вызывать после ClearVodCategoryMap.
  // abortFlag проверяется каждые 1000 записей.
  bool UpsertVodBatch(int categoryId, const std::vector<VodStream> &batch,
                      const std::atomic<bool> &abortFlag);

  // ---- Series batch API для SAX ----
  bool ClearSeriesCategoryMap(int categoryId);

  // UPSERT каждой записи в series + INSERT OR IGNORE в
  // series_category_map. БЕЗ DELETE. Вызывать после ClearSeriesCategoryMap.
  // Не трогает series_seasons/series_episodes (они через InsertSeriesInfo).
  bool UpsertSeriesBatch(int categoryId, const std::vector<Series> &batch,
                         const std::atomic<bool> &abortFlag);

  // ---- размер файла БД  ----
  long long FileSizeBytes() const;

private:
  bool CreateSchemaV1(bool inTransaction);

  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace xtream