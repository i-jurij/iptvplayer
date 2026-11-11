#pragma once

#include "EPGData.h"

#include <wx/activityindicator.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/weakref.h>

#include <chrono>
#include <future>
#include <unordered_map>
#include <unordered_set>

class MainFrame;
class EPGManager;
class wxWindow;
struct EpgProgressInfo;

class EpgSourceManagerPanel : public wxPanel {
public:
  EpgSourceManagerPanel(wxWindow *parent, EPGManager *epgMgr,
                        bool showAutoUpdateSettings = false);
  virtual ~EpgSourceManagerPanel();

  void UpdateSourceList();
  void SaveSettings();
  void LoadSettings();
  void SetMainFrame(MainFrame *mainFrame);
  void SetBusy(bool busy);

private:
  EPGManager *m_epgMgr;
  MainFrame *m_mainFrame = nullptr;

  wxButton *m_aboutMatchBtn;
  wxButton *m_editRulesBtn;
  void OnAboutMatch(wxCommandEvent &event);
  void OnEditRules(wxCommandEvent &event);
  
  void OnToggleAutoUpdate(wxListEvent &event);

  // Возвращает текущее выделение + снимок GetSources().
  // idx == -1, если выделения нет, m_epgMgr == nullptr, или idx вне
  // границ sources.size(). Сообщений не показывает — это делают
  // вызывающие (у OnToggleAutoUpdate — silent return).
  struct SelectedSource {
    long idx = -1;
    std::vector<EpgSource> sources;
    bool ok() const { return idx >= 0; }
  };
  SelectedSource GetSelectedSource() const;
  
  wxButton *m_autoApplyBtn;
  void OnAutoApply(wxCommandEvent &event);

  wxScrolledWindow *m_scrolledWindow;
  wxStaticText *m_headerLabel = nullptr;
  wxStaticText *m_infoLabel = nullptr;

  // Основные элементы управления
  wxListCtrl *m_sourceList;
  wxButton *m_addBtn;
  wxButton *m_editBtn;
  wxButton *m_removeBtn;
  wxButton *m_refreshBtn;
  wxButton *m_deleteCacheBtn;
  wxButton *m_cancelBtn;
  wxActivityIndicator *m_activityIndicator;

  // Прогресс
  bool m_busy = false;

  // Настройки автообновления (опционально)
  wxCheckBox *m_autoUpdateCheck;
  wxSpinCtrl *m_updateIntervalSpin;
  wxSpinCtrl *m_daysToKeepSpin;

  // Настройки матчинга
  wxSpinCtrl *m_fuzzyThresholdSpin;
  wxSpinCtrl *m_substringMinLengthSpin;
  wxSpinCtrl *m_substringMinRatioSpin;
  wxSpinCtrl *m_minScoreSpin;
  wxButton *m_applyMatchBtn;

  bool m_showAutoUpdateSettings;
  bool m_dirty;

  // Токен подписки на прогресс EPG (EPGManager::AddOnProgress).
  // Отписываемся в деструкторе — иначе колбэк с висячим this остаётся
  // в реестре EPGManager и даёт UAF в UpdateProgress.
  size_t m_progressToken = 0;

  // Кэш доступности
  struct AvailabilityCacheEntry {
    bool available;
    std::chrono::steady_clock::time_point timestamp;
    int failCount = 0;
  };
  std::unordered_map<std::string, AvailabilityCacheEntry> m_availabilityCache;

  // Методы
  void SetupUI();
  void UpdateHeaderInfo();
  void AdjustColumnWidths();
  void CheckAvailabilityAsync(const std::string &url);
  void OnAvailabilityChecked(const std::string &url, bool available);
  void SaveMatchSettings();
  void RefreshSourceInternal(const std::string &url, const std::string &name);
  void OnApplyMatch(wxCommandEvent &event);
  void OnProgressUpdate(const EpgProgressInfo &info);

  // Обработчики событий
  void OnAdd(wxCommandEvent &event);
  void OnEdit(wxCommandEvent &event);
  void OnRemove(wxCommandEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnDeleteCache(wxCommandEvent &event);
  void OnSourceRightClick(wxMouseEvent &event);
};
