#pragma once
#include <functional>
#include <wx/string.h>
#include <wx/timer.h>

class TypeAheadSearch {
public:
  TypeAheadSearch(wxWindow *owner, std::function<int()> getCount,
                  std::function<wxString(int)> getName,
                  std::function<void(int)> selectRow);

  void OnChar(wxKeyEvent &evt);

private:
  void StartSearch(const wxString &str);
  void PerformSearch();
  void OnTimer(wxTimerEvent &);

  wxWindow *m_owner;
  wxTimer m_timer;

  wxString m_buffer;
  int m_lastRow = -1;

  std::function<int()> m_getCount;
  std::function<wxString(int)> m_getName;
  std::function<void(int)> m_selectRow;
};
