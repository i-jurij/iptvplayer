#ifndef EPGPANEL_H
#define EPGPANEL_H

#include <wx/activityindicator.h>
#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

#include "Channel.h"
#include "EPGData.h"

#include <chrono>

class EPGManager;
class Channel;

enum Mode { MODE_PLAYLIST, MODE_FAVORITES };

class EPGPanel : public wxPanel {
public:
  EPGPanel(wxWindow *parent);
  ~EPGPanel();

  void SetChannels(const std::vector<Channel> &channels);
  void SetCurrentChannel(Channel channel);
  
  void SaveState();
  void RestoreState();
  void ClearStatus();

  void SetFavoriteChannels(const std::vector<Channel> &channels);
  void SwitchMode(Mode mode);

  void OnEpgUpdateFinished(int status, const wxString &error = wxEmptyString);

private:
  bool m_refreshing = false;
  
  wxGauge *m_progressGauge;
  wxStaticText *m_progressText;
  wxButton *m_cancelBtn;
  wxTimer m_progressTimer;

  double m_prevDownloaded = 0.0;
  std::chrono::steady_clock::time_point m_prevTime;

  void OnProgressTimer(wxTimerEvent &event);
  void OnCancelDownload(wxCommandEvent &event);
  void UpdateProgressUI();
  void ShowProgressControls(bool show);
  
  wxString m_lastError;    // последняя ошибка загрузки/парсинга
  bool m_hasError = false; // флаг наличия ошибки

  enum {
    ID_PREV_DAY = wxID_HIGHEST + 100,
    ID_NEXT_DAY,
    ID_TODAY,
    ID_REFRESH_EPG,
    ID_MANAGE_SOURCES,
    ID_CHANNEL_LIST = wxID_HIGHEST + 200,
    ID_SEARCH_CTRL,
    ID_PROGRAM_LIST,
    ID_CANCEL_DOWNLOAD = wxID_HIGHEST + 300
  };

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
  void OnChannelActivated(wxDataViewEvent &event);
  void OnSearchText(wxCommandEvent &event);
  void OnPrevDay(wxCommandEvent &event);
  void OnNextDay(wxCommandEvent &event);
  void OnToday(wxCommandEvent &event);
  void OnRefreshEPG(wxCommandEvent &event);
  void OnManageSources(wxCommandEvent &event);
  void LoadProgramsForChannel(const std::string &channelId, time_t date);
  void OnProgramSelected(wxListEvent &event);
  void ShowMessage(const wxString &msg);

  wxDECLARE_EVENT_TABLE();
};

#endif
