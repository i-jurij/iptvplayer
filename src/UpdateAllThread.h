#pragma once
#include <wx/event.h>
#include <wx/thread.h>

class MainFrame;
class PlaylistManager;

class UpdateAllThread : public wxThread {
public:
  UpdateAllThread(MainFrame *frame, PlaylistManager *pm);
  UpdateAllThread(MainFrame *frame, PlaylistManager *pm,
                  const std::vector<std::size_t> &indices);

protected:
  ExitCode Entry() override;

private:
  void postProgress(int processed, int total);
  void postDone(int updated);

  MainFrame *m_frame = nullptr;
  PlaylistManager *m_pm = nullptr;
  std::vector<std::size_t> m_indices;
};
