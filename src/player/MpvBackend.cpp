#include "MpvBackend.h"
#include "../LogControl.h"
#include "MpvGLCanvas.h"
#include "../Utils.h"

#include <wx/log.h>

#include <locale.h>


MpvBackend::MpvBackend(wxWindow *parentWindow) : m_parentWindow(parentWindow) {
  //LOG_DEBUG("MpvBackend::MpvBackend()");

  setlocale(LC_NUMERIC, "C");
  unsetenv("WINDOWID");

  m_mpv = mpv_create();
  if (!m_mpv) {
    wxLogError("mpv_create failed");
    return;
  }

  // Базовые опции
  mpv_set_option_string(m_mpv, "config", "no");
  mpv_set_option_string(m_mpv, "terminal", "no");
  mpv_set_option_string(m_mpv, "msg-level", "all");

  // Критично для render API: vo=libmpv, без собственного окна
  mpv_set_option_string(m_mpv, "vo", "libmpv");
  mpv_set_option_string(m_mpv, "force-window", "no");
  mpv_set_option_string(m_mpv, "keep-open", "no");
  mpv_set_option_string(m_mpv, "idle", "yes");

  // gpu-context оставляем как было
  if (IsWaylandSession()) {
    mpv_set_option_string(m_mpv, "gpu-context", "wayland");
    //LOG_DEBUG("MpvBackend: gpu-context set to 'wayland'");
  } else if (IsX11Session()) {
    mpv_set_option_string(m_mpv, "gpu-context", "x11egl");
    //LOG_DEBUG("MpvBackend: gpu-context set to 'x11egl'");
  } else {
    mpv_set_option_string(m_mpv, "gpu-context", "auto");
    //LOG_DEBUG("MpvBackend: gpu-context set to 'auto'");
  }

  int st = mpv_initialize(m_mpv);
  //LOG_DEBUG("mpv_initialize returned %d", st);

  mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
  //LOG_DEBUG("MpvBackend: observing property 'pause'");

  // Enable tick events for regular progress updates
  mpv_request_event(m_mpv, MPV_EVENT_TICK, 1);
  //LOG_DEBUG("MpvBackend: requested MPV_EVENT_TICK");

  // ВАЖНО: вместо отдельного потока — wakeup callback
  mpv_set_wakeup_callback(m_mpv, &MpvBackend::WakeupCallback, this);
}

MpvBackend::~MpvBackend() { Shutdown(); }

bool MpvBackend::AttachToWindow(wxWindow *window) {
  // For GL canvas rendering we do not set WID here.
  // Keep pointer to window for potential UI-related queries (focus/fullscreen).
  if (!window) {
    LOG_ERROR("MpvBackend: AttachToWindow called with null window");
    return false;
  }

  m_window = window;
  //LOG_DEBUG("MpvBackend: AttachToWindow stored wxWindow pointer (no WID)");

  // Если mpv уже инициализирован и окно — MpvGLCanvas, передаём mpv_handle
  if (m_mpv) {
    MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(window);
    if (canvas) {
      canvas->SetMpvHandle(m_mpv);
      //LOG_DEBUG("MpvBackend: passed mpv_handle to MpvGLCanvas (%p)",
        //        (void *)canvas);
    }
  }

  return true;
}

void MpvBackend::Detach() { m_window = nullptr; }

void MpvBackend::Shutdown() {
  if (!m_mpv)
    return;

  //LOG_DEBUG("MpvBackend::Shutdown()");

  // 0. Сначала отцепляем canvas и уничтожаем render_context
  if (m_window) {
    MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(m_window);
    if (canvas) {
      canvas->SetMpvHandle(nullptr); // внутри DestroyRenderContext()
      //LOG_DEBUG(
        //  "MpvBackend::Shutdown: cleared mpv_handle from MpvGLCanvas (%p)",
          //(void *)canvas);
    }
  }

  // 1. Сбрасываем wakeup callback
  mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);

  // 2. Мягкий выход
  mpv_command_string(m_mpv, "quit");

  // 3. Гарантированное уничтожение
  mpv_terminate_destroy(m_mpv);
  m_mpv = nullptr;
}

