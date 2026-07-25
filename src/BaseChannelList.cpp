// BaseChannelList.cpp
#include "BaseChannelList.h"
#include "ChannelDataModel.h"
#include "ChannelListLogoRenderer.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"

#include <wx/app.h>
#include <wx/dc.h>
#include <wx/frame.h>
#include <wx/log.h>
#include <wx/statusbr.h>
#include <wx/timer.h>

#include "star_filled_png.h"
#include "star_outline_png.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <inttypes.h>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Configurable constants
static constexpr uint64_t kPendingStalledMs = 1500;
static constexpr int kPendingWatchdogIntervalMs = 700;
static constexpr uint64_t kLargeJumpMinThreshold = 200;

// ---------------------------------------------------------------------------
// Logging control
static std::atomic<bool> s_verboseBaseList{false};
static std::atomic<uint32_t> s_baseLogCounter{0};
static constexpr uint32_t SAMPLE_N_BASE = 500;

static inline bool ShouldLogBase() {
  if (s_verboseBaseList.load(std::memory_order_relaxed))
    return true;
  uint32_t c = s_baseLogCounter.fetch_add(1, std::memory_order_relaxed);
  return (c % SAMPLE_N_BASE) == 0;
}

// ---------------------------------------------------------------------------
// Helper: current monotonic ms
static inline uint64_t NowMs() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// ---------------------------------------------------------------------------
// Inline favorite star renderer
class InlineFavoriteStarRenderer : public wxDataViewCustomRenderer {
public:
  InlineFavoriteStarRenderer(wxBitmap filled, wxBitmap outline,
                             std::function<void(unsigned int)> onToggle)
      : wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_ACTIVATABLE,
                                 wxALIGN_CENTER),
        m_starOn(filled), m_starOff(outline), m_onToggle(onToggle) {}

  bool SetValue(const wxVariant &value) override {
    m_value = value.GetString();
    return true;
  }

  bool GetValue(wxVariant &value) const override {
    value = m_value;
    return true;
  }

  wxSize GetSize() const override { return wxSize(24, 24); }

  bool Render(wxRect rect, wxDC *dc, int) override {
    const wxBitmap &src = (m_value == "1") ? m_starOn : m_starOff;
    if (!src.IsOk())
      return false;

    int cellW = rect.width;
    int cellH = rect.height;
    int bw = src.GetWidth();
    int bh = src.GetHeight();

    double scaleX = (double)cellW / bw;
    double scaleY = (double)cellH / bh;
    double scale = std::min(scaleX, scaleY);

    int drawW = std::max(1, (int)(bw * scale));
    int drawH = std::max(1, (int)(bh * scale));

    int x = rect.x + (cellW - drawW) / 2;
    int y = rect.y + (cellH - drawH) / 2;

    wxBitmap scaled = wxBitmap(
        src.ConvertToImage().Scale(drawW, drawH, wxIMAGE_QUALITY_HIGH));
    dc->DrawBitmap(scaled, x, y, true);
    return true;
  }

  bool ActivateCell(const wxRect &, wxDataViewModel *model,
                    const wxDataViewItem &item, unsigned int,
                    const wxMouseEvent *) override {
    auto *myModel = static_cast<ChannelDataModel *>(model);
    unsigned int row = myModel->GetRow(item);
    if (m_onToggle)
      m_onToggle(row);
    return true;
  }

private:
  wxBitmap m_starOn;
  wxBitmap m_starOff;
  wxString m_value;
  std::function<void(unsigned int)> m_onToggle;
};

// ---------------------------------------------------------------------------
// Helper: extract host from URL
static std::string ExtractHostSimple(const std::string &url) {
  size_t pos = url.find("://");
  size_t start = (pos == std::string::npos) ? 0 : pos + 3;
  size_t end = url.find_first_of("/:?", start);
  if (end == std::string::npos)
    end = url.size();
  return url.substr(start, end - start);
}

