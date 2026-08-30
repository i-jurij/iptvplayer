#ifndef MANUALMAPPINGDIALOG_H
#define MANUALMAPPINGDIALOG_H

#include "Channel.h"
#include <string>
#include <vector>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>

class EPGManager;
class MainFrame;

class ManualMappingDialog : public wxDialog {
public:
  ManualMappingDialog(wxWindow *parent, EPGManager *epgMgr,
                      MainFrame *mainFrame);
  virtual ~ManualMappingDialog();

  void SetPreselectedChannel(const Channel &channel);

private:
  EPGManager *m_epgMgr;
  MainFrame *m_mainFrame;

  // --- Структуры для хранения данных списков ---
  struct PlaylistItem {
    std::string tvgId;
    std::string name;
  };
  struct EpgItem {
    std::string id;
    std::string name;
  };
  struct MappingItem {
    std::string playlistTvgId;
    std::string playlistName;
    std::string epgId;
    std::string epgName;
    std::string status; // "Manual", "Auto", "Ignored"
    std::string key;
  };

  std::vector<PlaylistItem> m_playlistItems;
  std::vector<EpgItem> m_epgItems;
  std::vector<MappingItem> m_mappingItems;

  // --- Переменные для сортировки ---
  int m_playlistSortCol;
  bool m_playlistSortAsc;
  int m_epgSortCol;
  bool m_epgSortAsc;
  int m_mappingSortCol;
  bool m_mappingSortAsc;

  // --- Элементы управления ---
  wxListCtrl *m_playlistList;
  wxListCtrl *m_epgList;
  wxListCtrl *m_mappingList;
  wxButton *m_addBtn;
  wxButton *m_removeBtn;
  wxButton *m_ignoreBtn;
  wxButton *m_unignoreBtn;

  // --- Состояние выделения ---
  std::string m_selectedPlaylistTvgId;
  std::string m_selectedPlaylistName;
  std::string m_selectedEpgId;
  long m_selectedMappingIndex;
  std::string m_selectedMappingKey; // ключ из скрытой колонки
  std::string m_preselectedTvgId;
  std::string m_preselectedChannelName;

  // --- Загрузка/обновление данных ---
  void LoadPlaylistChannels();
  void LoadEpgChannels();
  void LoadMappings();
  void RefreshPlaylistList();
  void RefreshEpgList();
  void RefreshMappingList();

  // --- Сортировка ---
  void SortPlaylist();
  void SortEpg();
  void SortMappings();

  // --- Выделение элементов ---
  void HighlightPlaylistChannel(const std::string &tvgId,
                                const std::string &name);
  void HighlightEpgChannel(const std::string &epgId);
  void HighlightMappingByKey(const std::string &key);
  void SelectPlaylistChannel(const std::string &tvgId, const std::string &name);

  // --- Восстановление выделения после сортировки ---
  void RestorePlaylistSelection();
  void RestoreEpgSelection();
  void RestoreMappingSelection();

  // --- Обработчики событий ---
  void OnPlaylistSelected(wxListEvent &event);
  void OnEpgSelected(wxListEvent &event);
  void OnMappingSelected(wxListEvent &event);
  void OnAddMapping(wxCommandEvent &event);
  void OnRemoveMapping(wxCommandEvent &event);
  void OnIgnore(wxCommandEvent &event);
  void OnUnignore(wxCommandEvent &event);

  void OnPlaylistColClick(wxListEvent &event);
  void OnEpgColClick(wxListEvent &event);
  void OnMappingColClick(wxListEvent &event);

  void UpdateButtons();
  void AdjustMappingColumns();
  void HighlightPreselected();
};

#endif // MANUALMAPPINGDIALOG_H