bool MpvBackend::PlayFile(const std::string &path) {
  if (!m_mpv)
    return false;

  const char *cmd[] = {"loadfile", path.c_str(), nullptr};
  int r = mpv_command(m_mpv, cmd);
  //LOG_DEBUG("mpv loadfile '%s' -> %d", path.c_str(), r);
  return r >= 0;
}

void MpvBackend::WakeupCallback(void *ctx) {
  MpvBackend *self = static_cast<MpvBackend *>(ctx);
  if (!self || !self->m_mpv)
    return;

  // Коалесцируем вызовы: пока один ProcessMpvEvents уже запланирован/идёт —
  // новые не ставим.
  bool expected = false;
  if (!self->m_wakeupPending.compare_exchange_strong(expected, true)) {
    // уже есть запланированный вызов
    return;
  }

  wxTheApp->CallAfter([self]() { self->ProcessMpvEvents(); });
}

void MpvBackend::ProcessMpvEvents() {
  if (!m_mpv)
    return;

  m_wakeupPending.store(false, std::memory_order_relaxed);

  if (m_processingEvents)
    return;

  m_processingEvents = true;

  //LOG_DEBUG("ProcessMpvEvents: enter (thread=%p)", (void *)wxThread::This());

  while (true) {
    mpv_event *ev = mpv_wait_event(m_mpv, 0);
    if (!ev || ev->event_id == MPV_EVENT_NONE)
      break;

    //LOG_DEBUG("ProcessMpvEvents: event_id=%d", ev->event_id);
    HandleEvent(ev);
  }

  //LOG_DEBUG("ProcessMpvEvents: leave");
  m_processingEvents = false;
}

void MpvBackend::HandleEvent(mpv_event *ev) {
  if (!ev)
    return;

  if (ev->event_id == MPV_EVENT_SHUTDOWN)
    return;

  switch (ev->event_id) {
  case MPV_EVENT_FILE_LOADED: {
    //LOG_DEBUG("MpvBackend: FILE_LOADED");
    EmitStreamInfo();
    EmitProgress();

    // ЯВНО снимаем паузу на всякий случай
    mpv_set_property_string(m_mpv, "pause", "no");
    //LOG_DEBUG("MpvBackend: pause=no after FILE_LOADED");

    if (m_stateCallback) {
      m_stateCallback(2); // 2 = FILE_LOADED
    }
    break;
  }
  case MPV_EVENT_PLAYBACK_RESTART: {
    //LOG_DEBUG("MpvBackend: PLAYBACK_RESTART");
    EmitStreamInfo();
    EmitProgress();
    if (m_stateCallback) {
      m_stateCallback(1); // 1 = Playing
    }
    break;
  }
  case MPV_EVENT_PROPERTY_CHANGE: {
    auto *prop = static_cast<mpv_event_property *>(ev->data);
    if (!prop || !prop->name)
      break;

    std::string name(prop->name);

    if (name == "pause") {
      // Для MPV_FORMAT_FLAG prop->data приходит как int64_t
      int64_t val = 0;
      if (prop->data) {
        // prop->data указывает на значение в формате mpv, безопасно читать как
        // int64_t
        val = *static_cast<int64_t *>(prop->data);
      }
      bool paused = (val != 0);
      //LOG_DEBUG("MpvBackend: property change pause=%d", paused ? 1 : 0);
      if (m_stateCallback) {
        m_stateCallback(paused ? 3 : 1); // 3 = Paused, 1 = Playing
      }
      EmitProgress(); // Update progress on pause/play
      break;
    }

    if (prop && std::string(prop->name) == "video-params") {
      EmitStreamInfo();
    }
    break;
  }
  case MPV_EVENT_TICK: {
    // Regular progress update during playback
    EmitProgress();
    break;
  }
  case MPV_EVENT_END_FILE: {
    auto *end = static_cast<mpv_event_end_file *>(ev->data);
    if (end && end->reason == MPV_END_FILE_REASON_ERROR) {
      LOG_ERROR("MpvBackend: END_FILE with error code: %d", end->error);
      if (m_stateCallback) {
        m_stateCallback(4); // 4 = Error
      }
    } else {
      // Нормальное завершение (включая остановку пользователем)
      if (m_stateCallback) {
        m_stateCallback(0); // 0 = Stopped
      }
    }
    EmitProgress();
    break;
  }
  default:
    break;
  }
}

