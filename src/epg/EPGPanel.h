#ifndef EPGPANEL_H
#define EPGPANEL_H

#include "Channel.h"
#include "EPGData.h"
#include "EPGManager.h"

#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/grid.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <atomic>
#include <memory>

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
  wxButton *m_playBtn;
  void UpdateHeader();
  
  void OnManualMapping(wxCommandEvent &event);
  void OnPlay(wxCommandEvent &event);

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

  // async EPG request tracking.
  // m_pendingRequestId — монотонный счётчик. Callback применяет результат
  // только если id совпадает (защита от stale callbacks при быстром
  // переключении каналов/дат).
  // m_pendingCancelFlag — cancel-флаг текущего фонового запроса. Новый
  // LoadProgramsForChannel или ~EPGPanel устанавливает *flag = true → curl
  // abort (быстрое завершение).
  uint64_t m_pendingRequestId = 0;
  std::shared_ptr<std::atomic<bool>> m_pendingCancelFlag;

  // Заполняет grid полученными программами (после async).
  // m_currentPrograms обновляется; grid НЕ очищается (caller очистил).
  void RenderPrograms(const std::vector<EpgProgram> &programs);

  // кнопка Play — live или catch-up в зависимости от выбранной
  // программы. Disabled для будущих и для прошедших вне окна архива.
  void UpdatePlayButtonState();
  bool IsProgramInArchive(const EpgProgram &prog) const;
  int64_t GetCurrentChannelArchiveDuration() const;

  bool m_isActive;
  void SetupUI();
  void UpdateDateLabel();
  void OnPrevDay(wxCommandEvent &);
  void OnNextDay(wxCommandEvent &);
  void OnToday(wxCommandEvent &);
  void OnProgramSelected(wxGridEvent &);
  void AdjustProgramColumns();
  void OnProgramListResize(wxSizeEvent &);
};

#endif
