#ifndef MANUALMAPPINGDIALOG_H
#define MANUALMAPPINGDIALOG_H

#include <string>
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

private:
  EPGManager *m_epgMgr;
  MainFrame *m_mainFrame;

  wxListCtrl *m_playlistList;
  wxListCtrl *m_epgList;
  wxListCtrl *m_mappingList;
  wxButton *m_addBtn;
  wxButton *m_removeBtn;

  std::string m_selectedPlaylistTvgId;
  std::string m_selectedEpgId;
  long m_selectedMappingIndex;

  // Методы заполнения списков
  void PopulatePlaylistChannels();
  void PopulateEpgChannels();
  void PopulateMappings();

  // Обработчики событий
  void OnPlaylistSelected(wxListEvent &event);
  void OnEpgSelected(wxListEvent &event);
  void OnMappingSelected(wxListEvent &event);
  void OnAddMapping(wxCommandEvent &event);
  void OnRemoveMapping(wxCommandEvent &event);

  // Вспомогательные
  void UpdateButtons();
  void SelectMapping(const std::string &tvgId, const std::string &epgId);
};

#endif // MANUALMAPPINGDIALOG_H
