#pragma once

#include "Playlist.h"
#include "xtream/XtreamDatabase.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xtream {

class XtreamApiClient {
public:
  using ProgressFn = std::function<void(int percent, const std::string &stage)>;

  XtreamApiClient();
  ~XtreamApiClient();

  XtreamApiClient(const XtreamApiClient &) = delete;
  XtreamApiClient &operator=(const XtreamApiClient &) = delete;

  // Сбрасывает флаг отмены в начале метода. Устанавливает true —
  // отменяет текущий curl-трансфер через XFERINFO callback.
  void Cancel();
  // сбросить флаг "уже предупредили о размере > 2 GB".
  // Вызывается из PlaylistManager::ClearAllXtreamCaches — после очистки
  // следующее превышение снова даст LOG_WARN.
  static void ResetSizeWarn();

  // ---- Auth ----
  struct AuthResult {
    bool ok = false;
    std::string error;
    std::string userInfoJson;
    std::string serverInfoJson;
    std::string baseUrl;
    std::string username;
    std::string password;
  };

  // player_api.php?username=X&password=Y.
  // Retry: 3 попытки, backoff 2/4/8s + jitter 0-0.5s. Retry на 5xx,
  // 429 (Retry-After, max 60s), network errors. Не retry на 4xx и HTML.
  AuthResult Auth(const Playlist &pl);

  // ---- Categories (DOM) ----
  enum class CategoryType { Live, Vod, Series };

  struct Category {
    int id = 0;
    std::string name;
  };

  // get_short_epg возвращает только ~10 ближайших
  // передач. Не заменяет XMLTV, а дополняет. Поля title/description
  // обычно base64-encoded (с префиксом "base64:" или без) — decode
  // уже применён.
  struct ShortEpgProgram {
    std::string id;
    std::string epgId;
    std::string title;
    std::string description;
    std::string lang;
    std::string channelId;
    int64_t startUtc = 0;
    int64_t stopUtc = 0;
    bool nowPlaying = false;
    bool hasArchive = false;
  };
  std::vector<Category> GetCategories(const Playlist &pl, CategoryType type);
  // get_short_epg. streamId — live_streams.stream_id (не VOD!).
  // limit 1..100 (default 10). Возвращает пустой вектор при ошибке.
  std::vector<ShortEpgProgram> GetShortEpg(const Playlist &pl, int64_t streamId,
                                           int limit = 10);

  // ---- VOD ----
  // SAX-парсинг get_vod_streams → batched INSERT в SQLite.
  // Транзакция: BEGIN IMMEDIATE → ClearVodCategoryMap → N× UpsertVodBatch
  // → SetMeta(last_update_vod_<cat>) → COMMIT. При ошибке — ROLLBACK.
  // cancelFlag опрашивается в SAX handler каждые 1000 записей.
  // progress вызывается каждые 5000 записей: percent=-1, stage="VOD: N items".
  // Возвращает false при: network error, parse error, abort, SQL error.
  bool RefreshVodCategory(const Playlist &pl, int categoryId,
                          XtreamDatabase &db, ProgressFn progress,
                          const std::atomic<bool> &cancelFlag);

  // ---- Series ----
  // SAX-парсинг get_series → batched INSERT в SQLite.
  // Сезоны/эпизоды НЕ загружаются — только карточки сериалов.
  // Полная загрузка сериала (get_series_info) — отдельный метод.
  bool RefreshSeriesCategory(const Playlist &pl, int categoryId,
                             XtreamDatabase &db, ProgressFn progress,
                             const std::atomic<bool> &cancelFlag);

  // ---- VOD/Series info (DOM) ----
  // get_vod_info: сохраняет raw JSON `info` объекта в vod_info.
  // При отсутствии info — весь response. При повторном открытии UI
  // читает через XtreamDatabase::GetVodInfo (кэш в БД).
  // outJson — заполняется при успехе; пустая строка при ошибке.
  bool FetchVodInfo(const Playlist &pl, int64_t streamId, XtreamDatabase &db,
                    std::string &outJson, const std::atomic<bool> &cancelFlag);

  // get_series_info: DOM-парсинг, нормализация episodes (dict/list),
  // атомарная замена seasons+episodes через InsertSeriesInfo.
  // Если seasons пуст — синтезируется из episodes.
  bool FetchSeriesInfo(const Playlist &pl, int64_t seriesId, XtreamDatabase &db,
                       ProgressFn progress,
                       const std::atomic<bool> &cancelFlag);

  // ---- URL builders ----
  // Возвращают пустую строку при ошибке (нет endpoint, невалидные id).
  // container_extension: caller берёт из VodStream/Episode. Если пусто —
  // fallback: "mp4" для VOD, "ts" для episodes/catch-up.
  // URL-encoded: username, password, episode id, catch-up start.

  std::string BuildVodUrl(const Playlist &pl, int64_t streamId,
                          const std::string &containerExt) const;

  std::string BuildSeriesEpisodeUrl(const Playlist &pl,
                                    const std::string &episodeId,
                                    const std::string &containerExt) const;

  // startUtc — UTC epoch (Xtream convention), durationMin — минуты.
  // path-style: {base}/timeshift/{u}/{p}/{dur}/{start}/{id}.ts
  // query-style: {base}/streaming/timeshift.php?username=..&password=..
  //              &stream=..&start=..&duration=..
  // start формат: YYYY-MM-DD:HH-MM (UTC).
  std::string BuildCatchupUrl(const Playlist &pl, int64_t streamId,
                              int64_t startUtc, int durationMin,
                              bool useQueryStyle = false) const;
  // ---- HTTP (public для тестов) ----
  struct HttpResponse {
    bool ok = false;
    long httpCode = 0;
    std::string body;
    std::string error;
  };

  HttpResponse HttpGet(const Playlist &pl, const std::string &url,
                       int timeoutSec = 30);

private:
  // base + credentials, извлечённые из XtreamMeta или source.
  struct Endpoint {
    bool ok = false;
    std::string base;
    std::string user;
    std::string pass;
  };
  Endpoint ResolveEndpoint(const Playlist &pl) const;

  // Копирует Playlist::m_xtreamMeta в xtream.db.meta.
  // Идемпотентно: SetMeta использует ON CONFLICT DO UPDATE.
  // Пароль НЕ записывается (остаётся только в Playlist JSON).
  void SyncMetaToDb(const Playlist &pl, XtreamDatabase &db) const;

  std::atomic<bool> m_cancel{false};
};

} // namespace xtream
