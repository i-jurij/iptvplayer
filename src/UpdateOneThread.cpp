#include "UpdateOneThread.h"
#include "MainFrame.h"
#include "PlaylistManager.h"
#include "ThreadUtils.h"
#include <wx/event.h>
#include <iostream>

UpdateOneThread::UpdateOneThread(MainFrame* frame,
                                 std::shared_ptr<PlaylistManager> pm,
                                 std::size_t playlistIdx)
    : wxThread(wxTHREAD_DETACHED),
      m_frameHandle(frame ? frame->getFrameHandle() : nullptr),
      m_pm(std::move(pm)),
      m_idx(playlistIdx)
{
}

wxThread::ExitCode UpdateOneThread::Entry()
{
    bool ok = false;

    try {
        if (validatePlaylistAccess(m_pm.get(), m_idx)) {
            ok = (m_pm->updatePlaylist(m_idx) == ErrorCode::OK);
            if (!ok) {
                std::cerr << "UpdateOneThread: Failed to update playlist at index " << m_idx << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "UpdateOneThread exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "UpdateOneThread unknown exception" << std::endl;
    }

    postEvent(m_frameHandle, EVT_UPDATE_ONE_DONE, ok ? 1 : 0,
              static_cast<int>(m_idx));

    return (wxThread::ExitCode)0;
}

void UpdateOneThread::postProgress(int processed, int total)
{
    postUpdateProgress(m_frameHandle, processed, total);
}

void UpdateOneThread::postDone(bool ok)
{
    postEvent(m_frameHandle, EVT_UPDATE_ONE_DONE, ok ? 1 : 0);
}
