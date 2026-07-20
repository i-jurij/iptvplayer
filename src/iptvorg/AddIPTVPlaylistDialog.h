#ifndef ADDIPTVPLAYLISTDIALOG_H
#define ADDIPTVPLAYLISTDIALOG_H

#include "IPTVOrgMetadataManager.h"
#include <atomic>
#include <vector>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>

class PlaylistManager;
class IPTVOrgMetadataManager;

class AddIPTVPlaylistDialog : public wxDialog {
public:
  AddIPTVPlaylistDialog(wxWindow *parent, PlaylistManager *playlistMgr);
  ~AddIPTVPlaylistDialog();

  wxString GetSelectedUrl() const { return m_selectedUrl; }
  wxString GetSelectedTitle() const { return m_selectedTitle; }

private:
  enum {
    ID_FILTER_TYPE_CHOICE = wxID_HIGHEST + 301,
    ID_SEARCH_CTRL = wxID_HIGHEST + 302,
    ID_REFRESH_BTN = wxID_HIGHEST + 303
  };

  PlaylistManager *m_playlistMgr;
  IPTVOrgMetadataManager *m_metadataMgr;

  wxChoice *m_filterTypeChoice;
  wxTextCtrl *m_searchCtrl;
  wxDataViewListCtrl *m_dataViewList; // используем ListCtrl
  wxButton *m_refreshBtn;

  wxString m_selectedUrl;
  wxString m_selectedTitle;
  wxString m_selectedCode;

  // Храним все элементы для фильтрации (без учёта фильтра)
  std::vector<wxString> m_allDisplayItems;
  std::vector<wxString> m_allCodes;

  std::atomic<bool> m_cancelled{false};
  std::atomic<bool> m_loading{false};
  bool m_updating = false;

  void InitializeUI();
  void PopulateFilterTypes();
  void OnFilterTypeChanged(wxCommandEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnOK(wxCommandEvent &event);
  void OnCancel(wxCommandEvent &event);
  void OnSearchText(wxCommandEvent &event);
  void OnSelectionChanged(wxDataViewEvent &event);

  void StartAsyncFetch(const wxString &filterType);
  void OnDataLoaded(const wxString &filterType, bool success,
                    const std::vector<Country> &countries,
                    const std::vector<Language> &languages,
                    const std::vector<Category> &categories);

  bool BuildPlaylistUrl(const wxString &filterType, const wxString &code,
                        wxString &outUrl, wxString &outTitle);
  wxString GetDisplayName(const wxString &filterType, const wxString &code);

  void UpdateList(const wxString &filterText);

  wxDECLARE_EVENT_TABLE();
};

#endif