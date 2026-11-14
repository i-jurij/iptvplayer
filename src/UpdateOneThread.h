#pragma once
#include <wx/thread.h>
#include <wx/event.h>

#include <memory>

class MainFrame;
class PlaylistManager;
struct FrameHandle;

class UpdateOneThread : public wxThread
{
public:
    UpdateOneThread(MainFrame* frame,
                    std::shared_ptr<PlaylistManager> pm,
                    std::size_t playlistIdx);

protected:
    ExitCode Entry() override;

private:
    void postProgress(int processed, int total);
    void postDone(bool ok);

    std::shared_ptr<FrameHandle>     m_frameHandle;   // safe MainFrame access
    std::shared_ptr<PlaylistManager> m_pm;            // keeps manager alive
    std::size_t                      m_idx = 0;
};

