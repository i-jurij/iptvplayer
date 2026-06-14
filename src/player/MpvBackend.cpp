#include "MpvBackend.h"
#include "../LogControl.h"

#include <X11/Xlib.h>
#include <dlfcn.h>
#include <locale.h>
#include <wx/log.h>

MpvBackend::MpvBackend() {
  LOG_DEBUG("MpvBackend::MpvBackend()");

  setlocale(LC_NUMERIC, "C");

  m_mpv = mpv_create();
  if (!m_mpv) {
    wxLogError("mpv_create failed");
    return;
  }

  // Минимальный, чистый mpv без пользовательских конфигов
  mpv_set_option_string(m_mpv, "config", "no");
  mpv_set_option_string(m_mpv, "force-window", "no");
  mpv_set_option_string(m_mpv, "terminal", "no");

  // Лог только предупреждений
  mpv_set_option_string(m_mpv, "msg-level", "warn");

  int st = mpv_initialize(m_mpv);
  LOG_DEBUG("mpv_initialize returned %d", st);
  m_eventThread = std::thread(&MpvBackend::EventLoop, this);
}

MpvBackend::~MpvBackend() {
  Shutdown();
}

bool MpvBackend::AttachToWindow(wxWindow *window) {
  if (!window) {
    LOG_ERROR("MpvBackend: window is null");
    return false;
  }

  if (!window->IsShown()) {
    LOG_ERROR("MpvBackend: window is not visible");
    return false;
  }

  // Получаем X11 XID
  Window xid = GetX11WindowID(window);
  if (!xid) {
    LOG_ERROR("MpvBackend: failed to get X11 window ID");
    return false;
  }

  LOG_DEBUG("MpvBackend: attaching to window XID=0x%lx", (unsigned long)xid);

  // Проверяем, что окно живо
  Display *disp = XOpenDisplay(nullptr);
  if (disp) {
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(disp, xid, &attrs)) {
      LOG_ERROR("MpvBackend: X11 window 0x%lx is invalid", (unsigned long)xid);
      XCloseDisplay(disp);
      return false;
    }
    LOG_DEBUG("MpvBackend: window is valid, size=%dx%d", attrs.width,
              attrs.height);
    XCloseDisplay(disp);
  }

  m_window = window;

  if (!m_mpv)
    return false;

  int64_t wid = (int64_t)xid;
  int ret = mpv_set_option(m_mpv, "wid", MPV_FORMAT_INT64, &wid);
  if (ret < 0) {
    LOG_ERROR("MpvBackend: mpv_set_option failed: %s", mpv_error_string(ret));
    return false;
  }

  LOG_DEBUG("MpvBackend: attached to WID=%lld", (long long)wid);
  return true;
}

Window MpvBackend::GetX11WindowID(wxWindow *window) {
  if (!window) {
    LOG_ERROR("ExternalPlayerBackend: window is null");
    return 0;
  }

#ifdef __WXGTK__
  void *gtk_widget = window->GetHandle();
  if (!gtk_widget) {
    LOG_ERROR("ExternalPlayerBackend: no GTK widget");
    return 0;
  }

  static auto gtk_widget_realize =
      (void (*)(void *))dlsym(RTLD_DEFAULT, "gtk_widget_realize");
  static auto gtk_widget_get_window =
      (void *(*)(void *))dlsym(RTLD_DEFAULT, "gtk_widget_get_window");
  static auto gdk_x11_window_get_xid =
      (unsigned long (*)(void *))dlsym(RTLD_DEFAULT, "gdk_x11_window_get_xid");

  if (!gtk_widget_get_window || !gdk_x11_window_get_xid) {
    LOG_ERROR("ExternalPlayerBackend: GTK symbols not found (no X11?)");
    return 0;
  }

  if (gtk_widget_realize) {
    gtk_widget_realize(gtk_widget);
  }

  void *gdk_win = gtk_widget_get_window(gtk_widget);
  if (!gdk_win) {
    LOG_ERROR("ExternalPlayerBackend: gtk_widget_get_window failed");
    return 0;
  }

  Window xid = gdk_x11_window_get_xid(gdk_win);
  if (!xid) {
    LOG_ERROR("ExternalPlayerBackend: gdk_x11_window_get_xid returned 0");
    return 0;
  }

  LOG_DEBUG("ExternalPlayerBackend: GTK widget -> X11 XID=0x%lx",
            (unsigned long)xid);
  return xid;

#else
  Window xid = (Window)(uintptr_t)window->GetHandle();
  if (!xid) {
    LOG_ERROR("ExternalPlayerBackend: cannot extract window handle");
    return 0;
  }
  return xid;
#endif
}

