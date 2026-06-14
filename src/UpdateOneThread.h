#pragma once
#include <wx/thread.h>
#include <wx/event.h>

class MainFrame;
class PlaylistManager;

class UpdateOneThread : public wxThread
{
public:
    UpdateOneThread(MainFrame* frame,
                    PlaylistManager* pm,
                    std::size_t playlistIdx);

protected:
    ExitCode Entry() override;

private:
    /* helpers – объявлены здесь, реализованы в .cpp */
    void postProgress(int processed, int total);
    void postDone(bool ok);

    MainFrame*      m_frame = nullptr;
    PlaylistManager* m_pm   = nullptr;
    std::size_t     m_idx   = 0;
};

