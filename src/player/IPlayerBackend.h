#pragma once
#include <string>
#include <wx/window.h>

struct StreamInfo {
  int width = 0;
  int height = 0;
  int fps = 0;
  std::string videoCodec;
  std::string audioCodec;
};

struct ProgressInfo {
  double timePos = 0.0;        // текущая позиция (сек)
  double duration = 0.0;       // длительность (сек)
  double percentPos = 0.0;     // прогресс 0-100%
  double cacheDuration = 0.0;  // буфер в сек (для потоков)
  int cachePercent = 0;        // буфер 0-100% (для потоков)
  bool pausedForCache = false; // ждёт ли буфер
};

class IPlayerBackend {
public:
  virtual ~IPlayerBackend() = default;

  using StreamInfoCallback = std::function<void(const StreamInfo &)>;
  using ProgressCallback = std::function<void(const ProgressInfo &)>;
  using StateCallback = std::function<void(int)>;
  virtual void SetStreamInfoCallback(StreamInfoCallback cb) = 0;
  virtual void SetProgressCallback(ProgressCallback cb) = 0;
  virtual void SetStateCallback(StateCallback cb) = 0;

  virtual bool AttachToWindow(wxWindow *window) = 0;
  virtual void Detach() = 0;
  virtual bool IsAttached() const = 0;

  virtual bool PlayFile(const std::string &path) = 0;
  virtual bool PlayUrl(const std::string &url) = 0;
  virtual void Play() = 0;
  virtual void Pause() = 0;
  virtual void Stop() = 0;

  virtual void SetVolume(int volume) = 0;
  virtual void SetMuted(bool muted) = 0;

  virtual void *GetBackendHandle() const = 0;
  virtual std::string GetBackendName() const = 0;

  virtual void ResizeEmbeddedWindow(int, int) {}

  virtual void SetFullscreen(bool fullscreen) = 0;

  virtual void Shutdown() = 0;
  virtual void SeekRelative(int seconds) = 0;
  virtual void SeekAbsolute(int percent) = 0;

  virtual void AdjustSpeed(double delta) = 0;
  virtual void ResetSpeed() = 0;

  virtual void NextAudioTrack() = 0;
  virtual void PrevAudioTrack() = 0;

  virtual void ToggleSubtitles() = 0;
  virtual void NextSubtitleTrack() = 0;
  virtual void PrevSubtitleTrack() = 0;
};
