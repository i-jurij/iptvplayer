#include "Application.h"
#include "CardsBase.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"

#include <algorithm>
#include <map>
#include <mutex>

void CardsBase::RequestLogo(size_t index) {
  PROFILE_SCOPE("CardsBase::RequestLogo");

  if (auto *mf = GetMainFrame();
      mf && mf->m_gridState != ChannelsViewState::Ready)
    return;

  if (wxWindow *top = wxGetTopLevelParent(this)) {
    if (auto *mf = dynamic_cast<MainFrame *>(top)) {
      if (!mf->AreLogosEnabled())
        return;
    }
  }

  if (index >= m_channels.size())
    return;
  if (m_loadingPaused.load(std::memory_order_relaxed))
    return;

  Channel chCopy = m_channels[index];
  const std::string url = chCopy.getLogo();
  if (url.empty())
    return;

  int dpi = GetNormDPI(this);

  const std::string key = MakeLogoCacheKey(
      chCopy.getPlaylistName(), chCopy.getName(), m_logoW, m_logoH, dpi);

  if (LogoCache::GetCachedBitmapPtr(key))
    return;

  uint64_t ver = m_channelsVersion.load(std::memory_order_relaxed);
  uint64_t cacheVer = m_cacheVersion.load(std::memory_order_relaxed);

  m_activeLoads++;
  m_lastLoadStart = wxGetUTCTimeMillis();

  LogoCache::GetLogoAsync(
      chCopy.getPlaylistName(), chCopy.getName(), url, m_logoW, m_logoH, dpi,
      [this, index, ver, cacheVer, key,
       chCopy](LogoCache::LogoBitmapPtr bmpPtr) {
        auto bmp_copy = bmpPtr;
        auto key_copy = key;
        auto ch_copy = chCopy;
        auto idx_copy = index;
        auto ver_copy = ver;
        auto cacheVer_copy = cacheVer;

        wxTheApp->CallAfter([this, idx_copy, ver_copy, cacheVer_copy, key_copy,
                             bmp_copy, ch_copy]() {
          if (m_activeLoads > 0)
            m_activeLoads--;

          if (m_channelsVersion.load(std::memory_order_relaxed) != ver_copy)
            return;
          if (m_cacheVersion.load(std::memory_order_relaxed) != cacheVer_copy)
            return;
          if (idx_copy >= m_channels.size())
            return;

          const Channel &cur = m_channels[idx_copy];
          if (cur.getName() != ch_copy.getName() ||
              cur.getPlaylistName() != ch_copy.getPlaylistName())
            return;
          if (!bmp_copy || !bmp_copy->IsOk() || m_closing)
            return;

          // tile‑only: обновляем только тайл и карточку
          RenderTile(idx_copy);
          MarkCardDirty((int)idx_copy);

          if (!m_logoQueuePQ.empty() && !m_logoTimer.IsRunning())
            m_logoTimer.Start(20);
        });
      });
}