// ---------------------------------------------------------------------------
// Event table
wxBEGIN_EVENT_TABLE(BaseChannelList, wxDataViewCtrl)
    EVT_DATAVIEW_ITEM_ACTIVATED(wxID_ANY, BaseChannelList::OnItemActivated)
        EVT_DATAVIEW_COLUMN_SORTED(wxID_ANY, BaseChannelList::OnColumnSorted)
            EVT_WINDOW_DESTROY(BaseChannelList::OnDestroy) wxEND_EVENT_TABLE()

    // ---------------------------------------------------------------------------
    // Constructor
    // ---------------------------------------------------------------------------
    BaseChannelList::BaseChannelList(wxWindow *parent, wxWindowID id)
    : wxDataViewCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                     wxDV_ROW_LINES | wxDV_VERT_RULES | wxDV_MULTIPLE),
      m_closing(false), m_model(new ChannelDataModel()), m_lazyLoadTimer(this),
      m_pendingWatchdogTimer(this) {

  AssociateModel(m_model);
  m_model->DecRef();

  // Model → view enqueue callback
  m_model->SetEnqueueCallback([this](unsigned int row, bool highPriority) {
    const int id = this->GetId();
    CallAfterSafeById(id, [id, row, highPriority]() {
      wxWindow *w = wxWindow::FindWindowById(id);
      if (!w)
        return;
      auto *self = dynamic_cast<BaseChannelList *>(w);
      if (!self || self->m_closing.load())
        return;
      self->EnqueueRowLoad(static_cast<size_t>(row), highPriority);
    });
  });

  m_favIconFilled =
      wxBitmap::NewFromPNGData(star_filled_png, star_filled_png_len);
  m_favIconOutline =
      wxBitmap::NewFromPNGData(star_outline_png, star_outline_png_len);

  InitColumns();

  m_lazyLoadTimer.Bind(wxEVT_TIMER, &BaseChannelList::OnLazyLoad, this,
                       m_lazyLoadTimer.GetId());

  Bind(wxEVT_DPI_CHANGED, &BaseChannelList::OnDPIChanged, this);

  m_search = std::make_unique<TypeAheadSearch>(
      this, [this]() { return (int)m_model->GetCount(); },
      [this](int i) {
        return wxString::FromUTF8(m_model->GetChannel(i).getName());
      },
      [this](int i) {
        wxDataViewItem item = m_model->GetItem(i);
        if (item.IsOk()) {
          SetCurrentItem(item);
          EnsureVisible(item);
        }
      });

  Bind(wxEVT_CHAR, [this](wxKeyEvent &e) { m_search->OnChar(e); });

  m_maxPerHost = 6;
  m_inflightPerHost.clear();

  m_dynamicPrefetch.store(kPrefetchCount);

  m_pendingWatchdogTimer.Bind(wxEVT_TIMER, &BaseChannelList::OnPendingWatchdog,
                              this);
  m_pendingWatchdogTimer.Start(kPendingWatchdogIntervalMs, wxTIMER_CONTINUOUS);

  m_epgUpdateTimer.SetOwner(this, wxID_HIGHEST + 4);
  Bind(wxEVT_TIMER, &BaseChannelList::OnEpgUpdateTimer, this,
       m_epgUpdateTimer.GetId());

  Bind(EVT_EPG_UPDATED, [this](wxCommandEvent &evt) {
    if (!m_epgUpdatePending) {
      m_epgUpdatePending = true;
      m_epgUpdateTimer.StartOnce(200); // 200 мс задержки
    }
    evt.Skip();
  });

  m_processQueueTimer.SetOwner(this, wxID_HIGHEST + 6);
  Bind(wxEVT_TIMER, &BaseChannelList::OnProcessQueueTimer, this,
       m_processQueueTimer.GetId());
}

BaseChannelList::~BaseChannelList() {
  PROFILE_SCOPE("BaseChannelList dtor");
  m_closing.store(true);
  m_lazyLoadTimer.Stop();
  m_pendingWatchdogTimer.Stop();
}
// ---------------------------------------------------------------------------
// Columns
// ---------------------------------------------------------------------------
void BaseChannelList::InitColumns() {
  AppendTextColumn("#", 0, wxDATAVIEW_CELL_INERT, 50, wxALIGN_CENTER)
      ->SetSortable(false);

  AppendColumn(new wxDataViewColumn("Logo", new ChannelListLogoRenderer(), 1,
                                    40, wxALIGN_CENTER,
                                    wxDATAVIEW_COL_RESIZABLE));

  AppendTextColumn("Name", 2, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE,
                   wxALIGN_LEFT)
      ->SetSortable(true);

  wxDataViewColumn *favCol = new wxDataViewColumn(
      "★",
      new InlineFavoriteStarRenderer(m_favIconFilled, m_favIconOutline,
                                     [this](unsigned int row) {
                                       if (row < m_model->GetCount()) {
                                         bool isFav = m_model->IsFavorite(row);
                                         const Channel &ch =
                                             m_model->GetChannel(row);
                                         OnFavoriteToggled(ch, !isFav);
                                       }
                                     }),
      3, 40, wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE);

  AppendColumn(favCol);
  favCol->SetSortable(true);

  AppendTextColumn("Group", 4, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE,
                   wxALIGN_LEFT)
      ->SetSortable(true);

  AppendTextColumn("Language", 5, wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT)
      ->SetSortable(true);

  AppendTextColumn("Country", 6, wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT)
      ->SetSortable(true);

  AppendTextColumn("Program", 7, wxDATAVIEW_CELL_INERT, 200, wxALIGN_LEFT)
      ->SetSortable(false);
}

