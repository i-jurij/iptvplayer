#ifndef ADDIPTVPLAYLISTDIALOG_H
#define ADDIPTVPLAYLISTDIALOG_H

#include "IPTVOrgMetadataManager.h"
#include <atomic>
#include <string>
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
  PlaylistManager *m_playlistMgr;
  IPTVOrgMetadataManager *m_metadataMgr;

  enum class FilterType { Country, Language, Category };
  wxChoice *m_filterTypeChoice;
  wxTextCtrl *m_searchCtrl;
  wxDataViewListCtrl *m_dataViewList; // используем ListCtrl
  wxButton *m_refreshBtn;
  FilterType m_filterType = FilterType::Country;

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
  void BindEvents();
  void PopulateFilterTypes();
  void OnFilterTypeChanged(wxCommandEvent &event);
  void OnRefresh(wxCommandEvent &event);
  void OnOK(wxCommandEvent &event);
  void OnCancel(wxCommandEvent &event);
  void OnSearchText(wxCommandEvent &event);
  void OnSelectionChanged(wxDataViewEvent &event);

  void StartAsyncFetch(FilterType filterType);
  void OnDataLoaded(FilterType filterType, bool success,
                    const std::vector<Country> &countries,
                    const std::vector<Language> &languages,
                    const std::vector<Category> &categories,
                    const std::string &errorText);

  bool BuildPlaylistUrl(FilterType filterType, const wxString &code,
                        wxString &outUrl, wxString &outTitle);
  wxString GetDisplayName(FilterType filterType, const wxString &code);

  void UpdateList(const wxString &filterText);
};

#endif
