#pragma once

#include <wx/activityindicator.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <chrono>
#include <future>
#include <unordered_map>
#include <unordered_set>

class MainFrame;
class EPGManager;
class wxWindow;
class EpgProgressInfo;

class EpgSourceManagerPanel : public wxPanel {
public:
  EpgSourceManagerPanel(wxWindow *parent, EPGManager *epgMgr,
                        bool showAutoUpdateSettings = false);
  virtual ~EpgSourceManagerPanel();

  void UpdateSourceList();
  void SaveSettings();
  void LoadSettings();
  void EnableButtons(bool enable);
  bool IsDirty() const;
  void SetMainFrame(MainFrame *mainFrame) { m_mainFrame = mainFrame; }
  void SetBusy(bool busy);

private:
  EPGManager *m_epgMgr;
  MainFrame *m_mainFrame = nullptr;

  wxButton *m_aboutMatchBtn;
  wxButton *m_editRulesBtn;
  void OnAboutMatch(wxCommandEvent &event);
  void OnEditRules(wxCommandEvent &event);
  
  void OnToggleAutoUpdate(wxListEvent &event);
  
  wxButton *m_autoApplyBtn;
  void OnAutoApply(wxCommandEvent &event);

  wxScrolledWindow *m_scrolledWindow;

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

  // Кэш доступности
  struct AvailabilityCacheEntry {
    bool available;
    std::chrono::steady_clock::time_point timestamp;
    int failCount = 0;
  };
  std::unordered_map<std::string, AvailabilityCacheEntry> m_availabilityCache;

  // Методы
  void SetupUI();
  void AdjustColumnWidths();
  void CheckAvailabilityAsync(const std::string &url);
  void OnAvailabilityChecked(const std::string &url, bool available);
  void SaveMatchSettings();
  void RefreshSourceInternal(const std::string &url, const std::string &name,
                               std::function<void(bool)> onComplete = nullptr);
  void OnApplyMatch(wxCommandEvent &event);
  void OnProgressUpdate(const EpgProgressInfo &info);

  // Обработчики событий
  void OnAdd(wxCommandEvent &event);
  void OnEdit(wxCommandEvent &event);
  void OnRemove(wxCommandEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnDeleteCache(wxCommandEvent &event);
  void OnSourceSelected(wxListEvent &event);
};