bool MpvBackend::PlayUrl(const std::string &url) {
  if (!m_mpv)
    return false;

  m_lastUrl = url;
  const char *cmd[] = {"loadfile", m_lastUrl.c_str(), nullptr};
  int r = mpv_command(m_mpv, cmd);
  //LOG_DEBUG("mpv loadurl '%s' -> %d", m_lastUrl.c_str(), r);
  return r >= 0;
}

void MpvBackend::Play() {
  if (!m_mpv)
    return;
  mpv_set_property_string(m_mpv, "pause", "no");
}

void MpvBackend::Pause() {
  if (!m_mpv)
    return;
  mpv_set_property_string(m_mpv, "pause", "yes");
}

void MpvBackend::Stop() {
  if (!m_mpv)
    return;
  const char *cmd[] = {"stop", nullptr};
  mpv_command(m_mpv, cmd);
}

void MpvBackend::SetVolume(int volume) {
  if (!m_mpv)
    return;
  int64_t v = volume;
  mpv_set_property(m_mpv, "volume", MPV_FORMAT_INT64, &v);
}

void MpvBackend::SetMuted(bool muted) {
  if (!m_mpv)
    return;
  mpv_set_property_string(m_mpv, "mute", muted ? "yes" : "no");
}

void MpvBackend::SetFullscreen(bool) {
  // fullscreen делает wxWidgets
}

std::string MpvBackend::GetBackendName() const { return "mpv"; }

void *MpvBackend::GetBackendHandle() const { return m_mpv; }

void MpvBackend::ResizeEmbeddedWindow(int width, int height) {
  // No-op for GL canvas renderer; canvas handles viewport and FBO sizes.
}

void MpvBackend::SeekRelative(int seconds) {
  if (!m_mpv)
    return;
  std::string cmd = "seek " + std::to_string(seconds) + " relative";
  mpv_command_string(m_mpv, cmd.c_str());
}

void MpvBackend::SeekAbsolute(int percent) {
  if (!m_mpv)
    return;
  std::string cmd = "seek " + std::to_string(percent) + " absolute-percent";
  mpv_command_string(m_mpv, cmd.c_str());
}

void MpvBackend::AdjustSpeed(double delta) {
  if (!m_mpv)
    return;
  std::string cmd = "add speed " + std::to_string(delta);
  mpv_command_string(m_mpv, cmd.c_str());
}

void MpvBackend::ResetSpeed() {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv, "set speed 1.0");
}

void MpvBackend::NextAudioTrack() {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv, "cycle audio");
}

void MpvBackend::PrevAudioTrack() {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv, "cycle audio down");
}

void MpvBackend::ToggleSubtitles() {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv, "cycle sub");
}

void MpvBackend::NextSubtitleTrack() {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv, "cycle sub");
}

void MpvBackend::PrevSubtitleTrack() {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv, "cycle sub down");
}

void MpvBackend::EmitProgress() {
  if (!m_progressCallback) {
    return;
  }

  ProgressInfo info;
  info.timePos = GetTimePos();
  info.duration = GetDuration();
  info.percentPos = GetPercentPos();
  info.cacheDuration = GetCacheDuration();
  info.cachePercent = GetCachePercent();
  info.pausedForCache = IsPausedForCache();

  //LOG_DEBUG("EmitProgress: t=%.2f dur=%.2f %%=%.1f cache=%.1fs %d%% "
    //        "pausedForCache=%d",
      //      info.timePos, info.duration, info.percentPos, info.cacheDuration,
        //    info.cachePercent, info.pausedForCache ? 1 : 0);

  m_progressCallback(info);
}

