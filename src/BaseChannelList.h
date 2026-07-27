#pragma once

#include <atomic>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/timer.h>

#include "Channel.h"
#include "ChannelDataModel.h"
#include "LogControl.h"
#include "TypeAheadSearch.h"
#include "Utils.h"

class BaseChannelList : public wxDataViewCtrl {
public:
  BaseChannelList(wxWindow *parent, wxWindowID id);
  virtual ~BaseChannelList();

  void LoadChannels(const std::vector<Channel> &channels,
                    const std::string &playlistName);
  void LoadFavoritesChannels(const std::vector<Channel> &channels,
                             const std::string &playlistName);

  ChannelDataModel *GetModel() { return m_model; }
  const ChannelDataModel *GetModel() const { return m_model; }

  void BeginFavoritesSync();
  void EndFavoritesSync();

  int GetTopVisibleRow();
  void RestoreTopVisibleRow(int row);

  void PauseLogoLoading();
  void ResumeLogoLoading();

  void AddPendingKeys(const std::vector<std::string> &keys);
  void OnLazyLoad(wxTimerEvent &evt);

  void EnqueueRowLoad(size_t row, bool highPriority);
  struct InflightGuard {
    std::atomic<int> &counter;
    InflightGuard(std::atomic<int> &c) : counter(c) { counter.fetch_add(1); }
    ~InflightGuard() { counter.fetch_sub(1); }
  };
  void processLoadQueue();
  void ResetVisibleRange();
  void CoalescedDoLazyLoadSchedule();

  std::atomic<bool> m_closing;

  void RefreshProgramColumn();
  void RefreshProgramColumnVisible();

  void ClearPendingLoads();

protected:
  int m_lastDpi = 96;

  bool m_epgUpdatePending = false;
  wxTimer m_epgUpdateTimer;
  void OnEpgUpdateTimer(wxTimerEvent &);
  
  PerformanceMode m_perfMode{PerformanceMode::Balanced};

  // Новый главный механизм отслеживания скролла
  void OnInternalIdle() override;

  int GetAccurateTopRow();

  void OnDPIChanged(wxDPIChangedEvent &evt);
  void OnItemActivated(wxDataViewEvent &evt);
  void OnColumnSorted(wxDataViewEvent &evt);
  void OnDestroy(wxWindowDestroyEvent &evt);
  void OnShowEvent(wxShowEvent &evt);

  virtual void OnChannelActivated(const Channel &ch, int col) = 0;
  virtual void OnFavoriteToggled(const Channel &ch, bool isFav) = 0;

  void InitColumns();

  ChannelDataModel *m_model;
  std::string m_playlistName;

  wxTimer m_lazyLoadTimer;
  wxTimer m_pendingWatchdogTimer;

  wxBitmap m_favIconFilled;
  wxBitmap m_favIconOutline;

  std::unique_ptr<TypeAheadSearch> m_search;

  std::atomic<uint64_t> m_lazySeq{0};

  size_t m_lastTopRow = 0;
  size_t m_lastVisibleCount = 0;

  static constexpr size_t kPrefetchCount = 500;

  struct QueueItem {
    size_t row;
    int priority;
    uint64_t modelVer;
    uint64_t enqueueTs;
    int retryCount;
  };

  std::deque<QueueItem> m_loadQueue;
  std::mutex m_queueMutex;

  std::atomic<int> m_inflightLoads{0};
  int m_maxConcurrentLoads{8};
  size_t m_maxTotalPending{500};

  std::atomic<bool> m_queuePaused{false};
  std::atomic<bool> m_loadingPaused{false};

  int m_maxPerHost{6};
  std::unordered_map<std::string, int> m_inflightPerHost;

  std::unordered_set<std::string> m_queuedKeys;

  std::atomic<int> m_favSyncDepth{0};
  std::atomic<bool> m_lazyLoadInProgress{false};

  std::atomic<bool> m_processing{false};

  std::list<std::string> m_lruQueuedKeys;
  std::unordered_map<std::string, std::list<std::string>::iterator> m_lruIter;

  // favorites sync flag
  bool m_syncingFavorites = false;

  // diagnostic counters
  std::atomic<size_t> m_diag_enqueued{0};
  std::atomic<size_t> m_diag_dropped{0};
  std::atomic<size_t> m_diag_evicted{0};
  std::atomic<size_t> m_diag_started{0};
  std::atomic<size_t> m_diag_success{0};

  std::atomic<size_t> m_dynamicPrefetch{kPrefetchCount};

  std::unordered_map<std::string, uint64_t> m_pendingLogoLoads;

  std::atomic<bool> m_bgWorkerRequested{false};

  std::atomic<bool> m_appendCoalesced{false};

  void DoLazyLoad();
  void HandleVisibleRangeChange();
  void ScheduleLogoLoadForRow(size_t row, uint64_t seq);

  int GetCountPerPage() const override;
  bool IsShownOnScreen() const override;

  static std::string ExtractHost(const std::string &url);

  void OnPendingWatchdog(wxTimerEvent &evt);

  // NEW: Обратное отображение key -> row для оптимизации watchdog
  std::unordered_map<std::string, size_t> m_keyToRow;
  std::mutex m_keyToRowMutex;

  // Helper methods для синхронной очистки маппинга
  void RemoveKeyMapping(const std::string &key) {
    std::lock_guard<std::mutex> lk(m_keyToRowMutex);
    m_keyToRow.erase(key);
  }

  void AddKeyMapping(const std::string &key, size_t row) {
    std::lock_guard<std::mutex> lk(m_keyToRowMutex);
    m_keyToRow[key] = row;
  }

  void ScheduleProcessLoadQueue() {
    if (m_queuePaused.load() || m_loadingPaused.load()) {
      return;
    }
    // Если нет работы, не планируем
    if (m_loadQueue.empty() && m_pendingLogoLoads.empty()) {
      return;
    }
    // Останавливаем и перезапускаем таймер — сброс задержки при каждом вызове
    m_processQueueTimer.Stop();
    m_processQueueTimer.StartOnce(50);
  }

  wxTimer m_processQueueTimer;
  void OnProcessQueueTimer(wxTimerEvent &event);

  wxDECLARE_EVENT_TABLE();
};
