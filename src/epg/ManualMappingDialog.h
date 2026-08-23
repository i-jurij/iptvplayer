#ifndef MANUALMAPPINGDIALOG_H
#define MANUALMAPPINGDIALOG_H

#include "Channel.h"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>

#include <string>

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

  void AdjustMappingColumns();
  
  std::string m_preselectedTvgId;
  void HighlightPreselected();

  wxListCtrl *m_playlistList;
  wxListCtrl *m_epgList;
  wxListCtrl *m_mappingList;
  wxButton *m_addBtn;
  wxButton *m_removeBtn;
  wxButton *m_ignoreBtn;
  wxButton *m_unignoreBtn;

  std::string m_selectedPlaylistTvgId;
  std::string m_selectedPlaylistName;
  std::string m_selectedEpgId;
  long m_selectedMappingIndex;
  std::string m_preselectedChannelName;

  void PopulatePlaylistChannels();
  void PopulateEpgChannels();
  void PopulateMappings();

  void OnPlaylistSelected(wxListEvent &event);
  void OnEpgSelected(wxListEvent &event);
  void OnMappingSelected(wxListEvent &event);
  void OnAddMapping(wxCommandEvent &event);
  void OnRemoveMapping(wxCommandEvent &event);
  void OnIgnore(wxCommandEvent &event);
  void OnUnignore(wxCommandEvent &event);

  void UpdateButtons();
  void SelectMapping(const std::string &tvgId, const std::string &epgId);
  void HighlightEpgChannel(const std::string &epgId);
  void HighlightMapping(const std::string &tvgId);
};

#endif // MANUALMAPPINGDIALOG_H
