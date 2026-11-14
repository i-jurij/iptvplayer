#pragma once
#include <wx/event.h>
#include <wx/thread.h>

#include <memory>

class MainFrame;
class PlaylistManager;
struct FrameHandle;

class UpdateAllThread : public wxThread {
public:
  UpdateAllThread(MainFrame *frame, std::shared_ptr<PlaylistManager> pm);
  UpdateAllThread(MainFrame *frame, std::shared_ptr<PlaylistManager> pm,
                  const std::vector<std::size_t> &indices);

protected:
  ExitCode Entry() override;

private:
  void postProgress(int processed, int total);
  void postDone(int updated);

  std::shared_ptr<FrameHandle> m_frameHandle;   // safe MainFrame access
  std::shared_ptr<PlaylistManager> m_pm;        // keeps manager alive
  std::vector<std::size_t> m_indices;
};