// ---------------------------------------------------------------------------
// Helpers: visible row, restore
// ---------------------------------------------------------------------------
int BaseChannelList::GetTopVisibleRow() {
  wxDataViewItem item = GetTopItem();
  if (!item.IsOk())
    return 0;
  return m_model->GetRow(item);
}

void BaseChannelList::RestoreTopVisibleRow(int row) {
  if (row < 0 || row >= (int)m_model->GetCount())
    return;
  wxDataViewItem item = m_model->GetItem(row);
  if (item.IsOk())
    EnsureVisible(item);
}

// ---------------------------------------------------------------------------
// Favorites sync
// ---------------------------------------------------------------------------
void BaseChannelList::BeginFavoritesSync() {
  m_favSyncDepth.fetch_add(1);
  m_syncingFavorites = true;
}

void BaseChannelList::EndFavoritesSync() {
  int prev = m_favSyncDepth.fetch_sub(1);
  if (prev <= 1) {
    m_syncingFavorites = false;
    m_favSyncDepth.store(0);
  }
}

// ---------------------------------------------------------------------------
// LoadChannels
// ---------------------------------------------------------------------------
void BaseChannelList::LoadChannels(const std::vector<Channel> &channels,
                                   const std::string &playlistName) {
  PROFILE_SCOPE("BaseChannelList::LoadChannels");

  if (m_closing.load())
    return;

  m_playlistName = playlistName;
  int logoSize = GetDpiLogoSizeList(this);
  if (logoSize <= 0)
    logoSize = 40;
  int dpi = GetNormDPI(this);

  const size_t initialCount = std::min<size_t>(50, channels.size());
  std::vector<Channel> initialBatch(channels.begin(),
                                    channels.begin() + initialCount);

  for (auto &ch : initialBatch)
    if (ch.getPlaylistName().empty())
      ch.setPlaylistName(playlistName);

  m_model->SetChannels(initialBatch, playlistName, logoSize, dpi);

  ResetVisibleRange();

  Refresh();
  Update();

  size_t top = (size_t)GetTopVisibleRow();
  size_t visible = (size_t)GetCountPerPage();
  if (visible == 0) {
    int rowH = 40;
    visible = std::max<size_t>(1, (size_t)(GetClientSize().GetHeight() / rowH));
  }

  size_t modelCount = m_model->GetCount();

  size_t prefetchLimit = std::min(m_dynamicPrefetch.load(), modelCount);
  prefetchLimit = std::min(prefetchLimit, m_maxTotalPending / 4 + (size_t)50);

  size_t visibleEnd = std::min(modelCount, top + visible);
  size_t prefetchEnd = std::min(modelCount, visibleEnd + prefetchLimit);

  for (size_t i = top; i < visibleEnd; ++i)
    m_model->RequestLogoLoadIfMissing((unsigned int)i, true);

  for (size_t i = visibleEnd; i < prefetchEnd; ++i)
    m_model->RequestLogoLoadIfMissing((unsigned int)i, false);

  if (!channels.empty()) {
    const int id = GetId();
    CallAfterSafeById(id, [id]() {
      wxWindow *w = wxWindow::FindWindowById(id);
      if (!w)
        return;
      auto *self = dynamic_cast<BaseChannelList *>(w);
      if (!self || self->m_closing.load())
        return;
      self->DoLazyLoad();
    });
  }
}

