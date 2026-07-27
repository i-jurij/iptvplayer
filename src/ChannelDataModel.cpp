#include "ChannelDataModel.h"
#include "Application.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"
#include "epg/EPGManager.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <wx/dataview.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/settings.h>
#include <wx/wx.h>

// ---------------------------------------------------------------------------
// Configurable logging controls to avoid log flood from high-frequency calls.
// По умолчанию семплируем 1 из N сообщений; для отладки можно включить полный
// вывод, установив соответствующие флаги в true.
// ---------------------------------------------------------------------------
static std::atomic<bool> s_verboseGetValue{false};
static std::atomic<uint32_t> s_getValueCounter{0};
static constexpr uint32_t SAMPLE_N_GETVALUE = 1000;

static std::atomic<bool> s_verboseRequestEnqueue{false};
static std::atomic<uint32_t> s_requestEnqueueCounter{0};
static constexpr uint32_t SAMPLE_N_REQ = 1000;

static inline bool ShouldLogGetValue() {
  if (s_verboseGetValue.load(std::memory_order_relaxed))
    return true;
  uint32_t c = s_getValueCounter.fetch_add(1, std::memory_order_relaxed);
  return (c % SAMPLE_N_GETVALUE) == 0;
}

static inline bool ShouldLogRequestEnqueue() {
  if (s_verboseRequestEnqueue.load(std::memory_order_relaxed))
    return true;
  uint32_t c = s_requestEnqueueCounter.fetch_add(1, std::memory_order_relaxed);
  return (c % SAMPLE_N_REQ) == 0;
}

// ---------------------------------------------------------------------------

std::string ChannelDataModel::MakeCacheKey(const std::string &playlist,
                                           const std::string &channelOrUrl,
                                           int size, int dpi) const {
  std::string id =
      playlist.empty() ? channelOrUrl : playlist + "|" + channelOrUrl;

  return id + "|" + std::to_string(size) + "x" + std::to_string(size) + "|" +
         std::to_string(dpi);
}

ChannelDataModel::ChannelDataModel()
    : wxDataViewVirtualListModel(0), m_sortColumn(-1), m_sortAscending(true),
      m_disableSorting(false), m_lastDpi(0) {}

unsigned int ChannelDataModel::GetColumnCount() const { return 8; }

wxString ChannelDataModel::GetColumnType(unsigned int /*col*/) const {
  return "string";
}

void ChannelDataModel::GetValueByRow(wxVariant &variant, unsigned int row,
                                     unsigned int col) const {
  PROFILE_SCOPE("ChannelDataModel::GetValueByRow");

  const Channel &ch = m_channels[row];

  switch (col) {
    case 0:
      variant = wxString::Format("%u", row + 1);
      break;

    case 1: {
      bool logosEnabled = true;
      if (wxTheApp && wxTheApp->GetTopWindow()) {
        if (auto *mf = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow())) {
          logosEnabled = mf->AreLogosEnabled();
        }
      }

      if (!logosEnabled) {
        variant = wxString();
        break;
      }

      int size = m_logoSize > 0 ? m_logoSize : GetDpiLogoSizeList(nullptr);
      int dpi = m_lastDpi > 0 ? m_lastDpi : GetNormDPI(wxTheApp->GetTopWindow());
      if (dpi <= 0)
        dpi = 96;

      const std::string pl = ch.getPlaylistName();
      const std::string nm = ch.getName();

      std::string key = MakeCacheKey(pl, nm, size, dpi);

      // Сохраняем последний рассчитанный ключ для этой строки
      m_rowKeyCache[row] = key;

      // Попытка взять bitmap из кэша
      auto bmpPtr = LogoCache::GetCachedBitmapPtr(key);

      // 🔥 ВСЕГДА возвращаем composite-ключ
      std::string composite = key + "||" + std::to_string(row);
      variant = wxString::FromUTF8(composite);

      // Если bitmap отсутствует — инициируем загрузку
      if (!bmpPtr || !bmpPtr->IsOk()) {
        const_cast<ChannelDataModel *>(this)->RequestLogoLoadIfMissing(row, true);
      }

      break;
    }

    case 2:
      variant = wxString::FromUTF8(ch.getName());
      break;

    case 3:
      variant = m_favorites[row] ? "1" : "0";
      break;

    case 4:
      variant = wxString::FromUTF8(ch.getGroupTitle());
      break;

    case 5:
      variant = wxString::FromUTF8(ch.getLanguage());
      break;

    case 6:
      variant = wxString::FromUTF8(ch.getCountry());
      break;

    case 7: {
      // Получаем текущую программу для канала
      wxString prog;
      Application *app = static_cast<Application *>(wxTheApp);
      if (app) {
        EPGManager *epg = app->GetEPGManager();
        if (epg && epg->IsLoaded()) {
          std::string tvgId = ch.getTvgId();
          if (!tvgId.empty()) {
            EpgProgram current = epg->GetCurrentProgram(tvgId);
            if (!current.title.empty()) {
              prog = wxString::FromUTF8(current.title);
            }
          }
        }
      }
      if (prog.IsEmpty()) {
        prog = "--";
      }
      variant = prog;
      break;
    }
  }
}

