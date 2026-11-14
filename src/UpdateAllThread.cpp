#include "UpdateAllThread.h"
#include "MainFrame.h"
#include "PlaylistManager.h"
#include "ThreadUtils.h"
#include <iostream>
#include <wx/event.h>

UpdateAllThread::UpdateAllThread(MainFrame *frame,
                                 std::shared_ptr<PlaylistManager> pm)
    : wxThread(wxTHREAD_DETACHED),
      m_frameHandle(frame ? frame->getFrameHandle() : nullptr),
      m_pm(std::move(pm)),
      m_indices() // пустой => обновлять все
{}

UpdateAllThread::UpdateAllThread(MainFrame *frame,
                                 std::shared_ptr<PlaylistManager> pm,
                                 const std::vector<std::size_t> &indices)
    : wxThread(wxTHREAD_DETACHED),
      m_frameHandle(frame ? frame->getFrameHandle() : nullptr),
      m_pm(std::move(pm)),
      m_indices(indices) {}

void UpdateAllThread::postProgress(int processed, int total) {
  postUpdateProgress(m_frameHandle, processed, total);
}

wxThread::ExitCode UpdateAllThread::Entry() {
  if (!m_pm) {
    std::cerr << "UpdateAllThread: PlaylistManager is null" << std::endl;
    postEvent(m_frameHandle, EVT_UPDATE_ALL_DONE, 0);
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
    postEvent(m_frameHandle, EVT_UPDATE_ALL_DONE, 0);
    return (wxThread::ExitCode)0;
  }

  int updated = 0;
  try {
    for (int idx = 0; idx < total; ++idx) {
      if (TestDestroy())
        break;
      std::size_t i = toUpdate[static_cast<std::size_t>(idx)];
      if (validatePlaylistAccess(m_pm.get(), i)) {
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

  postEvent(m_frameHandle, EVT_UPDATE_ALL_DONE, updated);
  return (wxThread::ExitCode)0;
}