void CardsBase::ProcessLogoQueue(wxTimerEvent &) {
  PROFILE_SCOPE("CardsBase::ProcessLogoQueue");

  if (auto *mf = GetMainFrame();
      mf && mf->m_gridState != ChannelsViewState::Ready)
    return;

  if (wxWindow *top = wxGetTopLevelParent(this)) {
    if (auto *mf = dynamic_cast<MainFrame *>(top)) {
      if (!mf->AreLogosEnabled()) {
        m_logoTimer.Stop();
        m_logoQueuePQ.clear();
        return;
      }
    }
  }

  // --- Динамический параллелизм ---
  if (m_lastLoadTimeMs < 40 && m_dynamicParallel < MAX_PARALLEL_LOADS)
    m_dynamicParallel++;
  else if (m_lastLoadTimeMs > 120 && m_dynamicParallel > MIN_PARALLEL_LOADS)
    m_dynamicParallel--;

  if (m_dynamicParallel < MIN_PARALLEL_LOADS)
    m_dynamicParallel = MIN_PARALLEL_LOADS;
  if (m_dynamicParallel > MAX_PARALLEL_LOADS)
    m_dynamicParallel = MAX_PARALLEL_LOADS;

  if (m_activeLoads >= m_dynamicParallel)
    return;

  if (m_logoQueuePQ.empty()) {
    m_logoTimer.Stop();
    return;
  }

  LogoTask task = m_logoQueuePQ.front();
  m_logoQueuePQ.pop_front();

  size_t index = task.index;
  if (index >= m_channels.size())
    return;

  const Channel ch = m_channels[index];
  const std::string url = ch.getLogo();
  if (url.empty())
    return;

  const int dpi = GetNormDPI(this);
  const uint64_t ver = m_channelsVersion.load(std::memory_order_relaxed);

  const std::string key = MakeLogoCacheKey(ch.getPlaylistName(), ch.getName(),
                                           m_logoW, m_logoH, dpi);
  if (LogoCache::GetCachedBitmapPtr(key))
    return;

  // --- Предиктивный prefetch ---
  int sx, sy;
  GetViewStart(&sx, &sy);
  int px, py;
  GetScrollPixelsPerUnit(&px, &py);
  if (px <= 0)
    px = 1;
  if (py <= 0)
    py = 1;

  const int scrollY = sy * py;
  const int clientH = GetClientSize().GetHeight();
  const int row = index / m_cols;
  const int y = row * m_rowH;

  bool visible = !(y + m_rowH < scrollY - m_rowH * 2 ||
                   y > scrollY + clientH + m_rowH * 2);

  if (visible && task.priority > 1) {
    EnqueueLogoPriority(index, 1);
    return;
  }

  int prefetchRows = 4;
  int predictedRow = row + m_scrollDirection * prefetchRows;

  if (m_scrollDirection != 0) {
    int totalRows = (int)((m_channels.size() + m_cols - 1) / m_cols);
    if (predictedRow >= 0 && predictedRow < totalRows) {
      int predictedIndex = predictedRow * m_cols + (index % m_cols);
      if (predictedIndex < (int)m_channels.size()) {
        const Channel preCh = m_channels[predictedIndex];
        const std::string preUrl = preCh.getLogo();
        if (!preUrl.empty()) {
          const std::string preKey = MakeLogoCacheKey(
              preCh.getPlaylistName(), preCh.getName(), m_logoW, m_logoH, dpi);
          if (!LogoCache::GetCachedBitmapPtr(preKey)) {
            EnqueueLogoPriority(predictedIndex, 2);
          }
        }
      }
    }
  }

  bool isFav = false;
  if (MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
    isFav = mf->getApplication()->getFavoritesManager().isFavorite(ch);
  }
  int basePriority = isFav ? 0 : task.priority;
  if (basePriority != task.priority) {
    EnqueueLogoPriority(index, basePriority);
    return;
  }

  if (m_loadingPaused.load(std::memory_order_relaxed)) {
    EnqueueLogoPriority(index, task.priority);
    return;
  }

  const uint64_t cacheVer = m_cacheVersion.load(std::memory_order_relaxed);

  m_activeLoads++;
  m_lastLoadStart = wxGetUTCTimeMillis();

  LogoCache::GetLogoAsync(
      ch.getPlaylistName(), ch.getName(), url, m_logoW, m_logoH, dpi,
      [this, index, ver, cacheVer, key, ch](LogoCache::LogoBitmapPtr bmpPtr) {
        auto bmp_copy = bmpPtr;
        auto key_copy = key;
        auto ch_copy = ch;
        auto idx_copy = index;
        auto ver_copy = ver;
        auto cacheVer_copy = cacheVer;

        wxTheApp->CallAfter([this, idx_copy, ver_copy, cacheVer_copy, key_copy,
                             bmp_copy, ch_copy]() {
          if (m_activeLoads > 0)
            m_activeLoads--;

          // --- Измеряем время загрузки ---
          int elapsed = (int)(wxGetUTCTimeMillis() - m_lastLoadStart).ToLong();
          m_lastLoadTimeMs = (m_lastLoadTimeMs * 3 + elapsed) / 4;

          if (m_channelsVersion.load(std::memory_order_relaxed) != ver_copy)
            return;
          if (m_cacheVersion.load(std::memory_order_relaxed) != cacheVer_copy)
            return;
          if (idx_copy >= m_channels.size())
            return;

          const Channel &cur = m_channels[idx_copy];
          if (cur.getName() != ch_copy.getName() ||
              cur.getPlaylistName() != ch_copy.getPlaylistName())
            return;
          if (!bmp_copy || !bmp_copy->IsOk() || m_closing)
            return;

          RenderTile(idx_copy);
          MarkCardDirty((int)idx_copy);

          if (!m_logoQueuePQ.empty() && !m_logoTimer.IsRunning())
            m_logoTimer.Start(20);
        });
      });
}