template <typename T>
static bool SafeGetProperty(mpv_handle *mpv, const char *prop, mpv_format fmt,
                            T *out) {
  if (!mpv || !prop || !out)
    return false;
  int ret = mpv_get_property(mpv, prop, fmt, out);
  return ret >= 0;
}

double MpvBackend::GetTimePos() const {
  if (!m_mpv)
    return 0.0;
  double pos = 0.0;
  SafeGetProperty(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
  return pos < 0 ? 0.0 : pos;
}

double MpvBackend::GetDuration() const {
  if (!m_mpv)
    return 0.0;
  double dur = 0.0;
  SafeGetProperty(m_mpv, "duration", MPV_FORMAT_DOUBLE, &dur);
  return dur < 0 ? 0.0 : dur;
}

double MpvBackend::GetPercentPos() const {
  if (!m_mpv)
    return 0.0;
  double percent = 0.0;
  SafeGetProperty(m_mpv, "percent-pos", MPV_FORMAT_DOUBLE, &percent);
  return std::clamp(percent, 0.0, 100.0);
}

double MpvBackend::GetCacheDuration() const {
  if (!m_mpv)
    return 0.0;
  double cache_sec = 0.0;
  SafeGetProperty(m_mpv, "demuxer-cache-duration", MPV_FORMAT_DOUBLE,
                  &cache_sec);
  return cache_sec < 0 ? 0.0 : cache_sec;
}

int MpvBackend::GetCachePercent() const {
  if (!m_mpv)
    return 0;
  int64_t cache_pct = 0;
  SafeGetProperty(m_mpv, "cache-buffering-state", MPV_FORMAT_INT64, &cache_pct);
  return std::clamp(static_cast<int>(cache_pct), 0, 100);
}

bool MpvBackend::IsPausedForCache() const {
  if (!m_mpv)
    return false;
  int64_t paused = 0;
  SafeGetProperty(m_mpv, "paused-for-cache", MPV_FORMAT_INT64, &paused);
  return paused != 0;
}

void MpvBackend::EmitStreamInfo() {
  if (!m_streamInfoCallback) {
    LOG_DEBUG("MpvBackend: m_streamInfoCallback is NULL");
    return;
  }
  
    StreamInfo info;

    int64_t w = 0, h = 0;
    mpv_get_property(m_mpv, "video-params/w", MPV_FORMAT_INT64, &w);
    mpv_get_property(m_mpv, "video-params/h", MPV_FORMAT_INT64, &h);
    info.width = static_cast<int>(w);
    info.height = static_cast<int>(h);

    double fps = 0.0;
    mpv_get_property(m_mpv, "container-fps", MPV_FORMAT_DOUBLE, &fps);
    info.fps = static_cast<int>(fps);

    char *vcodec = nullptr;
    mpv_get_property(m_mpv, "video-codec", MPV_FORMAT_STRING, &vcodec);
    if (vcodec) {
      info.videoCodec = std::string(vcodec);
      mpv_free(vcodec);
    }

    char *acodec = nullptr;
    mpv_get_property(m_mpv, "audio-codec-name", MPV_FORMAT_STRING, &acodec);
    if (acodec) {
      info.audioCodec = std::string(acodec);
      mpv_free(acodec);
    }

    //LOG_DEBUG("MpvBackend: StreamInfo %dx%d@%dfps | %s/%s", info.width,
      //        info.height, info.fps, info.videoCodec.c_str(),
        //      info.audioCodec.c_str());

    m_streamInfoCallback(info);
}

bool MpvBackend::GetPropertyBool(const char *name, bool &out) {
  if (!m_mpv || !name)
    return false;

  int64_t val = 0;
  int ret = mpv_get_property(m_mpv, name, MPV_FORMAT_INT64, &val);
  if (ret < 0)
    return false;

  out = (val != 0);
  return true;
}