void BaseChannelList::LoadFavoritesChannels(
    const std::vector<Channel> &channels, const std::string &playlistName) {
  int logoSize = GetDpiLogoSizeList(this);
  if (logoSize <= 0)
    logoSize = 40;

  int dpi = GetNormDPI(this);
  
  m_model->SetChannels(channels, playlistName, logoSize, dpi);
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------
void BaseChannelList::OnDestroy(wxWindowDestroyEvent &) {
  m_closing.store(true);
  m_lazyLoadTimer.Stop();
  m_pendingWatchdogTimer.Stop();
}

void BaseChannelList::OnItemActivated(wxDataViewEvent &evt) {
  if (m_closing.load())
    return;

  unsigned int row = m_model->GetRow(evt.GetItem());
  int col = evt.GetColumn();
  if (row >= m_model->GetCount())
    return;

  const Channel &ch = m_model->GetChannel(row);
  OnChannelActivated(ch, col);
}

void BaseChannelList::OnColumnSorted(wxDataViewEvent &evt) {
  if (m_closing.load())
    return;

  int col = evt.GetColumn();
  wxDataViewColumn *colPtr = GetColumn(col);
  if (!colPtr)
    return;

  bool asc = colPtr->IsSortOrderAscending();
  m_model->SetSorting(col, asc);
}

void BaseChannelList::OnLazyLoad(wxTimerEvent &evt) {
  (void)evt;
  DoLazyLoad();
}

void BaseChannelList::OnShowEvent(wxShowEvent &evt) {
  if (evt.IsShown()) {
    const int id = GetId();
    CallAfterSafeById(id, [id]() {
      wxWindow *w = wxWindow::FindWindowById(id);
      if (!w)
        return;
      auto *self = dynamic_cast<BaseChannelList *>(w);
      if (!self || self->m_closing.load())
        return;
      self->DoLazyLoad();
    });
  }
  evt.Skip();
}

// ---------------------------------------------------------------------------
// OnInternalIdle — главный механизм отслеживания скролла
// ---------------------------------------------------------------------------
void BaseChannelList::OnInternalIdle() {
  wxDataViewCtrl::OnInternalIdle();

  if (m_closing.load())
    return;

  wxDataViewItem topItem = GetTopItem();
  size_t newTop = 0;

  if (topItem.IsOk())
    newTop = m_model->GetRow(topItem);

  if (newTop != m_lastTopRow) {
    HandleVisibleRangeChange();
    m_lastTopRow = newTop;
  }
}

// ---------------------------------------------------------------------------
// DoLazyLoad
// ---------------------------------------------------------------------------
void BaseChannelList::DoLazyLoad() {
  PROFILE_SCOPE("BaseChannelList::DoLazyLoad");

  if (ShouldLogBase()) {
    LOG_DEBUG("DoLazyLoad called this=%p modelCount=%u", this,
              m_model ? m_model->GetCount() : 0);
  }

  if (m_closing.load())
    return;

  if (!IsShownOnScreen())
    return;

  if (m_model->GetCount() == 0)
    return;

  if (wxTheApp && wxTheApp->GetTopWindow()) {
    if (auto *mf = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow())) {
      if (mf->m_listState == ChannelsViewState::Uninitialized ||
          mf->m_listState == ChannelsViewState::Paused)
        return;
      if (!mf->AreLogosEnabled()) {
        m_pendingLogoLoads.clear();
        return;
      }
      if (mf->m_listState != ChannelsViewState::Ready)
        return;
    }
  }

  HandleVisibleRangeChange();

  int inflight = m_inflightLoads.load();
  size_t totalPending = m_pendingLogoLoads.size() + m_loadQueue.size();
  double cpuLoad = GetSystemCPULoadPercent();

  if ((cpuLoad >= 0.0 && cpuLoad < 30.0) &&
      inflight < (m_maxConcurrentLoads / 2) &&
      totalPending < m_maxTotalPending / 4) {

    size_t cur = m_dynamicPrefetch.load();
    size_t target = std::min<size_t>(kPrefetchCount * 4, cur + 100);
    m_dynamicPrefetch.store(target);

    if (ShouldLogBase()) {
      LOG_DEBUG("DoLazyLoad adaptive prefetch increased to %zu (cpu=%.1f "
                "inflight=%d pending=%zu)",
                target, cpuLoad, inflight, totalPending);
    }
  } else {
    m_dynamicPrefetch.store(kPrefetchCount);
  }
}
// ---------------------------------------------------------------------------
// HandleVisibleRangeChange
// ---------------------------------------------------------------------------
void BaseChannelList::HandleVisibleRangeChange() {
  PROFILE_SCOPE("BaseChannelList::HandleVisibleRangeChange");

  if (m_closing.load())
    return;

  size_t total = (size_t)m_model->GetCount();
  if (total == 0)
    return;

  size_t newTop = (size_t)GetAccurateTopRow();
  if (newTop >= total)
    newTop = (total > 0) ? (total - 1) : 0;

  size_t visibleCount = (size_t)GetCountPerPage();
  if (visibleCount == 0) {
    int rowH = GetDpiLogoSizeList(this);
    if (rowH <= 0)
      rowH = 40;
    int h = GetClientSize().GetHeight();
    if (h > 0)
      visibleCount = std::max<size_t>(1, (size_t)(h / rowH));
    else
      visibleCount = 50;
  }

  size_t rangeAbove = visibleCount;
  size_t rangeBelow = visibleCount;

  size_t start = (newTop > rangeAbove) ? newTop - rangeAbove : 0;
  size_t end = std::min(total, newTop + visibleCount + rangeBelow);

  size_t jumpDist = (newTop > m_lastTopRow) ? (newTop - m_lastTopRow)
                                            : (m_lastTopRow - newTop);

  if (jumpDist >= kLargeJumpMinThreshold) {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    m_loadQueue.clear();
    m_queuedKeys.clear();
    m_lruQueuedKeys.clear();
    m_lruIter.clear();
    m_pendingLogoLoads.clear();
  }

  for (size_t row = start; row < end; ++row) {
    bool highPriority = (row >= newTop && row < newTop + visibleCount);
    m_model->RequestLogoLoadIfMissing((unsigned int)row, highPriority);
  }

  m_lastTopRow = newTop;
  m_lastVisibleCount = visibleCount;
}

