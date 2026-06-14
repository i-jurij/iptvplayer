#include "UpdateAllThread.h"
#include "MainFrame.h"
#include "PlaylistManager.h"
#include "ThreadUtils.h"
#include <iostream>
#include <wx/event.h>

UpdateAllThread::UpdateAllThread(MainFrame *frame, PlaylistManager *pm)
    : wxThread(wxTHREAD_DETACHED), m_frame(frame), m_pm(pm),
      m_indices() // пустой => обновлять все
{}

UpdateAllThread::UpdateAllThread(MainFrame *frame, PlaylistManager *pm,
                                 const std::vector<std::size_t> &indices)
    : wxThread(wxTHREAD_DETACHED), m_frame(frame), m_pm(pm),
      m_indices(indices) {}

void UpdateAllThread::postProgress(int processed, int total) {
  if (!m_frame || m_frame->IsBeingDeleted() || m_frame->isClosing())
    return;

  // Debug log for thread-side values
  wxLogDebug("UpdateAllThread::postProgress processed=%d total=%d", processed,
             total);

  // Use CallAfter but do NOT capture m_frame directly (it may be deleted).
  // Inside the lambda obtain the current top window and post the event to it.
  wxTheApp->CallAfter([processed, total]() {
    // Get the current top-level window (main window) in the GUI thread
    wxWindow *top = wxTheApp->GetTopWindow();
    if (!top)
      return;

    // Ensure the window is not being deleted
    if (top->IsBeingDeleted())
      return;

    // Post a wxCommandEvent with the same id EVT_UPDATE_PROGRESS
    wxCommandEvent ev(EVT_UPDATE_PROGRESS);
    ev.SetInt(processed);
    ev.SetExtraLong(total);

    // Use ProcessEvent to deliver synchronously in GUI thread
    top->GetEventHandler()->ProcessEvent(ev);
  });
}

wxThread::ExitCode UpdateAllThread::Entry() {
  if (!m_pm) {
    std::cerr << "UpdateAllThread: PlaylistManager is null" << std::endl;
    postEvent(m_frame, EVT_UPDATE_ALL_DONE, 0);
    return (wxThread::ExitCode)0;
  }

  // Формируем список индексов для обновления
  std::vector<std::size_t> toUpdate;
  if (!m_indices.empty()) {
    toUpdate = m_indices; // используем переданный список
  } else {
    // старое поведение: все плейлисты по порядку
    const std::size_t totalPm = m_pm->size();
    toUpdate.reserve(totalPm);
    for (std::size_t i = 0; i < totalPm; ++i)
      toUpdate.push_back(i);
  }

  const int total = static_cast<int>(toUpdate.size());
  if (total <= 0) {
    std::cerr << "UpdateAllThread: No playlists to update" << std::endl;
    postEvent(m_frame, EVT_UPDATE_ALL_DONE, 0);
    return (wxThread::ExitCode)0;
  }

  int updated = 0;
  try {
    for (int idx = 0; idx < total; ++idx) {
      if (TestDestroy())
        break;
      std::size_t i = toUpdate[static_cast<std::size_t>(idx)];
      if (validatePlaylistAccess(m_pm, static_cast<int>(i))) {
        if (m_pm->updatePlaylist(i) == ErrorCode::OK)
          ++updated;
      }
      postProgress(idx + 1, total);
    }
  } catch (const std::exception &e) {
    std::cerr << "UpdateAllThread exception: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "UpdateAllThread unknown exception" << std::endl;
  }

  postEvent(m_frame, EVT_UPDATE_ALL_DONE, updated);
  return (wxThread::ExitCode)0;
}
