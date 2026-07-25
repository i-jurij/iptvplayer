#include "CardsBase.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"

#include <map>

int CardsBase::MAX_TILE_LRU = 300;

void CardsBase::InitLRULimits() {
  int clientH = GetClientSize().GetHeight();
  if (clientH <= 0 || m_rowH <= 0 || m_cols <= 0)
    return;

  int visibleRows = clientH / m_rowH;
  int visibleCols = m_cols;
  int visibleTiles = visibleRows * visibleCols;

  int bufferTiles = visibleCols * 4;
  int prefetchTiles = visibleCols * 6;
  int favTiles = 50;

  int dpiLayers = std::max(1, (int)m_tileCacheDPI.size());

  size_t ramMB = GetAvailableRAM_MB();
  double ramFactor = std::clamp(ramMB / 8000.0, 0.5, 4.0);

  int base = visibleTiles + bufferTiles + prefetchTiles + favTiles;
  int scaled = (int)(base * dpiLayers * ramFactor);

  MAX_TILE_LRU = std::clamp(scaled, 300, 4000);

  //LOG_DEBUG("Adaptive LRU: visible=%d buffer=%d prefetch=%d dpi=%d ram=%.1f → "
    //        "MAX_TILE_LRU=%d",
      //      visibleTiles, bufferTiles, prefetchTiles, dpiLayers, ramFactor,
        //    MAX_TILE_LRU);
}

wxBEGIN_EVENT_TABLE(CardsBase, wxScrolledWindow) EVT_PAINT(CardsBase::OnPaint)
    EVT_SIZE(CardsBase::OnResize) EVT_MOTION(CardsBase::OnMouseMove)
        EVT_LEAVE_WINDOW(CardsBase::OnMouseLeave)
            EVT_LEFT_DOWN(CardsBase::OnMouseDown)
                EVT_WINDOW_DESTROY(CardsBase::OnDestroy)
                    EVT_KEY_DOWN(CardsBase::OnKeyDown)
                        EVT_CONTEXT_MENU(CardsBase::OnContextMenu)
    wxEND_EVENT_TABLE()

                                CardsBase::CardsBase(wxWindow *parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxFULL_REPAINT_ON_RESIZE | wxVSCROLL),
      m_redrawTimer(this, wxID_HIGHEST + 1),
      m_logoTimer(this, wxID_HIGHEST + 2) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetDoubleBuffered(true);
  SetScrollRate(0, 20);
  Bind(wxEVT_TIMER, &CardsBase::OnRedrawTimer, this, m_redrawTimer.GetId());
  Bind(wxEVT_TIMER, &CardsBase::ProcessLogoQueue, this, m_logoTimer.GetId());
  Bind(wxEVT_DPI_CHANGED, &CardsBase::OnDPIChanged, this);
  SetFocusIgnoringChildren();
  SetWindowStyleFlag(GetWindowStyleFlag() | wxWANTS_CHARS);
  m_currentDPI = GetCurrentDPI();

#if defined(wxEVT_SCROLLWIN_LINEUP)
  Bind(wxEVT_SCROLLWIN_LINEUP, &CardsBase::OnScroll, this);
#endif
#if defined(wxEVT_SCROLLWIN_LINEDOWN)
  Bind(wxEVT_SCROLLWIN_LINEDOWN, &CardsBase::OnScroll, this);
#endif
#if defined(wxEVT_SCROLLWIN_PAGEUP)
  Bind(wxEVT_SCROLLWIN_PAGEUP, &CardsBase::OnScroll, this);
#endif
#if defined(wxEVT_SCROLLWIN_PAGEDOWN)
  Bind(wxEVT_SCROLLWIN_PAGEDOWN, &CardsBase::OnScroll, this);
#endif
#if defined(wxEVT_SCROLLWIN_THUMBTRACK)
  Bind(wxEVT_SCROLLWIN_THUMBTRACK, &CardsBase::OnScroll, this);
#endif
#if defined(wxEVT_SCROLLWIN_THUMBRELEASE)
  Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &CardsBase::OnScroll, this);
#endif

#if defined(wxEVT_SCROLLBAR)
  Bind(wxEVT_SCROLLBAR, &CardsBase::OnScroll, this);
#endif

  Bind(wxEVT_MOUSEWHEEL, &CardsBase::OnMouseWheel, this);

  int localWinId = this->GetId();
  LogoCache::RegisterScaledReadyCallback([localWinId](const std::string &sk) {
    CallAfterSafeById(localWinId, [sk](wxWindow *w) {
      auto *self = dynamic_cast<CardsBase *>(w);
      if (!self)
        return;
      if (self->m_closing)
        return;
      self->OnLogoScaledReady(sk);
    });
  });

  m_scrollStopTimer.SetOwner(this, wxID_HIGHEST + 3);
  Bind(wxEVT_TIMER, &CardsBase::OnScrollStop, this, m_scrollStopTimer.GetId());
}

MainFrame *CardsBase::GetMainFrame() const {
  auto *top = wxGetTopLevelParent(const_cast<CardsBase *>(this));
  return dynamic_cast<MainFrame *>(top);
}

void CardsBase::OnLogoScaledReady(const std::string &scaledKey) {
  auto it = m_scaledKeyToIndices.find(scaledKey);
  if (it == m_scaledKeyToIndices.end())
    return;

  std::vector<int> indices = it->second;
  for (int idx : indices) {
    if (idx < 0 || idx >= (int)m_channels.size())
      continue;

    RenderTile(idx);
    MarkCardDirty(idx);
  }
}

void CardsBase::SetChannelsBase(const std::vector<Channel> &channels) {
  PROFILE_SCOPE("CardsBase::SetChannelsBase");
  m_channelsVersion.fetch_add(1, std::memory_order_relaxed);

  m_channels = channels;

  ClearAllCaches(true, true);
  m_dirtyCards.clear();
  m_textCache.clear();
  m_textSizeCache.clear();
  m_layoutCache.clear();

  m_logoQueuePQ.clear();
  m_tileCacheDPI.clear();
  m_tileLRU.clear();
  m_tileLRUCache.clear();

  m_activeLoads = 0;

  ResumeLogoLoading();

  int winId = this->GetId();

  CallAfterSafeById(winId, [winId, this](wxWindow *w) {
    auto *self = dynamic_cast<CardsBase *>(w);
    if (!self)
      return;
    if (self->m_closing)
      return;

    self->UpdateLayout();
    self->InitLRULimits();
    self->Refresh();

    // debug ----------------------
    LogoCache::DebugMemoryUsage();
    DebugTileMemory();
    DebugInternalMemory();
    // --------------------------------------

    CallAfterSafeById(winId, [](wxWindow *w2) {
      auto *self2 = dynamic_cast<CardsBase *>(w2);
      if (!self2)
        return;
      if (self2->m_closing)
        return;

      self2->WarmUpFavorites();
      self2->WarmUpTiles();
    });
  });
}

void CardsBase::RefreshCards() {
  m_tileCacheDPI.clear();
  m_tileLRUCache.clear();
  UpdateLayout();
  InitLRULimits();
  Refresh();
}
