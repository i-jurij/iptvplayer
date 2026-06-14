#include "TypeAheadSearch.h"
#include <wx/frame.h>
#include <wx/utils.h> // wxGetTopLevelParent
#include <wx/window.h>

TypeAheadSearch::TypeAheadSearch(wxWindow *owner, std::function<int()> getCount,
                                 std::function<wxString(int)> getName,
                                 std::function<void(int)> selectRow)
    : m_owner(owner), m_timer(), // таймер пока без owner
      m_getCount(getCount), m_getName(getName), m_selectRow(selectRow) {
  wxWindow *top = wxGetTopLevelParent(owner);
  if (!top)
    top = owner;

  // привязываем таймер к top-level окну
  m_timer.SetOwner(top);

  int id = m_timer.GetId();

  top->Bind(
      wxEVT_TIMER,
      [this](wxTimerEvent &e) {
        // фильтруем по ID, на всякий случай
        if (e.GetId() == m_timer.GetId())
          OnTimer(e);
        else
          e.Skip();
      },
      id);
}

void TypeAheadSearch::OnChar(wxKeyEvent &evt) {
  int key = evt.GetKeyCode();

  if (key == WXK_ESCAPE) {
    m_buffer.Clear();
    if (auto *f = dynamic_cast<wxFrame *>(wxGetTopLevelParent(m_owner)))
      f->SetStatusText("", 0);
    m_timer.Stop();
    return;
  }

  wxUniChar u = evt.GetUnicodeKey();
  if (u == WXK_NONE || u < 32) {
    evt.Skip();
    return;
  }

  StartSearch(wxString(u));
}

void TypeAheadSearch::StartSearch(const wxString &str) {
  if (!m_timer.IsRunning())
    m_buffer.Clear();

  m_buffer += str.Lower();

  if (auto *f = dynamic_cast<wxFrame *>(wxGetTopLevelParent(m_owner)))
    f->SetStatusText("Поиск: " + m_buffer, 0);

  PerformSearch();

  m_timer.Start(1000, wxTIMER_ONE_SHOT);
}

static bool Fuzzy(const wxString &name, const wxString &needle) {
  int j = 0;
  for (int i = 0; i < (int)name.length() && j < (int)needle.length(); ++i)
    if (name[i] == needle[j])
      j++;
  return j == (int)needle.length();
}

void TypeAheadSearch::PerformSearch() {
  wxString needle = m_buffer.Lower();
  if (needle.IsEmpty())
    return;

  int count = m_getCount();
  if (count <= 0)
    return;

  std::vector<int> matches;

  // 1. startswith
  for (int i = 0; i < count; i++) {
    wxString name = m_getName(i).Lower();
    if (name.StartsWith(needle))
      matches.push_back(i);
  }

  // 2. multi-word
  if (matches.empty() && needle.Contains(" ")) {
    auto parts = wxSplit(needle, ' ');
    for (int i = 0; i < count; i++) {
      wxString name = m_getName(i).Lower();
      bool ok = true;
      for (auto &p : parts)
        if (!name.Contains(p)) {
          ok = false;
          break;
        }
      if (ok)
        matches.push_back(i);
    }
  }

  // 3. fuzzy
  if (matches.empty()) {
    for (int i = 0; i < count; i++) {
      wxString name = m_getName(i).Lower();
      if (Fuzzy(name, needle))
        matches.push_back(i);
    }
  }

  if (matches.empty())
    return;

  int next = matches[0];

  if (m_lastRow >= 0) {
    for (size_t i = 0; i < matches.size(); i++)
      if (matches[i] == m_lastRow)
        next = matches[(i + 1) % matches.size()];
  }

  m_lastRow = next;
  m_selectRow(next);
}

void TypeAheadSearch::OnTimer(wxTimerEvent &) {
  m_buffer.Clear();
  if (auto *f = dynamic_cast<wxFrame *>(wxGetTopLevelParent(m_owner)))
    f->SetStatusText("", 0);
}
