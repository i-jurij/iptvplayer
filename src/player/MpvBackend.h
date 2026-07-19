#pragma once
#include "IPlayerBackend.h"

#include <atomic>
#include <mpv/client.h>

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
  bool GetPropertyBool(const char *name, bool &out) override;

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

  void SetVideoZoom(double zoom) override;
  void SetVideoAspect(const std::string &aspect) override;
  void SetVideoRotate(int degrees) override;
  void SetAudioDelay(double delay) override;
  double GetAudioDelay() const override;
  void AdjustAudioDelay(double delta) override;
  void GetVideoZoom(double &zoom) const override;
  void GetVideoRotate(int &degrees) const override;
  void ToggleVideoMirror() override;
  void ResetVideoFilters() override;
  std::vector<std::pair<int, wxString>> GetAudioTracks() const override;
  int GetCurrentAudioTrack() const override;
  void SetAudioTrack(int trackId) override;
  std::vector<std::pair<int, wxString>> GetSubtitleTracks() const override;
  int GetCurrentSubtitleTrack() const override;
  void SetSubtitleTrack(int trackId) override;

  // --- методы записи ---
  void SetRecordStateCallback(RecordStateCallback cb) override {
    m_recordStateCb = std::move(cb);
  }
  void StartRecording(const std::string &filename) override;
  void StopRecording() override;
  bool IsRecording() const override { return m_isRecording; }

private:
  bool m_isRecording = false;
  RecordStateCallback m_recordStateCb;

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
