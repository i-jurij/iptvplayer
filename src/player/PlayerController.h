#pragma once

#include "IPlayerBackend.h"
#include <functional>
#include <memory>
#include <string>
#include <wx/app.h>
#include <wx/window.h>

enum class PlayerState {
  Stopped,
  Playing,
  Paused,
  FileLoaded
};

class PlayerController {
public:
  void SetStreamInfoCallback(IPlayerBackend::StreamInfoCallback cb) {
    if (m_backend)
      m_backend->SetStreamInfoCallback(cb);
  }

  using StateCallback = std::function<void(PlayerState)>;
  using InfoCallback = std::function<void(const std::string &)>;
  using ProgressCallback = std::function<void(const ProgressInfo &)>;

  PlayerController(std::unique_ptr<IPlayerBackend> backend);

  bool AttachToWindow(wxWindow *window);
  void Detach();

  bool PlayFile(const std::string &path);
  bool PlayUrl(const std::string &url);

  void Play();
  void Pause();
  void Stop();

  void SetVolume(int volume);
  void SetMuted(bool muted);

  void SetStateCallback(StateCallback cb);
  void SetInfoCallback(InfoCallback cb);
  void SetProgressCallback(std::function<void(const ProgressInfo &)> cb) {
    m_progressCallback = cb;

    if (m_backend) {
      // Оборачиваем в безопасный callback с маршалингом в главный поток
      m_backend->SetProgressCallback(
          [this](const ProgressInfo &info) { NotifyProgress(info); });
    }
  }

  void *GetMpvHandle() const;
  IPlayerBackend *GetBackend() const { return m_backend.get(); }
  std::string GetBackendName() const;

  void ResizeEmbeddedWindow(int width, int height);

  PlayerState GetState() const { return m_state; }
  
  void Shutdown();
  void SeekRelative(int seconds);
  void SeekAbsolute(int percent);

  void AdjustSpeed(double delta);
  void ResetSpeed();

  void NextAudioTrack();
  void PrevAudioTrack();

  void ToggleSubtitles();
  void NextSubtitleTrack();
  void PrevSubtitleTrack();

private:
  void NotifyState(PlayerState st);
  void NotifyInfo(const std::string &info);
  void NotifyProgress(const ProgressInfo &info) {
    if (!m_progressCallback)
      return;

    // Маршалим в главный UI поток
    wxTheApp->CallAfter([this, info]() {
      if (m_progressCallback) {
        m_progressCallback(info);
      }
    });
  }

  std::unique_ptr<IPlayerBackend> m_backend;
  wxWindow *m_attachedWindow = nullptr;

  PlayerState m_state = PlayerState::Stopped;
  StateCallback m_onState;
  InfoCallback m_onInfo;
  ProgressCallback m_onProgress;
  std::function<void(const ProgressInfo &)> m_progressCallback;
};
