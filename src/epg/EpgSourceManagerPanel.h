#pragma once

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/spinctrl.h>

class EPGManager;
class wxWindow;

class EpgSourceManagerPanel : public wxPanel {
public:
  EpgSourceManagerPanel(wxWindow *parent, EPGManager *epgMgr,
                        bool showAutoUpdateSettings = false);
  virtual ~EpgSourceManagerPanel();

  // Обновить список источников
  void UpdateSourceList();

  // Сохранить настройки (если есть)
  void SaveSettings();

  // Загрузить настройки
  void LoadSettings();

  // Управление кнопками
  void EnableButtons(bool enable);

  // Проверка на наличие изменений
  bool IsDirty() const;

private:
  EPGManager *m_epgMgr;

  // Основные элементы управления
  wxListCtrl *m_sourceList;
  wxButton *m_addBtn;
  wxButton *m_editBtn;
  wxButton *m_removeBtn;
  wxButton *m_refreshBtn;
  wxButton *m_deleteCacheBtn;

  // Настройки автообновления (опционально)
  wxCheckBox *m_autoUpdateCheck;
  wxSpinCtrl *m_updateIntervalSpin;
  wxSpinCtrl *m_daysToKeepSpin;

  bool m_showAutoUpdateSettings;
  bool m_dirty;

  enum {
    ID_ADD_SOURCE = wxID_HIGHEST + 1000,
    ID_EDIT_SOURCE,
    ID_REMOVE_SOURCE,
    ID_REFRESH_SOURCE,
    ID_DELETE_CACHE
  };
  
  // Обработчики событий
  void OnAdd(wxCommandEvent &event);
  void OnEdit(wxCommandEvent &event);
  void OnRemove(wxCommandEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnDeleteCache(wxCommandEvent &event);
  void OnSourceSelected(wxListEvent &event);

  void SetupUI();

  wxDECLARE_EVENT_TABLE();
};