// ---------------------------------------------------------------------------
// EnqueueRowLoad
// ---------------------------------------------------------------------------
void BaseChannelList::EnqueueRowLoad(size_t row, bool highPriority) {
  if (m_closing.load())
    return;

  if (row >= (size_t)m_model->GetCount())
    return;

  if (m_queuePaused.load() || m_loadingPaused.load())
    return;

  const Channel &ch = m_model->GetChannel((unsigned int)row);
  const std::string url = ch.getLogo();
  if (url.empty())
    return;

  int logoSize = GetDpiLogoSizeList(this);
  if (logoSize <= 0)
    logoSize = 40;
  int dpi = GetNormDPI(this);

  std::string key =
      m_model->MakeCacheKey(ch.getPlaylistName(), ch.getName(), logoSize, dpi);
  uint64_t now = NowMs();

  {
    std::lock_guard<std::mutex> lk(m_queueMutex);

    if (m_pendingLogoLoads.count(key))
      return;

    if (m_queuedKeys.count(key))
      return;

    if (LogoCache::GetCachedBitmapPtr(key)) {
      GetModel()->UpdateRowByName(ch.getPlaylistName(), ch.getName());
      return;
    }

    size_t totalPending = m_pendingLogoLoads.size() + m_loadQueue.size();
    if (totalPending >= m_maxTotalPending) {
      if (!highPriority)
        return;

      if (!m_loadQueue.empty()) {
        const QueueItem &back = m_loadQueue.back();
        const Channel &qc = m_model->GetChannel((unsigned int)back.row);
        std::string qkey = m_model->MakeCacheKey(qc.getPlaylistName(),
                                                 qc.getName(), logoSize, dpi);
        m_queuedKeys.erase(qkey);
        m_loadQueue.pop_back();
      }
    }

    uint64_t modelVer = m_model ? m_model->GetModelVersion() : 0;
    int priority = highPriority ? 0 : 1;

    QueueItem qi{row, priority, modelVer, now};
    if (priority == 0)
      m_loadQueue.push_front(qi);
    else
      m_loadQueue.push_back(qi);

    m_queuedKeys.insert(key);
    m_lruQueuedKeys.push_back(key);
    m_lruIter[key] = std::prev(m_lruQueuedKeys.end());

    AddKeyMapping(key, row);
  }

  ScheduleProcessLoadQueue();
}