void CardsBase::EnqueueLogoPriority(size_t index, int priority) {
  if ((int)m_logoQueuePQ.size() >= MAX_LOGO_QUEUE) {
    if (priority > 0)
      return;
  }

  for (auto &t : m_logoQueuePQ)
    if (t.index == index)
      return;
  LogoTask task{index, priority};
  auto it = std::find_if(
      m_logoQueuePQ.begin(), m_logoQueuePQ.end(),
      [&](const LogoTask &other) { return priority < other.priority; });
  m_logoQueuePQ.insert(it, task);
  if (!m_logoTimer.IsRunning())
    m_logoTimer.Start(20);
}

void CardsBase::WarmUpFavorites() {
  PROFILE_SCOPE("CardsBase::WarmUpFavorites");

  auto *mf = GetMainFrame();
  if (!mf || !mf->AreLogosEnabled())
    return;

  if (mf && mf->m_gridState != ChannelsViewState::Ready)
    return;

  if (MainFrame *mf2 = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
    auto &favMgr = mf2->getApplication()->getFavoritesManager();
    int dpi = GetCurrentDPI();
    const uint64_t ver = m_channelsVersion.load(std::memory_order_relaxed);

    for (size_t i = 0; i < m_channels.size(); ++i) {
      const Channel ch = m_channels[i];
      const std::string &url = ch.getLogo();
      if (!favMgr.isFavorite(ch))
        continue;
      if (url.empty())
        continue;

      const std::string key = MakeLogoCacheKey(
          ch.getPlaylistName(), ch.getName(), m_logoW, m_logoH, dpi);
      if (LogoCache::GetCachedBitmapPtr(key))
        continue;

      LogoCache::GetLogoAsync(
          ch.getPlaylistName(), ch.getName(), url, m_logoW, m_logoH, dpi,
          [this, i, ver, key, ch](LogoCache::LogoBitmapPtr bmpPtr) {
            auto bmp_copy = bmpPtr;
            auto key_copy = key;
            auto ch_copy = ch;
            auto idx_copy = i;
            auto ver_copy = ver;

            wxTheApp->CallAfter([this, idx_copy, ver_copy, key_copy, bmp_copy,
                                 ch_copy]() {
              if (m_channelsVersion.load(std::memory_order_relaxed) != ver_copy)
                return;
              if (idx_copy >= m_channels.size())
                return;
              const Channel &cur = m_channels[idx_copy];
              if (cur.getName() != ch_copy.getName() ||
                  cur.getPlaylistName() != ch_copy.getPlaylistName())
                return;
              if (!bmp_copy || !bmp_copy->IsOk() || m_closing)
                return;

              // tile‑only
              RenderTile(idx_copy);
              MarkCardDirty((int)idx_copy);
            });
          });
    }
  }
}