bool ChannelDataModel::SetValueByRow(const wxVariant &, unsigned int,
                                     unsigned int) {
  return false;
}

// ============================================================================
// ChangeValue / DPI / Update helpers
// ============================================================================

void ChannelDataModel::ChangeValue(const wxVariant & /*variant*/,
                                   unsigned int row, unsigned int col) {
  if (col != 1)
    return;

  PROFILE_SCOPE("ChannelDataModel::ChangeValue");
  RowChanged(row);
}

void ChannelDataModel::CheckDpiReset() {
  int currentDpi = GetNormDPI(wxTheApp->GetTopWindow());
  if (currentDpi <= 0)
    currentDpi = 96;

  if (m_lastDpi == currentDpi)
    return;

  if (m_lastDpi != 0) {
    double changePercent = std::abs(currentDpi - m_lastDpi) * 100.0 / m_lastDpi;

    if (changePercent < 15.0)
      return;

    wxLogInfo("ChannelDataModel: DPI changed (%d → %d, %.1f%%) → clearing",
              m_lastDpi, currentDpi, changePercent);
  }

  m_lastDpi = currentDpi;
}

void ChannelDataModel::UpdateRowByName(const std::string &playlist,
                                       const std::string &name) {
  for (size_t row = 0; row < m_channels.size(); ++row) {
    const Channel &c = m_channels[row];
    if (c.getPlaylistName() == playlist && c.getName() == name) {
      RowChanged((unsigned)row);
      return;
    }
  }
}

void ChannelDataModel::UpdateRowByIndex(unsigned int row) {
  if (row >= m_channels.size())
    return;

  RowChanged(row);
}

void ChannelDataModel::SafeUpdateRowByName(
    const std::string &playlist, const std::string &name,
    const LogoCache::LogoBitmapPtr & /*bmp*/, uint64_t expectedModelVer) {
  if (expectedModelVer != 0) {
    uint64_t cur = m_channelsVersion.load(std::memory_order_relaxed);
    if (cur != expectedModelVer) {
      LOG_DEBUG("SafeUpdateRowByName SKIP modelVer mismatch expected=%llu "
                "cur=%llu playlist=%s name=%s",
                (unsigned long long)expectedModelVer, (unsigned long long)cur,
                playlist.c_str(), name.c_str());
      return;
    }
  }

  size_t foundIndex = SIZE_MAX;
  for (size_t i = 0; i < m_channels.size(); ++i) {
    const Channel &ch = m_channels[i];
    if (ch.getPlaylistName() == playlist && ch.getName() == name) {
      foundIndex = i;
      break;
    }
  }

  if (foundIndex == SIZE_MAX) {
    LOG_DEBUG("SafeUpdateRowByName not found playlist=%s name=%s",
              playlist.c_str(), name.c_str());
    return;
  }

  // LOG_DEBUG("SafeUpdateRowByName found playlist=%s name=%s row=%zu",
  //         playlist.c_str(), name.c_str(), foundIndex);
  RowChanged((unsigned)foundIndex);
}

void ChannelDataModel::SetChannels(const std::vector<Channel> &channels,
                                   const std::string &playlistName,
                                   int logoSize, int dpi) {
  PROFILE_SCOPE("ChannelDataModel::SetChannels");

  m_lastDpi = dpi;
  CheckDpiReset();

  m_channelsVersion.fetch_add(1, std::memory_order_relaxed);
  m_channels = channels;
  m_playlistName = playlistName;

  for (auto &ch : m_channels) {
    if (ch.getPlaylistName().empty()) {
      ch.setPlaylistName(m_playlistName);
    }
  }
  m_favorites.assign(m_channels.size(), false);
  m_logoSize = (logoSize > 0 ? logoSize : GetDpiLogoSizeList(nullptr));

  if (!m_disableSorting) {
    Resort();
  }

  m_rowKeyCache.clear();

  Reset(m_channels.size());
}