// ---------------------------------------------------------------------------
// processLoadQueue
// ---------------------------------------------------------------------------
void BaseChannelList::processLoadQueue() {
  PROFILE_SCOPE("BaseChannelList::processLoadQueue");

  if (m_closing.load())
    return;
  if (m_queuePaused.load() || m_loadingPaused.load())
    return;

  int logoSize = GetDpiLogoSizeList(this);
  if (logoSize <= 0)
    logoSize = 40;
  int dpi = GetNormDPI(this);

  bool expected = false;
  if (!m_processing.compare_exchange_strong(expected, true))
    return;

  const size_t kMaxStartsPerPass = 5;

  int inflight = m_inflightLoads.load();
  int canStart = std::max(0, m_maxConcurrentLoads - inflight);
  size_t startsAllowed = std::min<size_t>((size_t)canStart, kMaxStartsPerPass);

  if (startsAllowed == 0) {
    m_processing.store(false);
    return;
  }

  std::vector<QueueItem> toStart;
  toStart.reserve(startsAllowed);

  {
    std::lock_guard<std::mutex> lk(m_queueMutex);

    if (m_loadQueue.empty()) {
      m_processing.store(false);
      return;
    }

    for (auto it = m_loadQueue.begin();
         it != m_loadQueue.end() && toStart.size() < startsAllowed;) {

      const QueueItem &qi = *it;

      if (qi.row >= (size_t)m_model->GetCount()) {
        it = m_loadQueue.erase(it);
        continue;
      }

      const Channel &c = m_model->GetChannel((unsigned int)qi.row);
      const std::string url = c.getLogo();
      if (url.empty()) {
        it = m_loadQueue.erase(it);
        continue;
      }

      std::string key = m_model->MakeCacheKey(c.getPlaylistName(), c.getName(),
                                              logoSize, dpi);

      if (m_pendingLogoLoads.count(key)) {
        ++it;
        continue;
      }

      toStart.push_back(qi);

      m_queuedKeys.erase(key);

      auto itLru = m_lruIter.find(key);
      if (itLru != m_lruIter.end()) {
        m_lruQueuedKeys.erase(itLru->second);
        m_lruIter.erase(itLru);
      }

      it = m_loadQueue.erase(it);
    }
  }

  for (const auto &qi : toStart) {
    if (m_closing.load())
      break;

    size_t row = qi.row;
    if (row >= (size_t)m_model->GetCount())
      continue;

    Channel ch = m_model->GetChannel((unsigned int)row);
    std::string url = ch.getLogo();
    if (url.empty())
      continue;

    int logoSize = GetDpiLogoSizeList(this);
    if (logoSize <= 0)
      logoSize = 40;
    int dpi = GetNormDPI(this);

    std::string key = m_model->MakeCacheKey(ch.getPlaylistName(), ch.getName(),
                                            logoSize, dpi);

    {
      std::lock_guard<std::mutex> lk(m_queueMutex);

      if (m_pendingLogoLoads.count(key))
        continue;

      m_pendingLogoLoads[key] = NowMs();

      std::string host = ExtractHostSimple(url);
      m_inflightPerHost[host] = m_inflightPerHost[host] + 1;

      m_inflightLoads.fetch_add(1);
      m_diag_started.fetch_add(1);
    }

    const uint64_t expectedModelVer = qi.modelVer;
    const int localWinId = GetId();
    const std::string pl = ch.getPlaylistName();
    const std::string nm = ch.getName();
    const std::string urlCopy = url;
    const std::string keyCopy = key;
    size_t rowCopy = row;

    LogoCache::GetLogoAsync(
        pl, nm, urlCopy, logoSize, logoSize, dpi,
        [localWinId, keyCopy, rowCopy, pl, nm, urlCopy,
         expectedModelVer](LogoCache::LogoBitmapPtr bmpPtr) {
          auto bmp_copy = bmpPtr;

          CallAfterSafeById(localWinId, [keyCopy, rowCopy, pl, nm, urlCopy,
                                         expectedModelVer,
                                         bmp_copy](wxWindow *w) {
            auto *list = dynamic_cast<BaseChannelList *>(w);
            if (!list)
              return;

            {
              std::lock_guard<std::mutex> lk(list->m_queueMutex);

              list->m_pendingLogoLoads.erase(keyCopy);

              std::string host = ExtractHostSimple(urlCopy);
              auto hit = list->m_inflightPerHost.find(host);
              if (hit != list->m_inflightPerHost.end()) {
                hit->second = std::max(0, hit->second - 1);
                if (hit->second == 0)
                  list->m_inflightPerHost.erase(hit);
              }
            }

            list->m_inflightLoads.fetch_sub(1);

            if (list->m_closing.load())
              return;

            if (!bmp_copy || !bmp_copy->IsOk()) {
              {
                std::lock_guard<std::mutex> lk(list->m_queueMutex);
                // Re-mark key как queued для повтора
                if (list->m_queuedKeys.find(keyCopy) ==
                    list->m_queuedKeys.end()) {
                  list->m_queuedKeys.insert(keyCopy);
                  QueueItem qi{rowCopy, 1, expectedModelVer, NowMs()};
                  list->m_loadQueue.push_back(qi);
                  list->m_lruQueuedKeys.push_back(keyCopy);
                  list->m_lruIter[keyCopy] =
                      std::prev(list->m_lruQueuedKeys.end());
                }
              }

              list->ScheduleProcessLoadQueue();
              return;
            }

            if (auto *model = list->GetModel()) {
              model->UpdateRowByIndex((unsigned int)rowCopy);
              model->SafeUpdateRowByName(pl, nm, bmp_copy, expectedModelVer);
            }

            list->ScheduleProcessLoadQueue();
          });
        });
  }

  m_processing.store(false);
}

// ---------------------------------------------------------------------------
// OnPendingWatchdog
// ---------------------------------------------------------------------------
void BaseChannelList::OnPendingWatchdog(wxTimerEvent &evt) {
  (void)evt;

  if (m_closing.load())
    return;

  uint64_t now = NowMs();
  std::vector<std::pair<std::string, size_t>> toRetry; // key, row pairs

  {
    std::lock_guard<std::mutex> lk(m_queueMutex);

    for (auto &kv : m_pendingLogoLoads) {
      uint64_t age = now - kv.second;
      if (age >= kPendingStalledMs) {
        // NEW: Получай row из маппинга O(1) вместо O(n)
        {
          std::lock_guard<std::mutex> lk2(m_keyToRowMutex);
          auto it = m_keyToRow.find(kv.first);
          if (it != m_keyToRow.end()) {
            toRetry.push_back({kv.first, it->second});
          }
        }
      }
    }

    for (const auto &[key, row] : toRetry) {
      m_pendingLogoLoads.erase(key);
      RemoveKeyMapping(key); // NEW: Очисти маппинг
    }
  }

  // NEW: Простой и прямой retry
  for (const auto &[key, row] : toRetry) {
    const int id = GetId();
    CallAfterSafeById(id, [id, row]() {
      wxWindow *w = wxWindow::FindWindowById(id);
      if (!w)
        return;
      auto *self = dynamic_cast<BaseChannelList *>(w);
      if (!self || self->m_closing.load())
        return;
      if (row < self->m_model->GetCount()) {
        self->EnqueueRowLoad(row, true);
      }
    });
  }
}