void CardsBase::WarmUpTiles() {
  PROFILE_SCOPE("CardsBase::WarmUpTiles");

  wxLongLong now = wxGetUTCTimeMillis();
  if (now < m_skipWarmupUntil)
    return;

  auto *mf = GetMainFrame();
  if (!mf || !mf->AreLogosEnabled())
    return;

  if (mf->m_gridState != ChannelsViewState::Ready)
    return;

  if (m_channels.empty() || m_cols <= 0 || m_rowH <= 0)
    return;

  int sx = 0, sy = 0;
  GetViewStart(&sx, &sy);
  int px = 1, py = 1;
  GetScrollPixelsPerUnit(&px, &py);
  if (py <= 0)
    py = 1;

  int scrollY = sy * py;
  int clientH = GetClientSize().GetHeight();

  int rowsPerScreen = std::max(1, clientH / m_rowH);

  int firstRow = scrollY / m_rowH - rowsPerScreen;
  int lastRow = (scrollY + clientH) / m_rowH + rowsPerScreen;

  if (firstRow < 0)
    firstRow = 0;

  int totalRows = (int)((m_channels.size() + m_cols - 1) / m_cols);
  if (lastRow >= totalRows)
    lastRow = totalRows - 1;

  const int dpi = GetCurrentDPI();
  m_scaledKeyToIndices.clear();

  int enqueued = 0;

  for (int row = firstRow; row <= lastRow; ++row) {
    for (int col = 0; col < m_cols; ++col) {
      int index = row * m_cols + col;
      if (index >= (int)m_channels.size())
        break;

      const Channel &ch = m_channels[index];
      const std::string &url = ch.getLogo();
      if (url.empty())
        continue;

      const std::string key = MakeLogoCacheKey(
          ch.getPlaylistName(), ch.getName(), m_logoW, m_logoH, dpi);

      m_scaledKeyToIndices[key].push_back(index);

      // логотип уже есть → убедиться, что тайл создан
      if (LogoCache::GetCachedBitmapPtr(key)) {
        auto it = m_tileCacheDPI[dpi].find(index);
        if (it == m_tileCacheDPI[dpi].end() || !it->second ||
            !it->second->IsOk()) {
          RenderTile(index);
        }
        continue;
      }

      // логотипа нет → ставим в очередь
      EnqueueLogoPriority(index, 1);
      ++enqueued;
    }
  }

  if (!m_logoTimer.IsRunning() && !m_logoQueuePQ.empty()) {
    m_logoTimer.Start(20);
  }

  Refresh();

  static int m_warmupCounter = 0;
  if (++m_warmupCounter >= 150) {
    m_warmupCounter = 0;
    TrimTextCache();
  }

  LogoCache::DebugMemoryUsage();
  DebugTileMemory();
  DebugInternalMemory();
}

void CardsBase::AddTileToLRU(size_t index,
                             const LogoCache::LogoBitmapPtr &bmpPtr) {
  if (!bmpPtr || !bmpPtr->IsOk())
    return;

  auto it = m_tileLRUCache.find(index);
  if (it != m_tileLRUCache.end()) {
    m_tileLRU.remove(index);
    m_tileLRU.push_front(index);
    it->second = bmpPtr;
    return;
  }

  m_tileLRU.push_front(index);
  m_tileLRUCache[index] = bmpPtr;

  if ((int)m_tileLRU.size() > MAX_TILE_LRU) {
    size_t old = m_tileLRU.back();
    m_tileLRU.pop_back();
    m_tileLRUCache.erase(old);

    for (auto &kv : m_tileCacheDPI) {
      kv.second.erase(old);
    }
  }
}

void CardsBase::TrimTextCache() {
  static const size_t MAX_TEXT_CACHE = 15000;
  if (m_textCache.size() > MAX_TEXT_CACHE) {
    m_textCache.clear();
    m_textSizeCache.clear();
  }
}

void CardsBase::ClearAllCaches(bool clearLRU, bool clearTextLayout) {
  IncrementCacheVersion();

  {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    if (clearTextLayout) {
      m_textCache.clear();
      m_textSizeCache.clear();
      m_layoutCache.clear();
    }

    m_tileCacheDPI.clear();

    if (clearLRU) {
      m_tileLRU.clear();
      m_tileLRUCache.clear();
    }

    m_scaledKeyToIndices.clear();
    m_dirtyCards.clear();
  }

  if (m_logoTimer.IsRunning())
    m_logoTimer.Stop();
  m_logoQueuePQ.clear();

  int winId = this->GetId();
  CallAfterSafeById(winId, [](wxWindow *w) {
    auto *self = dynamic_cast<CardsBase *>(w);
    if (!self)
      return;
    if (self->m_closing)
      return;
    self->Refresh();
  });
}

void CardsBase::StopWarmUp() {
  PROFILE_SCOPE("CardsBase::StopWarmUp");

  if (m_logoTimer.IsRunning())
    m_logoTimer.Stop();

  m_logoQueuePQ.clear();
  m_scaledKeyToIndices.clear();

  LOG_DEBUG("CardsBase::StopWarmUp - warm-up stopped");
}
