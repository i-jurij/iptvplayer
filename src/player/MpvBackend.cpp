#include "MpvBackend.h"
#include "../LogControl.h"
#include "MpvGLCanvas.h"
#include "../Utils.h"

#include <wx/log.h>

#include <locale.h>

MpvBackend::MpvBackend(wxWindow *parentWindow, const MpvInitOptions &opts)
    : m_parentWindow(parentWindow) {
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
  mpv_set_option_string(m_mpv, "cache-pause", "yes");
  mpv_set_option_string(m_mpv, "msg-level", "warn");
  mpv_set_option_string(m_mpv, "osd-level", "1");
  mpv_set_option_string(m_mpv, "osd-msg1", "${?pause==yes:${osd-sym-cc}}");
  // Политика приложения: сброс per-file свойств при загрузке нового файла.
  mpv_set_option_string(m_mpv, "reset-on-next-file",
                        "video-zoom,video-aspect-override,video-rotate,"
                        "audio-delay,sub-delay,panscan,"
                        "brightness,contrast,saturation,gamma,hue");

  // !!! Don`t change! Критично для render API: vo=libmpv, без собственного окна
  mpv_set_option_string(m_mpv, "vo", "libmpv");
  mpv_set_option_string(m_mpv, "force-window", "no");
  mpv_set_option_string(m_mpv, "keep-open", "no");
  mpv_set_option_string(m_mpv, "idle", "yes");

  // gpu-context
  if (IsWaylandSession()) {
    mpv_set_option_string(m_mpv, "gpu-context", "wayland");
    //LOG_DEBUG("MpvBackend: gpu-context set to 'wayland'");
  } else if (IsX11Session()) {
    mpv_set_option_string(m_mpv, "gpu-context", "x11egl");
    //LOG_DEBUG("MpvBackend: gpu-context set to 'x11egl'");
  } else {
    mpv_set_option_string(m_mpv, "gpu-context", "auto");
    // LOG_DEBUG("MpvBackend: gpu-context set to 'auto'");
  }

  // Пользовательские опции: ставим только непустые.
  auto setOptIfSet = [this](const char *name, const std::string &v) {
    if (!v.empty())
      mpv_set_option_string(m_mpv, name, v.c_str());
  };

  setOptIfSet("hwdec", opts.hwdec);
  setOptIfSet("framedrop", opts.framedrop);
  setOptIfSet("video-sync", opts.videoSync);
  setOptIfSet("interpolation", opts.interpolation);
  setOptIfSet("deband", opts.deband);
  setOptIfSet("scale", opts.scale);
  setOptIfSet("audio-pitch-correction", opts.pitchCorrection);

  setOptIfSet("speed", opts.speed);
  setOptIfSet("audio-channels", opts.audioChannels);
  setOptIfSet("audio-device", opts.audioDevice);
  setOptIfSet("sub-scale", opts.subScale);
  setOptIfSet("sub-pos", opts.subPos);
  setOptIfSet("audio-spdif", opts.audioSpdif);

  if (opts.cacheMB > 0) {
    std::string bytes = std::to_string((long long)opts.cacheMB * 1024 * 1024);
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", bytes.c_str());
  }

  int st = mpv_initialize(m_mpv);
  // LOG_DEBUG("mpv_initialize returned %d", st);

  mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);

  mpv_observe_property(m_mpv, 1, "playback-time", MPV_FORMAT_DOUBLE);
  mpv_observe_property(m_mpv, 2, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
  mpv_observe_property(m_mpv, 3, "cache-buffering-state", MPV_FORMAT_INT64);
  mpv_observe_property(m_mpv, 4, "paused-for-cache", MPV_FORMAT_FLAG);

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

  if (m_isRecording) {
    StopRecording();
  }

  //LOG_DEBUG("MpvBackend::Shutdown()");

  ShowOsdText("", 0);
  
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

  if (ev->event_id == MPV_EVENT_SHUTDOWN) {
    if (m_isRecording) {
      StopRecording();
    }
    return;
  }

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
      int64_t val = 0;
      if (prop->data) {
        val = *static_cast<int64_t *>(prop->data);
      }
      bool paused = (val != 0);
      if (m_stateCallback) {
        m_stateCallback(paused ? 3 : 1);
      }
      EmitProgress();
      break;
    }

    if (name == "video-params") {
      EmitStreamInfo();
      break;
    }

    if (name == "playback-time" || name == "demuxer-cache-duration" ||
        name == "cache-buffering-state" || name == "paused-for-cache") {
      if (prop->format == MPV_FORMAT_NONE)
        break;
      EmitProgress();
      break;
    }

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

    if (m_isRecording) {
      StopRecording();
    }

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
  ShowOsdText("", 0);
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
  SafeGetProperty(m_mpv, "playback-time", MPV_FORMAT_DOUBLE, &pos);
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