// ---------------------------------------------------------------------------
// ExtractHost
// ---------------------------------------------------------------------------
std::string BaseChannelList::ExtractHost(const std::string &url) {
  return ExtractHostSimple(url);
}

void BaseChannelList::PauseLogoLoading() {
  PROFILE_SCOPE("BaseChannelList::PauseLogoLoading");
  if (m_closing.load())
    return;

  if (m_lazyLoadTimer.IsRunning())
    m_lazyLoadTimer.Stop();

  m_queuePaused.store(true);
  m_loadingPaused.store(true);

  {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    m_loadQueue.clear();
    m_inflightPerHost.clear();
    m_queuedKeys.clear();
    m_lruQueuedKeys.clear();
    m_lruIter.clear();
    m_pendingLogoLoads.clear(); // NEW
  }

  {
    std::lock_guard<std::mutex> lk(m_keyToRowMutex);
    m_keyToRow.clear();
  }
}

void BaseChannelList::ResumeLogoLoading() {
  PROFILE_SCOPE("BaseChannelList::ResumeLogoLoading");
  LOG_DEBUG("BaseChannelList::ResumeLogoLoading start");
  if (m_closing.load())
    return;

  // Respect MainFrame state
  if (wxTheApp && wxTheApp->GetTopWindow()) {
    if (auto *mf = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow())) {
      if (!mf->AreLogosEnabled())
        return;
      if (mf->m_listState != ChannelsViewState::Ready)
        return;
    }
  }

  // ------------------------------------------------------------
  // Performance mode auto-selection (RAM + CPU + playlist size)
  // ------------------------------------------------------------
  {
    size_t availMB = GetAvailableRAM_MB();
    unsigned int cores =
        std::max<unsigned int>(1, std::thread::hardware_concurrency());
    size_t modelCount = m_model ? m_model->GetCount() : 0;

    PerformanceTuning t = GetPerformanceTuning(availMB, cores, modelCount);
    m_perfMode = DetectPerformanceMode(availMB, cores, modelCount);

    // Apply tuning
    m_maxConcurrentLoads = std::max(2, t.maxConcurrentLoads);
    m_maxTotalPending = std::clamp<size_t>(t.maxTotalPending, 100, 50000);

    // Base prefetch for adaptive logic
    m_dynamicPrefetch.store(
        std::clamp<size_t>(t.basePrefetch, (size_t)100, (size_t)50000));

    // Apply LRU limits to LogoCache
    LogoCache::SetCacheLimits(t.lru.rowLRU, t.lru.tileLRU);

    if (ShouldLogBase()) {
      LOG_DEBUG("PerfMode=%d availMB=%zu cores=%u modelCount=%zu "
                "maxConcurrent=%d maxPending=%zu basePrefetch=%zu "
                "lru_rowLRU=%d lru_tileLRU=%d",
                (int)m_perfMode, availMB, cores, modelCount,
                m_maxConcurrentLoads, m_maxTotalPending, t.basePrefetch,
                t.lru.rowLRU, t.lru.tileLRU);
    }
  }

  // ------------------------------------------------------------
  // Per-host throttling (unchanged)
  // ------------------------------------------------------------
  m_maxPerHost = std::clamp<int>(
      std::max<int>(2, (int)std::min<unsigned int>(m_maxConcurrentLoads, 8u)),
      2, 24);

  // ------------------------------------------------------------
  // Resume loading
  // ------------------------------------------------------------
  m_loadingPaused.store(false);
  m_queuePaused.store(false);

  if (m_model && m_model->GetCount() > 0) {
    const int id = GetId();
    CallAfterSafeById(id, [id]() {
      LOG_DEBUG("BaseChannelList::ResumeLogoLoading CallAfterSafeById run DoLazyLoad");
      wxWindow *w = wxWindow::FindWindowById(id);
      if (!w)
        return;
      auto *self = dynamic_cast<BaseChannelList *>(w);
      if (!self || self->m_closing.load())
        return;
      self->DoLazyLoad();
    });
  }
  LOG_DEBUG("BaseChannelList::ResumeLogoLoading end");
}