void ChannelDataModel::AppendChannels(const std::vector<Channel> &channels,
                                      const std::string &playlistName,
                                      size_t /*preloadCount*/, int dpi) {
  PROFILE_SCOPE("ChannelDataModel::AppendChannels");

  m_lastDpi = dpi;
  CheckDpiReset();

  size_t oldSize = m_channels.size();
  m_channels.insert(m_channels.end(), channels.begin(), channels.end());
  m_playlistName = playlistName;

  for (size_t i = oldSize; i < m_channels.size(); ++i) {
    if (m_channels[i].getPlaylistName().empty()) {
      m_channels[i].setPlaylistName(m_playlistName);
    }
  }

  m_favorites.resize(m_channels.size(), false);

  m_logoSize = (m_logoSize > 0 ? m_logoSize : GetDpiLogoSizeList(nullptr));

  if (!m_disableSorting) {
    Resort();
  }

  Reset(m_channels.size());
}

const Channel &ChannelDataModel::GetChannel(unsigned int row) const {
  return m_channels[row];
}

bool ChannelDataModel::IsFavorite(unsigned int row) const {
  return m_favorites[row];
}

void ChannelDataModel::SetFavoritesFromNames(
    const std::vector<std::string> &names) {
  for (size_t i = 0; i < m_channels.size(); ++i)
    m_favorites[i] = false;

  for (size_t i = 0; i < m_channels.size(); ++i)
    if (std::find(names.begin(), names.end(), m_channels[i].getName()) !=
        names.end())
      m_favorites[i] = true;

  for (unsigned int i = 0; i < m_channels.size(); ++i)
    RowChanged(i);

  if (m_sortColumn == 3) {
    Resort();
    Reset(m_channels.size());
  }
}

// ============================================================================
// RequestLogoLoadIfMissing — теперь с семплированным логированием
// ============================================================================
ChannelDataModel::RequestLogoStatus
ChannelDataModel::RequestLogoLoadIfMissing(unsigned int row,
                                           bool highPriority) {
  if (row >= m_channels.size())
    return RequestLogoStatus::Skipped;

  const Channel &ch = m_channels[row];
  const std::string url = ch.getLogo();
  if (url.empty()) {
    return RequestLogoStatus::Skipped;
  }

  if (wxTheApp && wxTheApp->GetTopWindow()) {
    if (auto *mf = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow())) {
      if (!mf->AreLogosEnabled()) {
        return RequestLogoStatus::Skipped;
      }
    }
  }

  int logoSize = m_logoSize > 0 ? m_logoSize : GetDpiLogoSizeList(nullptr);
  int dpi = m_lastDpi > 0 ? m_lastDpi : GetNormDPI(wxTheApp->GetTopWindow());
  if (dpi <= 0)
    dpi = 96;

  std::string key =
      MakeCacheKey(ch.getPlaylistName(), ch.getName(), logoSize, dpi);

  auto bmpPtr = LogoCache::GetCachedBitmapPtr(key);
  if (bmpPtr && bmpPtr->IsOk()) {
    return RequestLogoStatus::AlreadyCached;
  }

  if (m_enqueueCallback) {
    try {
      m_enqueueCallback(row, highPriority);
      return RequestLogoStatus::Queued;
    } catch (...) {
      return RequestLogoStatus::Skipped;
    }
  }

  if (ShouldLogRequestEnqueue()) {
    LOG_DEBUG("RequestLogoLoadIfMissing no_enqueue_callback row=%u key=%s", row,
              key.c_str());
  }
  return RequestLogoStatus::Skipped;
}

void ChannelDataModel::SetEnqueueCallback(
    std::function<void(unsigned int, bool)> cb) {
  m_enqueueCallback = std::move(cb);
}

// ============================================================================
// Sorting
// ============================================================================
void ChannelDataModel::SetSorting(int column, bool ascending) {
  m_sortColumn = column;
  m_sortAscending = ascending;

  if (!m_disableSorting)
    Resort();

  Reset(m_channels.size());
}

