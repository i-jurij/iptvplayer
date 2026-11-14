#ifndef ADDIPTVPLAYLISTDIALOG_H
#define ADDIPTVPLAYLISTDIALOG_H

#include "IPTVOrgMetadataManager.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

class PlaylistManager;

class AddIPTVPlaylistDialog : public wxDialog {
public:
  AddIPTVPlaylistDialog(wxWindow *parent, PlaylistManager *playlistMgr);
  ~AddIPTVPlaylistDialog() override;

  wxString GetSelectedUrl() const { return m_selectedUrl; }
  wxString GetSelectedTitle() const { return m_selectedTitle; }
  bool GetAutoUpdate() const {
    return m_autoUpdateCheck ? m_autoUpdateCheck->GetValue() : true;
  }

private:
  enum class FilterType { Country, Language, Category };

  // Worker writes into this; UI polls done flag via wxTimer.
  // Worker NEVER touches `this` or any wx widget.
  struct FetchToken {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    FilterType filterType = FilterType::Country;
    bool success = false;
    std::vector<Country> countries;
    std::vector<Language> languages;
    std::vector<Category> categories;
    std::string errorText;
  };

  PlaylistManager *m_playlistMgr;
  std::shared_ptr<IPTVOrgMetadataManager> m_metadataMgr;

  wxChoice *m_filterTypeChoice = nullptr;
  wxTextCtrl *m_searchCtrl = nullptr;
  wxDataViewListCtrl *m_dataViewList = nullptr;
  wxButton *m_refreshBtn = nullptr;
  wxCheckBox *m_autoUpdateCheck = nullptr;
  FilterType m_filterType = FilterType::Country;

  wxString m_selectedUrl;
  wxString m_selectedTitle;
  wxString m_selectedCode;

  // Parallel to m_allCodes. m_allNames используется для заголовка (без сети).
  std::vector<wxString> m_allDisplayItems;
  std::vector<wxString> m_allNames;
  std::vector<wxString> m_allCodes;

  std::shared_ptr<FetchToken> m_currentFetch;
  wxTimer m_pollTimer;
  std::atomic<bool> m_updating{false};

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
  void OnFetchPoll(wxTimerEvent &event);
  void OnDataLoaded(FilterType filterType, bool success,
                    const std::vector<Country> &countries,
                    const std::vector<Language> &languages,
                    const std::vector<Category> &categories,
                    const std::string &errorText);

  bool BuildPlaylistUrl(FilterType filterType, const wxString &code,
                        const wxString &name,
                        wxString &outUrl, wxString &outTitle);
  void UpdateList(const wxString &filterText);
};

#endif
