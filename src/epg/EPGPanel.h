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

class MainFrame;

class EPGPanel : public wxPanel {
public:
  EPGPanel(wxWindow *parent, MainFrame *mainFrame);
  ~EPGPanel();

  void SetChannel(const Channel &channel);
  void Clear();
  void LoadProgramsForChannel(const std::string &channelId, time_t date);

  void SetActive(bool active) { m_isActive = active; }
  bool IsActive() const { return m_isActive; }

  bool HasChannel() const { return !m_currentChannelId.empty(); }
  const std::string &GetCurrentChannelId() const { return m_currentChannelId; }
  time_t GetCurrentDate() const { return m_currentDate; }

private:
  MainFrame *m_mainFrame;

  void SelectProgramRow(int row);
  
  wxStaticText *m_headerLabel;
  wxButton *m_manualMapBtn;
  void UpdateHeader();
  
  void OnManualMapping(wxCommandEvent &event);

  // UI
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

  void SetupUI();
  void UpdateDateLabel();
  void OnPrevDay(wxCommandEvent &);
  void OnNextDay(wxCommandEvent &);
  void OnToday(wxCommandEvent &);
  void OnProgramSelected(wxGridEvent &);
  void AdjustProgramColumns();
  void OnProgramListResize(wxSizeEvent &);
  void ShowMessage(const wxString &msg);
};

#endif