void MpvBackend::SetPropertyString(const char *name, const std::string &value) {
  if (!m_mpv || !name)
    return;
  mpv_set_property_string(m_mpv, name, value.c_str());
}

bool MpvBackend::GetPropertyString(const char *name, std::string &out) {
  if (!m_mpv || !name)
    return false;
  char *val = nullptr;
  if (mpv_get_property(m_mpv, name, MPV_FORMAT_STRING, &val) < 0)
    return false;
  out = val ? val : "";
  mpv_free(val);
  return true;
}

bool MpvBackend::GetOptionDefault(const char *name, std::string &out) {
  if (!m_mpv || !name)
    return false;
  std::string path = std::string("option-info/") + name + "/default-value";
  char *val = nullptr;
  if (mpv_get_property(m_mpv, path.c_str(), MPV_FORMAT_STRING, &val) < 0)
    return false;
  out = val ? val : "";
  mpv_free(val);
  return true;
}

void MpvBackend::SetVideoZoom(double zoom) {
  if (!m_mpv)
    return;
  mpv_set_property_string(m_mpv, "video-zoom", std::to_string(zoom).c_str());
}

void MpvBackend::SetVideoAspect(const std::string &aspect) {
  if (!m_mpv)
    return;
  mpv_set_property_string(m_mpv, "video-aspect-override", aspect.c_str());
}

void MpvBackend::SetVideoRotate(int degrees) {
  if (!m_mpv)
    return;
  mpv_set_property_string(m_mpv, "video-rotate",
                          std::to_string(degrees).c_str());
}

void MpvBackend::SetAudioDelay(double delay) {
  if (!m_mpv)
    return;
  mpv_set_property_string(m_mpv, "audio-delay", std::to_string(delay).c_str());
}

void MpvBackend::AdjustAudioDelay(double delta) {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv,
                     ("add audio-delay " + std::to_string(delta)).c_str());
}

void MpvBackend::GetVideoZoom(double &zoom) const {
  zoom = 0.0;
  if (!m_mpv)
    return;
  mpv_get_property(m_mpv, "video-zoom", MPV_FORMAT_DOUBLE, &zoom);
}

void MpvBackend::GetVideoRotate(int &degrees) const {
  degrees = 0;
  if (!m_mpv)
    return;
  int64_t val = 0;
  if (mpv_get_property(m_mpv, "video-rotate", MPV_FORMAT_INT64, &val) >= 0)
    degrees = static_cast<int>(val);
}

void MpvBackend::ToggleVideoMirror() {
  if (!m_mpv)
    return;
  // @mirror — метка. При первом вызове добавит hflip, при повторном — удалит.
  mpv_command_string(m_mpv, "vf toggle @mirror:hflip");
}

void MpvBackend::ToggleVideoFlipVertical() {
  if (!m_mpv)
    return;
  // @vflip — метка. Первый вызов добавит vflip, повторный — удалит.
  mpv_command_string(m_mpv, "vf toggle @vflip:vflip");
}

void MpvBackend::ResetVideoFilters() {
  if (!m_mpv)
    return;
  // vf set "" перезаписывает цепочку целиком — удаляет и @mirror, и @vflip,
  // и @deint, и @sharp, и @crop. vf clr для этого не годится.
  mpv_command_string(m_mpv, "vf set \"\"");
}

void MpvBackend::SendCommand(const std::string &cmd) {
  if (!m_mpv)
    return;
  mpv_command_string(m_mpv, cmd.c_str());
}

void MpvBackend::ToggleVideoDeinterlace() {
  if (!m_mpv)
    return;
  // yadif — встроенный фильтр деинтерлейсинга.
  mpv_command_string(m_mpv, "vf toggle @deint:yadif");
}

void MpvBackend::ToggleVideoSharpen() {
  if (!m_mpv)
    return;
  // unsharp — встроенный фильтр повышения резкости.
  mpv_command_string(m_mpv, "vf toggle @sharp:unsharp");
}

