#include "UpdateOneThread.h"
#include "MainFrame.h"
#include "PlaylistManager.h"
#include "ThreadUtils.h"
#include <wx/event.h>
#include <iostream>

UpdateOneThread::UpdateOneThread(MainFrame* frame,
                                 PlaylistManager* pm,
                                 std::size_t playlistIdx)
    : wxThread(wxTHREAD_DETACHED),
      m_frame(frame),
      m_pm(pm),
      m_idx(playlistIdx)
{
}

wxThread::ExitCode UpdateOneThread::Entry()
{
    bool ok = false;

    try {
        if (validatePlaylistAccess(m_pm, m_idx)) {
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

    postEvent(m_frame, EVT_UPDATE_ONE_DONE, ok ? 1 : 0,
              static_cast<int>(m_idx));
    
    return (wxThread::ExitCode)0;
}

void UpdateOneThread::postProgress(int processed, int total)
{
    if (!m_frame || m_frame->IsBeingDeleted() || m_frame->isClosing())
        return;

    wxTheApp->CallAfter([this, processed, total]() {
        if (m_frame && !m_frame->IsBeingDeleted() && !m_frame->isClosing()) {
            wxCommandEvent ev(EVT_UPDATE_PROGRESS);
            ev.SetInt(processed);
            ev.SetExtraLong(total);
            m_frame->GetEventHandler()->ProcessEvent(ev);
        }
    });
}

void UpdateOneThread::postDone(bool ok)
{
    postEvent(m_frame, EVT_UPDATE_ONE_DONE, ok ? 1 : 0);
}