void MpvBackend::Detach() { m_window = nullptr; }

bool MpvBackend::PlayFile(const std::string &path) {
  if (!m_mpv)
    return false;

  const char *cmd[] = {"loadfile", path.c_str(), nullptr};
  int r = mpv_command(m_mpv, cmd);
  LOG_DEBUG("mpv loadfile '%s' -> %d", path.c_str(), r);
  return r >= 0;
}

bool MpvBackend::PlayUrl(const std::string &url) {
  if (!m_mpv)
    return false;

  // ВАРИАНТ 1: Сохранить URL в member переменную (самый надежный)
  m_lastUrl = url;

  const char *cmd[] = {"loadfile", m_lastUrl.c_str(), nullptr};
  int r = mpv_command(m_mpv, cmd);
  LOG_DEBUG("mpv loadurl '%s' -> %d", m_lastUrl.c_str(), r);
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

void MpvBackend::Shutdown() {
  if (!m_mpv)
    return;

  LOG_DEBUG("MpvBackend::Shutdown()");

  // 1. Остановить event-loop
  m_stopEventLoop = true;
  if (m_eventThread.joinable())
    m_eventThread.join();

  // 2. Мягкий выход
  mpv_command_string(m_mpv, "quit");

  // 3. Гарантированное уничтожение
  mpv_terminate_destroy(m_mpv);
  m_mpv = nullptr;
}

std::string MpvBackend::GetBackendName() const { return "mpv"; }

void *MpvBackend::GetBackendHandle() const { return m_mpv; }

void MpvBackend::ResizeEmbeddedWindow(int width, int height) {
  // Your implementation here
  // e.g., mpv_render_context_set_size() or equivalent
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
  // percent от 0 до 100
  std::string cmd = "seek " + std::to_string(percent) + " absolute-percent";
  mpv_command_string(m_mpv, cmd.c_str());
}

void MpvBackend::AdjustSpeed(double delta) {
  if (!m_mpv)
    return;
  // add speed <delta>
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

// В EventLoop(), регулярно вызываем EmitProgress:
void MpvBackend::EventLoop() {
  if (!m_mpv)
    return;

  int update_counter = 0;
  const int UPDATE_INTERVAL = 5; // каждые 500ms (0.1s * 5)

  while (!m_stopEventLoop.load()) {
    mpv_event *ev = mpv_wait_event(m_mpv, 0.1);

    // Регулярное обновление прогресса
    if (++update_counter >= UPDATE_INTERVAL) {
      update_counter = 0;
      EmitProgress();
    }

    if (ev->event_id == MPV_EVENT_NONE)
      continue;

    if (ev->event_id == MPV_EVENT_SHUTDOWN)
      break;

    switch (ev->event_id) {
    case MPV_EVENT_FILE_LOADED: {
      LOG_DEBUG("MpvBackend: FILE_LOADED");
      EmitStreamInfo();
      EmitProgress();

      // Проброс события FILE_LOADED через state callback как специальный код
      // (2).
      if (m_stateCallback) {
        m_stateCallback(2); // 2 = FILE_LOADED
      }
      break;
    }
    case MPV_EVENT_PLAYBACK_RESTART: {
      LOG_DEBUG("MpvBackend: PLAYBACK_RESTART");
      EmitStreamInfo();
      EmitProgress();

      // mpv фактически начал воспроизведение/рестарт — считаем это Playing
      if (m_stateCallback) {
        m_stateCallback(1); // 1 = Playing
      }
      break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
      auto *prop = static_cast<mpv_event_property *>(ev->data);
      if (prop && std::string(prop->name) == "video-params") {
        EmitStreamInfo();
      }
      break;
    }
    case MPV_EVENT_END_FILE: {
      LOG_DEBUG("MpvBackend: END_FILE");

      if (m_stateCallback) {
        m_stateCallback(0); // 0 = Stopped
      }
      break;
    }
    case MPV_EVENT_SHUTDOWN:
      break;
      
    default:
      break;
    }
  }
}

// Новый метод
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

  m_progressCallback(info);
}

// Вспомогательные методы (из предыдущего ответа)
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

  LOG_DEBUG("MpvBackend: StreamInfo %dx%d@%dfps | %s/%s", info.width,
            info.height, info.fps, info.videoCodec.c_str(),
            info.audioCodec.c_str());

  m_streamInfoCallback(info);
}
