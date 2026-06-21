#include "ChannelList.h"
#include "Application.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"

#include <algorithm>
#include <chrono>
#include <thread>

ChannelList::ChannelList(wxWindow *parent, wxWindowID id)
    : BaseChannelList(parent, id) {
  Bind(wxEVT_KEY_DOWN, &ChannelList::OnKeyDown, this);
}

ChannelList::~ChannelList() {
  // stop background worker cleanly
  StopBackgroundPrefetch();
  if (m_bgWorker.joinable())
    m_bgWorker.join();
}

void ChannelList::OnChannelActivated(const Channel &ch, int col) {
  if (col == 2)
    return;

  if (!m_onSelect)
    return;

  m_onSelect(ch, 0, wxRect());
}

void ChannelList::OnFavoriteToggled(const Channel &ch, bool isFav) {
  if (MainFrame *parentFrame =
          dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {

    if (isFav)
      parentFrame->getApplication()->getFavoritesManager().add(ch);
    else
      parentFrame->getApplication()->getFavoritesManager().remove(ch.getName(),
                                                                  ch.getPlaylistName());

    parentFrame->refreshFavorites();
  }
}

static size_t ComputeBatchSizeForMemory() {
  size_t availMB = GetAvailableRAM_MB();

  if (availMB < 1024) {
    return 100;
  } else if (availMB < 2048) {
    return 250;
  } else if (availMB < 4096) {
    return 500;
  } else {
    return 1000;
  }
}

// Start background prefetch worker
void ChannelList::StartBackgroundPrefetch() {
  bool expected = false;
  if (!m_bgRunning.compare_exchange_strong(expected, true))
    return; // already running

  m_bgCv.notify_all(); // wake if waiting
  m_bgWorker = std::thread([this]() { BackgroundPrefetchLoop(); });
}

// Stop background prefetch worker
void ChannelList::StopBackgroundPrefetch() {
  bool expected = true;
  if (!m_bgRunning.compare_exchange_strong(expected, false))
    return; // already stopped
  m_bgCv.notify_all();
}

// Background worker: gradually enqueue low-priority distant items.
// Respects m_maxTotalPending and m_prefetchRemaining; sleeps between chunks.
void ChannelList::BackgroundPrefetchLoop() {
  PROFILE_SCOPE("ChannelList::BackgroundPrefetchLoop");
  std::unique_lock<std::mutex> lk(m_bgMutex);
  while (m_bgRunning.load()) {
    // Wait a short while to coalesce bursts; also wakeable via cv
    m_bgCv.wait_for(lk, std::chrono::milliseconds(100));

    if (!m_bgRunning.load())
      break;
    if (m_closing.load())
      break;

    // If loading paused or queue paused, back off
    if (m_queuePaused.load() || m_loadingPaused.load()) {
      // sleep longer while paused
      m_bgCv.wait_for(lk, std::chrono::milliseconds(500));
      continue;
    }

    // compute current ranges
    size_t modelCount = m_model ? m_model->GetCount() : 0;
    if (modelCount == 0) {
      // nothing to do
      m_bgCv.wait_for(lk, std::chrono::milliseconds(500));
      continue;
    }

    int topInt = GetTopVisibleRow();
    int visibleInt = GetCountPerPage();
    size_t top = (topInt > 0) ? static_cast<size_t>(topInt) : 0;
    size_t visible = (visibleInt > 0) ? static_cast<size_t>(visibleInt) : 0;
    size_t visibleEnd = std::min(modelCount, top + visible);

    // dynamic effective prefetch (adaptive)
    size_t effectivePrefetch =
        std::min(BaseChannelList::kPrefetchCount, modelCount);
    effectivePrefetch =
        std::min(effectivePrefetch, m_maxTotalPending / 4 + (size_t)50);

    // adaptive increase when system is idle: check CPU/memory heuristics
    // (Utils helpers used; if not available, this is a no-op safe path)
    size_t extra = 0;
    try {
      double cpuLoad = GetSystemCPULoadPercent(); // 0..100, helper may return
                                                  // -1 if unsupported
      size_t availMB = GetAvailableRAM_MB();
      if (cpuLoad >= 0 && cpuLoad < 30.0 && availMB > 2048) {
        // system idle-ish: increase prefetch by up to 2x
        extra = effectivePrefetch; // double
      }
    } catch (...) {
      extra = 0;
    }
    effectivePrefetch = std::min(modelCount, effectivePrefetch + extra);

    size_t prefetchEnd = std::min(modelCount, visibleEnd + effectivePrefetch);

    // initialize next index if needed
    if (m_bgNextIndex < visibleEnd)
      m_bgNextIndex = visibleEnd;

    if (m_bgNextIndex >= prefetchEnd) {
      // nothing new to prefetch right now
      // sleep a bit longer before re-checking
      m_bgCv.wait_for(lk, std::chrono::milliseconds(500));
      continue;
    }

    // chunked enqueue to avoid flooding queue
    const size_t chunkSize = 200;
    size_t start = m_bgNextIndex;
    size_t end = std::min(prefetchEnd, start + chunkSize);

    // If global pending budget is nearly full, back off
    size_t totalPending = 0;
    {
      std::lock_guard<std::mutex> qlk(m_queueMutex);
      totalPending = m_pendingLogoLoads.size() + m_loadQueue.size();
    }
    if (totalPending >= m_maxTotalPending) {
      // back off and retry later
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }

    // Enqueue with lowest priority (farthest first within chunk to favor
    // nearer)
    for (size_t idx = start; idx < end; ++idx) {
      // small safety checks
      if (m_closing.load() || !m_bgRunning.load())
        break;
      if (idx >= modelCount)
        break;

      // For background worker we always use low priority (false)
      try {
        m_model->RequestLogoLoadIfMissing(static_cast<unsigned int>(idx),
                                          false);
      } catch (...) {
      }
    }

    // advance pointer
    m_bgNextIndex = end;

    // small sleep to yield and allow queue processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

// ChannelList.cpp — метод loadChannelsAsync (полный)
void ChannelList::loadChannelsAsync(const std::vector<Channel> &channels,
                                    const std::string &playlistName) {
  PROFILE_SCOPE("ChannelList::loadChannelsAsync");
  if (m_closing.load())
    return;
  if (channels.empty())
    return;

  int topRow = GetTopVisibleRow();

  const size_t initialCount = std::min<size_t>(50, channels.size());
  std::vector<Channel> initialBatch(channels.begin(),
                                    channels.begin() + initialCount);

  for (auto &ch : initialBatch)
    if (ch.getPlaylistName().empty())
      ch.setPlaylistName(playlistName);

  BeginFavoritesSync();
  LoadChannels(initialBatch, playlistName); // minimal initial set
  EndFavoritesSync();
  RestoreTopVisibleRow(topRow);

  if (MainFrame *parentFrame =
          dynamic_cast<MainFrame *>(wxGetTopLevelParent(this))) {
    auto favChannels =
        parentFrame->getApplication()->getFavoritesManager().list();
    
    std::vector<std::pair<std::string, std::string>> favKeys;
    favKeys.reserve(favChannels.size());
    for (const auto &c : favChannels)
      favKeys.emplace_back(c.getName(), c.getPlaylistName());

    BeginFavoritesSync();
    GetModel()->SetFavorites(favKeys);
    EndFavoritesSync();
  }

  const int winId = GetId();

  std::thread([winId, channels, playlistName, initialCount]() {
    try {
      const size_t rawBatch = ComputeBatchSizeForMemory();
      const size_t MAX_BATCH_SAFE = 2000;
      const size_t batchSize = std::min(rawBatch, MAX_BATCH_SAFE);

      std::vector<Channel> batch;
      batch.reserve(std::min(batchSize, (size_t)10000));

      for (size_t i = initialCount; i < channels.size(); ++i) {
        Channel ch = channels[i];
        if (ch.getPlaylistName().empty())
          ch.setPlaylistName(playlistName);

        batch.push_back(std::move(ch));

        if (batch.size() == batchSize || i == channels.size() - 1) {
          auto copyBatch = batch;

          CallAfterSafeById(winId, [winId, copyBatch = std::move(copyBatch),
                                    playlistName, batchSize]() mutable {
            auto *win = wxWindow::FindWindowById(winId);
            auto *chList = dynamic_cast<ChannelList *>(win);
            if (!chList || chList->m_closing.load())
              return;

            int prevTopRow = chList->GetTopVisibleRow();
            try {
              chList->BeginFavoritesSync();
              chList->GetModel()->AppendChannels(copyBatch, playlistName, 0,
                                                 GetNormDPI(chList));
              chList->EndFavoritesSync();

              //LOG_DEBUG("AppendChannels called winId=%d appended=%zu", winId,
                //        copyBatch.size());

              chList->m_lastTopRow = 0;
              chList->m_lastVisibleCount = 0;

              chList->RestoreTopVisibleRow(prevTopRow);

              int topInt = chList->GetTopVisibleRow();
              int visibleInt = chList->GetCountPerPage();
              size_t top = (topInt > 0) ? static_cast<size_t>(topInt) : 0;
              size_t visible =
                  (visibleInt > 0) ? static_cast<size_t>(visibleInt) : 0;
              size_t modelCount = chList->GetModel()->GetCount();

              size_t visibleEnd = std::min(modelCount, top + visible);

              size_t effectivePrefetch =
                  std::min(BaseChannelList::kPrefetchCount, modelCount);
              effectivePrefetch =
                  std::min(effectivePrefetch,
                           chList->m_maxTotalPending / 4 + (size_t)50);

              size_t prefetchEnd =
                  std::min(modelCount, visibleEnd + effectivePrefetch);

              // smaller chunk size to avoid flooding
              size_t chunkSize = std::min<size_t>(200, batchSize);
              for (size_t start = top; start < prefetchEnd;
                   start += chunkSize) {
                size_t end = std::min(prefetchEnd, start + chunkSize);
                for (size_t idx = start; idx < end; ++idx) {
                  bool highPriority = (idx < visibleEnd);
                  chList->GetModel()->RequestLogoLoadIfMissing(
                      static_cast<unsigned int>(idx), highPriority);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
              }

              // start background prefetch worker to gradually fetch farther
              // items
              chList->m_bgNextIndex =
                  prefetchEnd; // start after current prefetch
              chList->StartBackgroundPrefetch();

              chList->CoalescedDoLazyLoadSchedule();

            } catch (...) {
            }
          });

          batch.clear();
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      }

      CallAfterSafeById(winId, [winId]() {
        auto *win = wxWindow::FindWindowById(winId);
        auto *chList = dynamic_cast<ChannelList *>(win);
        if (!chList || chList->m_closing.load())
          return;
        int topRow = chList->GetTopVisibleRow();
        try {
          auto *model = chList->GetModel();
          model->SetSorting(model->GetSortColumn(), model->IsSortAscending());
          chList->RestoreTopVisibleRow(topRow);

          // пересчитать режим и лимиты с учётом финального размера плейлиста
          chList->ResumeLogoLoading();

          chList->DoLazyLoad();
        } catch (...) {
        }
      });

    } catch (...) {
    }
  }).detach();
}

void ChannelList::OnKeyDown(wxKeyEvent &evt) {
  if (!this->IsShownOnScreen()) {
    evt.Skip();
    return;
  }
  
  int key = evt.GetKeyCode();
  wxDataViewItem item = GetSelection();
  if (!item.IsOk()) {
    evt.Skip();
    return;
  }
  int row = m_model->GetRow(item);
  if (row < 0 || row >= (int)m_model->GetCount()) {
    evt.Skip();
    return;
  }
  const Channel &ch = m_model->GetChannel(row);

  switch (key) {
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    OnChannelActivated(ch, 0);
    return;
  case WXK_SPACE: {
    bool isFav = !m_model->IsFavorite(row);
    OnFavoriteToggled(ch, isFav);
    auto favList = wxGetApp().getFavoritesManager().list();
    std::vector<std::pair<std::string, std::string>> favKeys;
    favKeys.reserve(favList.size());
    for (auto &c : favList)
      favKeys.emplace_back(c.getName(), c.getPlaylistName());

    m_model->SetFavorites(favKeys);
  }
    return;
  // Обрабатываем клавиши навигации: пропускаем базовую обработку, затем
  // обновляем лого
  case WXK_UP:
  case WXK_DOWN:
  case WXK_PAGEUP:
  case WXK_PAGEDOWN:
  case WXK_HOME:
  case WXK_END:
    evt.Skip(); // пусть wxDataViewCtrl сделает своё дело (сдвинет
                // выделение/видимую область)
    // После обработки события вызываем HandleVisibleRangeChange
    CallAfterSafeById(GetId(), [this]() {
      if (!m_closing.load())
        HandleVisibleRangeChange();
    });
    return;
  default:
    evt.Skip();
    return;
  }
}
