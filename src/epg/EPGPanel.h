#ifndef EPGPANEL_H
#define EPGPANEL_H

#include <wx/activityindicator.h>
#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>

#include "Channel.h"
#include "EPGData.h"

class EPGManager;
class Channel;

enum Mode { MODE_PLAYLIST, MODE_FAVORITES };

class EPGPanel : public wxPanel {
public:
  EPGPanel(wxWindow *parent);
  ~EPGPanel();

  void SetChannels(const std::vector<Channel> &channels);
  void SetCurrentChannel(Channel channel);

  void RefreshCurrentChannel();
  void SaveState();
  void RestoreState();
  void ClearStatus();

  void SetFavoriteChannels(const std::vector<Channel> &channels);
  void SwitchMode(Mode mode);

private:
  std::vector<EpgProgram> m_currentPrograms;
  
  std::vector<Channel> m_playlistChannels;
  std::vector<Channel> m_favoriteChannels;
  Mode m_currentMode = MODE_PLAYLIST;
  wxToggleButton *m_btnPlaylist = nullptr;
  wxToggleButton *m_btnFavorites = nullptr;
  void UpdateModeButtons(Mode mode);
  void SelectCurrentChannelInList();
  bool IsChannelInSource(const Channel &ch,
                         const std::vector<Channel> &source) const;

  Channel m_currentChannel;
  
  void SetStatus(const wxString &brief, const wxString &detail);

  static std::string s_lastChannelId;
  static std::string s_lastChannelName;
  static std::string s_lastPlaylistName;
  static time_t s_lastDate;

  wxActivityIndicator *m_activityIndicator;

  EPGManager *m_epgManager;
  const std::vector<Channel> *m_allChannels = nullptr;
  std::string m_currentChannelId;
  std::string m_currentChannelName;
  time_t m_currentDate;

  wxTextCtrl *m_searchCtrl;
  wxDataViewCtrl *m_channelListView;
  wxStaticText *m_channelNameLabel;
  wxStaticText *m_dateLabel;
  wxButton *m_prevDayBtn;
  wxButton *m_todayBtn;
  wxButton *m_nextDayBtn;
  wxListCtrl *m_programList;
  wxStaticText *m_detailTitle;
  wxStaticText *m_detailDesc;
  wxButton *m_refreshBtn;
  wxButton *m_manageSourcesBtn;

  class ChannelListModel : public wxDataViewVirtualListModel {
  public:
    ChannelListModel();
    void SetChannels(const std::vector<Channel> &channels);
    void Filter(const wxString &filter);
    unsigned int GetCount() const override;
    void GetValueByRow(wxVariant &variant, unsigned int row,
                       unsigned int col) const override;
    bool SetValueByRow(const wxVariant &variant, unsigned int row,
                       unsigned int col) override;
    wxString GetColumnType(unsigned int col) const override;
    unsigned int GetRow(const wxDataViewItem &item) const override;
    wxDataViewItem
    GetItemByRow(unsigned int row) const; // не override, просто метод
    void Clear();
    const Channel &GetChannel(unsigned int row) const;
    int FindChannel(const Channel &ch) const;
    void SetSource(const std::vector<Channel> *source);

  private:
    const std::vector<Channel> *m_currentSource = nullptr;
    const std::vector<Channel> *m_channels = nullptr;
    std::vector<Channel> m_filteredChannels;
    wxString m_filterText;
  };

  ChannelListModel *m_channelModel;

  void SetupUI();
  void OnChannelSelected(wxDataViewEvent &event);
  void OnSearchText(wxCommandEvent &event);
  void OnPrevDay(wxCommandEvent &event);
  void OnNextDay(wxCommandEvent &event);
  void OnToday(wxCommandEvent &event);
  void OnRefreshEPG(wxCommandEvent &event);
  void OnManageSources(wxCommandEvent &event);
  void LoadProgramsForChannel(const std::string &channelId, time_t date);
  void OnProgramSelected(wxListEvent &event);
  void OnEPGUpdated(wxCommandEvent &event);
  void ShowMessage(const wxString &msg);

  wxDECLARE_EVENT_TABLE();
};

#endif