void BaseChannelList::AddPendingKeys(const std::vector<std::string> &keys) {
  if (m_closing.load())
    return;
  uint64_t now = NowMs();
  std::lock_guard<std::mutex> lk(m_queueMutex);
  for (const auto &k : keys)
    m_pendingLogoLoads[k] = now;
}

void BaseChannelList::OnDPIChanged(wxDPIChangedEvent &evt) {
  PROFILE_SCOPE("BaseChannelList::OnDPIChanged");

  int newDpi = GetNormDPI(this);
  PauseLogoLoading();
  LogoCache::OnDPIChanged(newDpi);

  if (m_model) {
    m_model->CheckDpiReset();
    m_model->Reset(m_model->GetCount());
  }

  int localWinId = GetId();
  CallAfterSafeById(localWinId, [](wxWindow *w) {
    auto *self = dynamic_cast<BaseChannelList *>(w);
    if (!self)
      return;
    if (self->m_closing.load())
      return;
    self->ResumeLogoLoading();
    self->Refresh();
  });

  evt.Skip();
}

void BaseChannelList::ResetVisibleRange() {
  std::lock_guard<std::mutex> lk(m_queueMutex);
  m_lastTopRow = 0;
  m_lastVisibleCount = 0;
  ++m_lazySeq;

  m_loadQueue.clear();
  m_inflightPerHost.clear();
  m_queuedKeys.clear();
  m_lruQueuedKeys.clear();
  m_lruIter.clear();
  m_pendingLogoLoads.clear();

  {
    std::lock_guard<std::mutex> lk2(m_keyToRowMutex);
    m_keyToRow.clear();
  }
}

void BaseChannelList::CoalescedDoLazyLoadSchedule() {
  const int id = GetId();
  if (m_appendCoalesced.exchange(true))
    return;

  CallAfterSafeById(id, [id]() {
    wxWindow *w = wxWindow::FindWindowById(id);
    if (!w)
      return;
    auto *self = dynamic_cast<BaseChannelList *>(w);
    if (!self)
      return;
    self->m_appendCoalesced.store(false);
    self->DoLazyLoad();
  });
}

int BaseChannelList::GetCountPerPage() const {
  int rowH = 40;
  int h = GetClientSize().GetHeight();
  if (h <= 0)
    return 0;
  return std::max(1, h / rowH);
}

bool BaseChannelList::IsShownOnScreen() const {
#if wxCHECK_VERSION(3, 1, 0)
  return wxWindow::IsShownOnScreen();
#else
  if (!IsShown())
    return false;
  wxWindow *top = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
  if (!top)
    return true;
  return top->IsShown();
#endif
}

int BaseChannelList::GetAccurateTopRow() {
  wxDataViewItem topItem = GetTopItem();

  if (!HasScrollbar(wxVERTICAL)) {
    if (topItem.IsOk())
      return m_model->GetRow(topItem);
    return 0;
  }

  int rowH = GetDpiLogoSizeList(this);
  if (rowH <= 0)
    rowH = 40;

  int scrollPos = 0;
  if (GetScrollThumb(wxVERTICAL) > 0) {
    scrollPos = GetScrollPos(wxVERTICAL);
    if (scrollPos < 0)
      scrollPos = 0;
  }

  int fromScroll = (rowH > 0) ? (scrollPos / rowH) : 0;

  if (topItem.IsOk()) {
    int fromTop = m_model->GetRow(topItem);
    return std::max(0, std::min(fromScroll, fromTop));
  }

  return std::max(0, fromScroll);
}

void BaseChannelList::RefreshProgramColumn() {
  static int count = 0;
  LOG_DEBUG("RefreshProgramColumn called %d", ++count);
  
  if (m_closing.load() || !m_model)
    return;
  // Если список не отображается, обновляем все (или ничего)
  if (!IsShownOnScreen())
    return;
  RefreshProgramColumnVisible();
}

void BaseChannelList::RefreshProgramColumnVisible() {
  if (m_closing.load() || !m_model)
    return;
  int top = GetTopVisibleRow();
  int visible = GetCountPerPage();
  if (top < 0 || visible <= 0)
    return;
  int end = std::min(top + visible, (int)m_model->GetCount());
  for (int row = top; row < end; ++row) {
    m_model->RowChanged(row);
  }
}

void BaseChannelList::OnEpgUpdateTimer(wxTimerEvent &) {
  m_epgUpdatePending = false;
  RefreshProgramColumn();
}

void BaseChannelList::OnProcessQueueTimer(wxTimerEvent &) {
  // Проверяем, есть ли работа (на случай, если очередь опустела за время задержки)
  if (m_loadQueue.empty() && m_pendingLogoLoads.empty()) {
    return;
  }
  processLoadQueue();
}