void ChannelDataModel::Resort() {
  if (m_sortColumn < 0)
    return;

  auto keyForColumn = [&](const Channel &c, bool fav,
                          int column) -> std::string {
    switch (column) {
    case 2:
      return c.getName();
    case 3:
      return fav ? "1" : "0";
    case 4:
      return c.getGroupTitle();
    case 5:
      return c.getLanguage();
    case 6:
      return c.getCountry();
    default:
      return "";
    }
  };

  auto startsWithDigit = [](const std::string &s) -> bool {
    if (s.empty())
      return false;
    unsigned char c = static_cast<unsigned char>(s[0]);
    return (c >= '0' && c <= '9');
  };

  std::vector<size_t> order(m_channels.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;

  std::stable_sort(order.begin(), order.end(), [&](size_t A, size_t B) {
    const Channel &a = m_channels[A];
    const Channel &b = m_channels[B];
    bool favA = m_favorites[A];
    bool favB = m_favorites[B];

    std::string ka = keyForColumn(a, favA, m_sortColumn);
    std::string kb = keyForColumn(b, favB, m_sortColumn);

    if (m_sortColumn == 3) {
      if (favA != favB) {
        return m_sortAscending ? (favA && !favB) : (!favA && favB);
      }
      int cmpName = wxString::FromUTF8(a.getName())
                        .CmpNoCase(wxString::FromUTF8(b.getName()));
      if (cmpName != 0)
        return m_sortAscending ? (cmpName < 0) : (cmpName > 0);
      return A < B;
    }

    bool aDigit = startsWithDigit(ka);
    bool bDigit = startsWithDigit(kb);
    if (aDigit != bDigit) {
      return m_sortAscending ? (aDigit && !bDigit) : (!aDigit && bDigit);
    }

    int cmp = wxString::FromUTF8(ka).CmpNoCase(wxString::FromUTF8(kb));
    if (cmp != 0)
      return m_sortAscending ? (cmp < 0) : (cmp > 0);

    int cmp2 = wxString::FromUTF8(a.getName())
                   .CmpNoCase(wxString::FromUTF8(b.getName()));
    if (cmp2 != 0)
      return m_sortAscending ? (cmp2 < 0) : (cmp2 > 0);

    return A < B;
  });

  std::vector<Channel> newChannels;
  std::vector<bool> newFav;
  const size_t MAX_RESERVE_ORDER = 100000;
  size_t reserveOrder = std::min(order.size(), MAX_RESERVE_ORDER);
  newChannels.reserve(reserveOrder);
  newFav.reserve(reserveOrder);

  for (size_t i = 0; i < order.size(); ++i) {
    size_t old = order[i];
    newChannels.push_back(m_channels[old]);
    newFav.push_back(m_favorites[old]);
  }

  m_channels = std::move(newChannels);
  m_favorites = std::move(newFav);
}

void ChannelDataModel::SetRowKey(unsigned int row, const std::string &key) {
  if (row >= m_channels.size())
    return;
  // сохраняем ключ; mutable map позволяет менять из const методов
  m_rowKeyCache[row] = key;
}

void ChannelDataModel::SetFavorites(
    const std::vector<std::pair<std::string, std::string>> &favs) {
  // Сбрасываем
  for (size_t i = 0; i < m_channels.size(); ++i)
    m_favorites[i] = false;

  // Устанавливаем избранные по (name + playlist)
  for (size_t i = 0; i < m_channels.size(); ++i) {
    const auto &ch = m_channels[i];
    for (const auto &f : favs) {
      if (ch.getName() == f.first && ch.getPlaylistName() == f.second) {
        m_favorites[i] = true;
        break;
      }
    }
  }

  // Обновляем строки
  for (unsigned int i = 0; i < m_channels.size(); ++i)
    RowChanged(i);

  // Пересортировка, если сортировка по избранному
  if (m_sortColumn == 3) {
    Resort();
  }
}

void ChannelDataModel::RemoveChannel(const std::string &name,
                                     const std::string &url) {
  LOG_DEBUG("ChannelDataModel::RemoveChannel: name='%s', url='%s'",
            name.c_str(), url.c_str());
  // Находим индекс удаляемого канала
  size_t index = SIZE_MAX;
  for (size_t i = 0; i < m_channels.size(); ++i) {
    if (m_channels[i].getName() == name && m_channels[i].getUrl() == url) {
      index = i;
      break;
    }
  }
  if (index == SIZE_MAX) {
    LOG_DEBUG("RemoveChannel: channel not found: %s", name.c_str());
    return;
  }

  // Удаляем из векторов
  m_channels.erase(m_channels.begin() + index);
  if (index < m_favorites.size()) {
    m_favorites.erase(m_favorites.begin() + index);
  }

  // Инвалидируем кэш ключей (индексы сдвинулись)
  m_rowKeyCache.clear();
  LOG_DEBUG("ChannelDataModel::RemoveChannel: erased, calling Reset()");
  Reset(m_channels.size());
  LOG_DEBUG("ChannelDataModel::RemoveChannel: Reset() done");
}
