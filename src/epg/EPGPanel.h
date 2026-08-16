#ifndef EPGPANEL_H
#define EPGPANEL_H

#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/grid.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include "Channel.h"
#include "EPGData.h"
#include "EPGManager.h"

class EPGPanel : public wxPanel {
public:
  EPGPanel(wxWindow *parent);
  ~EPGPanel();

  void SetChannel(const Channel &channel);
  void LoadProgramsForChannel(const std::string &channelId, time_t date);
  void SaveState();
  void RestoreState();
  void ClearStatus();

  void OnEpgUpdateFinished(int status, const wxString &error = wxEmptyString);
  void ShowMatchProgress(bool show);
  void UpdateMatchProgress(int matched, int total, int progress);

  void SetActive(bool active) { m_isActive = active; }
  bool IsActive() const { return m_isActive; }

  bool HasChannel() const { return !m_currentChannelId.empty(); }
  const std::string &GetCurrentChannelId() const { return m_currentChannelId; }
  time_t GetCurrentDate() const { return m_currentDate; }

private:
  // UI
  wxStaticText *m_channelNameLabel;
  wxStaticText *m_dateLabel;
  wxButton *m_prevDayBtn;
  wxButton *m_todayBtn;
  wxButton *m_nextDayBtn;
  wxGrid *m_programGrid;
  wxStaticText *m_detailTitle;
  wxTextCtrl *m_detailDesc;

  // Данные
  Channel m_currentChannel;
  std::string m_currentChannelId;
  std::string m_currentChannelName;
  time_t m_currentDate;
  std::vector<EpgProgram> m_currentPrograms;
  EPGManager *m_epgManager;

  bool m_isActive;
  bool m_hasError;
  wxString m_lastError;

  // Статическое состояние
  static std::string s_lastChannelId;
  static std::string s_lastChannelName;
  static std::string s_lastPlaylistName;
  static time_t s_lastDate;

  void SetupUI();
  void UpdateDateLabel();
  void OnPrevDay(wxCommandEvent &);
  void OnNextDay(wxCommandEvent &);
  void OnToday(wxCommandEvent &);
  void OnProgramSelected(wxGridEvent &);
  void AdjustProgramColumns();
  void OnProgramListResize(wxSizeEvent &);
  void SetStatus(const wxString &brief, const wxString &detail);
  void ShowMessage(const wxString &msg);

  wxDECLARE_EVENT_TABLE();
};

#endif