std::vector<MpvBackend::AudioDevice> MpvBackend::GetAudioDevices() const {
  std::vector<AudioDevice> result;
  if (!m_mpv)
    return result;

  mpv_node node;
  if (mpv_get_property(m_mpv, "audio-device-list", MPV_FORMAT_NODE, &node) < 0)
    return result;
  if (node.format != MPV_FORMAT_NODE_ARRAY) {
    mpv_free_node_contents(&node);
    return result;
  }

  // Собираем сырой список (name, description) от mpv.
  struct RawDev {
    std::string name;
    std::string description;
  };
  std::vector<RawDev> raw;
  raw.reserve(node.u.list->num);

  mpv_node_list *list = node.u.list;
  for (int i = 0; i < list->num; ++i) {
    mpv_node *item = &list->values[i];
    if (item->format != MPV_FORMAT_NODE_MAP)
      continue;

    std::string name, description;
    mpv_node_list *il = item->u.list;
    for (int j = 0; j < il->num; ++j) {
      std::string key = il->keys[j] ? il->keys[j] : "";
      mpv_node *v = &il->values[j];
      if (key == "name" && v->format == MPV_FORMAT_STRING && v->u.string)
        name = v->u.string;
      else if (key == "description" && v->format == MPV_FORMAT_STRING &&
               v->u.string)
        description = v->u.string;
    }
    if (!name.empty())
      raw.push_back({std::move(name), std::move(description)});
  }
  mpv_free_node_contents(&node);

  // ---- Фильтр ----
  // Белый список: оставляем только реальные устройства и верхнеуровневый
  // "auto". Всё прочее (no, null, lavc/*, */auto, oss/*, sdl/*,
  // directsound/*, portaudio/*, PipeWire-эффекты и ALSA-плагины типа
  // plug/dmix/surround51/front/iec958/sysdefault/default) — отсеивается.
  //
  // На каждой платформе mpv вернёт только те бэкенды, что реально
  // доступны, поэтому лишних ветвей не будет.
  auto isTopAuto = [](const std::string &n) { return n == "auto"; };

  auto startsWith = [](const std::string &s, const char *prefix) {
    size_t len = std::strlen(prefix);
    return s.size() >= len && s.compare(0, len, prefix) == 0;
  };

  auto isWhitelisted = [&](const std::string &n) {
    if (n == "auto")
      return true;

    // Windows
    if (startsWith(n, "wasapi/"))
      return true;

    // macOS
    if (startsWith(n, "coreaudio/"))
      return true;

    // Linux — реальные бэкенды
    if (startsWith(n, "pipewire/"))
      return true;
    if (startsWith(n, "pulse/"))
      return true;
    if (startsWith(n, "jack/"))
      return true;

    // ALSA: только прямое железо (hw:X,Y) и его конвертирующая обёртка
    // (plughw:X,Y).
    if (startsWith(n, "alsa/hw:"))
      return true;
    if (startsWith(n, "alsa/plughw:"))
      return true;

    // ALSA: многоканальные аналоговые выходы (5.1 / 7.1 на три-четыре
    // мини-джека). Префиксный матч — имена могут иметь суффикс карты
    // (alsa/surround51:CARD=...,DEV=...).
    if (startsWith(n, "alsa/surround51"))
      return true;
    if (startsWith(n, "alsa/surround71"))
      return true;

    // ALSA: цифровой выход S/PDIF (он же IEC958) и HDMI. Могут иметь
    // суффикс карты. Passthrough (AC3/DTS) настраивается отдельно —
    // опцией --audio-spdif, а не выбором устройства.
    if (startsWith(n, "alsa/iec958"))
      return true;
    if (startsWith(n, "alsa/hdmi"))
      return true;

    return false;
  };

  auto backendPrefix = [](const std::string &n) -> std::string {
    auto slash = n.find('/');
    if (slash == std::string::npos)
      return "";
    return n.substr(0, slash);
  };

  // ---- Сборка результата: сначала auto, потом остальные в исходном
  //      порядке ----
  for (const auto &d : raw) {
    if (!isTopAuto(d.name))
      continue;
    result.emplace_back(d.name, d.description.empty() ? d.name : d.description);
  }

  for (const auto &d : raw) {
    if (isTopAuto(d.name) || !isWhitelisted(d.name))
      continue;

    if (startsWith(d.name, "alsa/plughw:")) {
      const std::string &desc = d.description;
      if (desc.find("HDMI") != std::string::npos ||
          desc.find("IEC958") != std::string::npos ||
          desc.find("S/PDIF") != std::string::npos ||
          desc.find("Surround") != std::string::npos) {
        continue;
      }
    }

    std::string backend = backendPrefix(d.name);
    wxString label =
        wxString::FromUTF8(d.description.empty() ? d.name : d.description);
    if (!backend.empty())
      label = "[" + wxString::FromUTF8(backend) + "] " + label;

    result.emplace_back(d.name, std::string(label.ToUTF8().data()));
  }

  return result;
}

