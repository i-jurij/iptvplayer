#pragma once
#include "IPlayerBackend.h"
#include "MpvGLCanvas.h"
#include <atomic>
#include <mpv/client.h>
#include <thread>

class MpvBackend : public IPlayerBackend {
public:
  explicit MpvBackend(wxWindow *parentWindow);
  ~MpvBackend() override;

  bool AttachToWindow(wxWindow *window) override;
  void Detach() override;
  bool IsAttached() const override { return m_window != nullptr; }

  bool PlayFile(const std::string &path) override;
  bool PlayUrl(const std::string &url) override;
  void Play() override;
  void Pause() override;
  void Stop() override;

  void SetVolume(int volume) override;
  void SetMuted(bool muted) override;

  void SetStreamInfoCallback(StreamInfoCallback cb) override {
    m_streamInfoCallback = std::move(cb);
  }

  void SetProgressCallback(ProgressCallback cb) override {
    m_progressCallback = std::move(cb);
  }

  void SetStateCallback(StateCallback cb) override { m_stateCallback = cb; }

  void *GetBackendHandle() const override;
  std::string GetBackendName() const override;

  void ResizeEmbeddedWindow(int width, int height) override;
  void SetFullscreen(bool fullscreen) override;

  void Shutdown() override;
  void SeekRelative(int seconds) override;
  void SeekAbsolute(int percent) override;

  void AdjustSpeed(double delta) override;
  void ResetSpeed() override;

  void NextAudioTrack() override;
  void PrevAudioTrack() override;

  void ToggleSubtitles() override;
  void NextSubtitleTrack() override;
  void PrevSubtitleTrack() override;

private:
  mpv_handle *m_mpv = nullptr;
  wxWindow *m_parentWindow = nullptr;
  
  std::string m_lastUrl;
  wxWindow *m_window = nullptr;

  ProgressCallback m_progressCallback;
  StreamInfoCallback m_streamInfoCallback;
  StateCallback m_stateCallback;

  static void WakeupCallback(void *ctx);
  void ProcessMpvEvents();
  void HandleEvent(mpv_event *ev);
  void EmitStreamInfo();
  void EmitProgress();
  std::atomic<bool> m_wakeupPending{false};
  bool m_processingEvents = false;

  // Вспомогательные для получения данных
  double GetTimePos() const;
  double GetDuration() const;
  double GetPercentPos() const;
  double GetCacheDuration() const;
  int GetCachePercent() const;
  bool IsPausedForCache() const;
};