double MpvBackend::GetAudioDelay() const {
  if (!m_mpv)
    return 0.0;
  double delay = 0.0;
  int ret = mpv_get_property(m_mpv, "audio-delay", MPV_FORMAT_DOUBLE, &delay);
  if (ret < 0)
    return 0.0;
  return delay;
}

double MpvBackend::GetSubtitleDelay() const {
  if (!m_mpv)
    return 0.0;
  double delay = 0.0;
  int ret = mpv_get_property(m_mpv, "sub-delay", MPV_FORMAT_DOUBLE, &delay);
  if (ret < 0)
    return 0.0;
  return delay;
}

double MpvBackend::GetSubtitleScale() const {
  if (!m_mpv)
    return 1.0;
  double scale = 1.0;
  int ret = mpv_get_property(m_mpv, "sub-scale", MPV_FORMAT_DOUBLE, &scale);
  if (ret < 0)
    return 1.0;
  return scale;
}

int MpvBackend::GetSubtitlePos() const {
  if (!m_mpv)
    return 100;
  int64_t pos = 100;
  int ret = mpv_get_property(m_mpv, "sub-pos", MPV_FORMAT_INT64, &pos);
  if (ret < 0)
    return 100;
  return static_cast<int>(pos);
}

static std::vector<std::pair<int, wxString>> GetTracksByType(mpv_handle *mpv,
                                                             const char *type) {
  std::vector<std::pair<int, wxString>> result;
  if (!mpv)
    return result;

  mpv_node node;
  if (mpv_get_property(mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
    return result;

  if (node.format != MPV_FORMAT_NODE_ARRAY)
    return result;

  mpv_node_list *list = node.u.list;
  for (int i = 0; i < list->num; ++i) {
    mpv_node *item = &list->values[i];
    if (item->format != MPV_FORMAT_NODE_MAP)
      continue;

    int id = -1;
    wxString label;
    bool isType = false;

    mpv_node_list *itemList = item->u.list;
    for (int j = 0; j < itemList->num; ++j) {
      char *key = itemList->keys[j]; // ключ – строка
      mpv_node *value = &itemList->values[j];

      if (!key)
        continue;
      std::string keyStr(key);

      if (keyStr == "id" && value->format == MPV_FORMAT_INT64) {
        id = (int)value->u.int64;
      } else if (keyStr == "type" && value->format == MPV_FORMAT_STRING) {
        if (value->u.string && strcmp(value->u.string, type) == 0)
          isType = true;
      } else if (keyStr == "lang" && value->format == MPV_FORMAT_STRING) {
        if (value->u.string)
          label = wxString::FromUTF8(value->u.string);
      } else if (keyStr == "title" && value->format == MPV_FORMAT_STRING) {
        if (value->u.string) {
          if (!label.empty())
            label += " - ";
          label += wxString::FromUTF8(value->u.string);
        }
      }
    }

    if (isType && id >= 0) {
      if (label.empty())
        label = wxString::Format("Track %d", id);
      result.push_back({id, label});
    }
  }

  mpv_free_node_contents(&node);
  return result;
}

std::vector<std::pair<int, wxString>> MpvBackend::GetAudioTracks() const {
  return GetTracksByType(m_mpv, "audio");
}

int MpvBackend::GetCurrentAudioTrack() const {
  if (!m_mpv)
    return -1;

  // Свойство audio-track возвращает -1/auto при автоматическом выборе.
  // Реальную текущую дорожку берём из track-list по флагу current.
  mpv_node node;
  if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
    return -1;
  if (node.format != MPV_FORMAT_NODE_ARRAY) {
    mpv_free_node_contents(&node);
    return -1;
  }

  int result = -1;
  mpv_node_list *list = node.u.list;
  for (int i = 0; i < list->num; ++i) {
    mpv_node *item = &list->values[i];
    if (item->format != MPV_FORMAT_NODE_MAP)
      continue;

    int id = -1;
    bool isAudio = false;
    bool isCurrent = false;

    mpv_node_list *il = item->u.list;
    for (int j = 0; j < il->num; ++j) {
      std::string key = il->keys[j] ? il->keys[j] : "";
      mpv_node *v = &il->values[j];

      if (key == "id" && v->format == MPV_FORMAT_INT64)
        id = static_cast<int>(v->u.int64);
      else if (key == "type" && v->format == MPV_FORMAT_STRING && v->u.string &&
               strcmp(v->u.string, "audio") == 0)
        isAudio = true;
      else if (key == "current" && v->format == MPV_FORMAT_FLAG)
        isCurrent = (v->u.flag != 0);
    }

    if (isAudio && isCurrent && id >= 0) {
      result = id;
      break;
    }
  }

  mpv_free_node_contents(&node);
  return result;
}

void MpvBackend::SetAudioTrack(int trackId) {
  if (!m_mpv)
    return;
  mpv_set_property(m_mpv, "audio-track", MPV_FORMAT_INT64, &trackId);
}

std::vector<std::pair<int, wxString>> MpvBackend::GetSubtitleTracks() const {
  return GetTracksByType(m_mpv, "sub");
}

int MpvBackend::GetCurrentSubtitleTrack() const {
  if (!m_mpv)
    return -1;

  mpv_node node;
  if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
    return -1;
  if (node.format != MPV_FORMAT_NODE_ARRAY) {
    mpv_free_node_contents(&node);
    return -1;
  }

  int result = -1;
  mpv_node_list *list = node.u.list;
  for (int i = 0; i < list->num; ++i) {
    mpv_node *item = &list->values[i];
    if (item->format != MPV_FORMAT_NODE_MAP)
      continue;

    int id = -1;
    bool isSub = false;
    bool isCurrent = false;

    mpv_node_list *il = item->u.list;
    for (int j = 0; j < il->num; ++j) {
      std::string key = il->keys[j] ? il->keys[j] : "";
      mpv_node *v = &il->values[j];

      if (key == "id" && v->format == MPV_FORMAT_INT64)
        id = static_cast<int>(v->u.int64);
      else if (key == "type" && v->format == MPV_FORMAT_STRING && v->u.string &&
               strcmp(v->u.string, "sub") == 0)
        isSub = true;
      else if (key == "current" && v->format == MPV_FORMAT_FLAG)
        isCurrent = (v->u.flag != 0);
    }

    if (isSub && isCurrent && id >= 0) {
      result = id;
      break;
    }
  }

  mpv_free_node_contents(&node);
  return result;
}

void MpvBackend::SetSubtitleTrack(int trackId) {
  if (!m_mpv)
    return;
  mpv_set_property(m_mpv, "sub-track", MPV_FORMAT_INT64, &trackId);
}

// ============================================================================
// Recording (only stream-record)
// ============================================================================

void MpvBackend::StartRecording(const std::string &filename) {
  if (!m_mpv) {
    if (m_recordStateCb)
      m_recordStateCb(false, filename, "mpv handle is null");
    return;
  }

  if (m_isRecording) {
    if (m_recordStateCb)
      m_recordStateCb(false, filename, "already recording");
    return;
  }

  int64_t idle = 0;
  if (mpv_get_property(m_mpv, "idle", MPV_FORMAT_INT64, &idle) >= 0 && idle) {
    if (m_recordStateCb)
      m_recordStateCb(false, filename, "cannot record in idle state");
    return;
  }

  int ret = mpv_set_property_string(m_mpv, "stream-record", filename.c_str());
  if (ret >= 0) {
    m_isRecording = true;
    LOG_DEBUG("Recording started: %s", filename.c_str());
    if (m_recordStateCb)
      m_recordStateCb(true, filename, "");
  } else {
    LOG_ERROR("Failed to start recording: %s (ret=%d)", mpv_error_string(ret),
              ret);
    if (m_recordStateCb)
      m_recordStateCb(false, filename, mpv_error_string(ret));
  }
}

void MpvBackend::StopRecording() {
  if (!m_mpv || !m_isRecording)
    return;

  int ret = mpv_set_property_string(m_mpv, "stream-record", "");
  if (ret >= 0) {
    m_isRecording = false;
    LOG_DEBUG("Recording stopped");
    if (m_recordStateCb)
      m_recordStateCb(false, "", "");
  } else {
    LOG_ERROR("Failed to stop recording: %s (ret=%d)", mpv_error_string(ret),
              ret);
    if (m_recordStateCb)
      m_recordStateCb(true, "", mpv_error_string(ret));
  }
}

void MpvBackend::ShowOsdText(const std::string &text, int durationMs) {
  if (!m_mpv)
    return;

  std::string escaped;
  escaped.reserve(text.size());
  for (char c : text) {
    if (c == '"' || c == '\\')
      escaped += '\\';
    escaped += c;
  }

  std::string cmd;
  if (durationMs < 0) {
    // Persistent: пока не будет перезаписано другим show-text.
    cmd = "show-text \"" + escaped + "\" 144000";
  } else if (durationMs == 0) {
    // Дефолтная длительность mpv (osd-duration).
    cmd = "show-text \"" + escaped + "\"";
  } else {
    cmd = "show-text \"" + escaped + "\" " + std::to_string(durationMs);
  }

  mpv_command_string(m_mpv, cmd.c_str());
}
