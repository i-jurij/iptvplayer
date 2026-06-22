#include "PlayerController.h"
#include "../LogControl.h"
#include "MpvBackend.h"

PlayerController::PlayerController(std::unique_ptr<IPlayerBackend> backend)
    : m_backend(std::move(backend)), m_reconcileTimer(this),
      m_reconcilePending(false),
      m_lastToggleTime(std::chrono::steady_clock::time_point::min()) {
  // Привязываем обработчик таймера
  m_reconcileTimer.Bind(wxEVT_TIMER, &PlayerController::OnReconcileTimer, this);
  m_reconcilePending.store(false);
}

PlayerController::~PlayerController() {
  if (m_reconcileTimer.IsRunning())
    m_reconcileTimer.Stop();
  m_reconcileTimer.Unbind(wxEVT_TIMER, &PlayerController::OnReconcileTimer,
                          this);
}

bool PlayerController::AttachToWindow(wxWindow *window) {
  if (!window) {
    LOG_ERROR("PlayerController: window is null");
    return false;
  }

  //LOG_DEBUG("PlayerController: attaching to window");

  m_attachedWindow = window;

  if (!m_backend)
    return false;

  return m_backend->AttachToWindow(window);
}

void PlayerController::Detach() {
  m_attachedWindow = nullptr;
  if (m_backend)
    m_backend->Detach();
}

bool PlayerController::PlayFile(const std::string &path) {
  if (!m_backend)
    return false;

  // Проверяем, прикреплен ли backend к окну
  if (!m_backend->IsAttached() && m_attachedWindow) {
    LOG_DEBUG("PlayerController: backend not attached, attaching now");
    if (!m_backend->AttachToWindow(m_attachedWindow)) {
      LOG_ERROR("PlayerController: failed to attach backend to window");
      return false;
    }
  }

  bool ok = m_backend->PlayFile(path);

  if (!ok) {
    LOG_ERROR("PlayerController: backend player failed play");
  }

  // НЕ нотифицируем Playing здесь синхронно.
  // Подтверждение Playing будет приходить от backend через SetStateCallback
  // (код 1).

  return ok;
}

bool PlayerController::PlayUrl(const std::string &url) {
  if (!m_backend)
    return false;

  // Проверяем, прикреплен ли backend к окну
  if (!m_backend->IsAttached() && m_attachedWindow) {
    LOG_DEBUG("PlayerController: backend not attached, attaching now");
    if (!m_backend->AttachToWindow(m_attachedWindow)) {
      LOG_ERROR("PlayerController: failed to attach backend to window");
      return false;
    }
  }

  bool ok = m_backend->PlayUrl(url);

  if (!ok) {
    LOG_ERROR("PlayerController: backend player failed play url");
  }

  return ok;
}

void PlayerController::Play() {
  if (!m_backend)
    return;

  auto now = std::chrono::steady_clock::now();
  if (m_lastToggleTime != std::chrono::steady_clock::time_point::min() &&
      now - m_lastToggleTime < m_debounceMs) {
    //LOG_DEBUG("PlayerController::Play debounced");
    return;
  }
  m_lastToggleTime = now;

  m_backend->Play();

  // Optimistic update
  NotifyState(PlayerState::Playing);

  // Запускаем reconciliation
  StartReconcileTimer();
}

// Pause с debounce и optimistic update
void PlayerController::Pause() {
  if (!m_backend)
    return;

  auto now = std::chrono::steady_clock::now();
  if (m_lastToggleTime != std::chrono::steady_clock::time_point::min() &&
      now - m_lastToggleTime < m_debounceMs) {
    LOG_DEBUG("PlayerController::Pause debounced");
    return;
  }
  m_lastToggleTime = now;

  m_backend->Pause();

  // Optimistic update
  NotifyState(PlayerState::Paused);

  StartReconcileTimer();
}

void PlayerController::Stop() {
  if (!m_backend)
    return;
  m_backend->Stop();
  NotifyState(PlayerState::Stopped);
}

void PlayerController::SetVolume(int volume) {
  if (!m_backend)
    return;
  m_backend->SetVolume(volume);
}

void PlayerController::SetMuted(bool muted) {
  if (!m_backend)
    return;
  m_backend->SetMuted(muted);
}

void PlayerController::SetStateCallback(StateCallback cb) {
  m_onState = std::move(cb);

  if (m_backend) {
    // Проксируем коды от backend в понятные контроллеру события.
    // 0 -> Stopped (как было)
    // 1 -> Playing (если backend решит посылать)
    // 2 -> FILE_LOADED -> преобразуем в PlayerState::FileLoaded
    m_backend->SetStateCallback([this](int code) {
      if (code == 0) {
        NotifyState(PlayerState::Stopped);
      } else if (code == 1) {
        NotifyState(PlayerState::Playing);
      } else if (code == 2) {
        NotifyState(PlayerState::FileLoaded);
      } else if (code == 3) {
        NotifyState(PlayerState::Paused);
      } else {
        LOG_DEBUG("PlayerController: backend state code %d (unhandled)", code);
      }
    });
  }
}

void PlayerController::SetInfoCallback(InfoCallback cb) {
  m_onInfo = std::move(cb);
}

void PlayerController::NotifyState(PlayerState st) {
  m_state = st;
  if (m_onState)
    m_onState(st);
}

void PlayerController::NotifyInfo(const std::string &info) {
  if (m_onInfo)
    m_onInfo(info);
}

void *PlayerController::GetMpvHandle() const {
  return m_backend ? m_backend->GetBackendHandle() : nullptr;
}

void PlayerController::ResizeEmbeddedWindow(int width, int height) {
  if (m_backend)
    m_backend->ResizeEmbeddedWindow(width, height);
}

std::string PlayerController::GetBackendName() const {
  if (!m_backend)
    return "none";
  return m_backend->GetBackendName();
}

void PlayerController::Shutdown() {
  if (m_backend)
    m_backend->Shutdown();
}

void PlayerController::SeekRelative(int seconds) {
  if (m_backend)
    m_backend->SeekRelative(seconds);
}

void PlayerController::SeekAbsolute(int percent) {
  if (m_backend)
    m_backend->SeekAbsolute(percent);
}

void PlayerController::AdjustSpeed(double delta) {
  if (m_backend)
    m_backend->AdjustSpeed(delta);
}

void PlayerController::ResetSpeed() {
  if (m_backend)
    m_backend->ResetSpeed();
}

void PlayerController::NextAudioTrack() {
  if (m_backend)
    m_backend->NextAudioTrack();
}

void PlayerController::PrevAudioTrack() {
  if (m_backend)
    m_backend->PrevAudioTrack();
}

void PlayerController::ToggleSubtitles() {
  if (m_backend)
    m_backend->ToggleSubtitles();
}

void PlayerController::NextSubtitleTrack() {
  if (m_backend)
    m_backend->NextSubtitleTrack();
}

void PlayerController::PrevSubtitleTrack() {
  if (m_backend)
    m_backend->PrevSubtitleTrack();
}

// Запуск reconciliation таймера
void PlayerController::StartReconcileTimer() {
  m_reconcilePending.store(true);
  if (m_reconcileTimer.IsRunning())
    m_reconcileTimer.Stop();
  m_reconcileTimer.Start(static_cast<int>(m_reconcileMs.count()),
                         wxTIMER_ONE_SHOT);
}

// Обработчик таймера: опрашиваем backend и корректируем состояние
void PlayerController::OnReconcileTimer(wxTimerEvent &evt) {
  m_reconcilePending.store(false);

  if (!m_backend)
    return;

  bool paused = false;
  bool ok = m_backend->GetPropertyBool("pause", paused);
  if (!ok) {
    //LOG_DEBUG("PlayerController::OnReconcileTimer: GetPropertyBool failed");
    return;
  }

  if (paused) {
    if (m_state != PlayerState::Paused) {
      NotifyState(PlayerState::Paused);
      //LOG_DEBUG("PlayerController::OnReconcileTimer: reconciled -> Paused");
    }
  } else {
    if (m_state != PlayerState::Playing) {
      NotifyState(PlayerState::Playing);
      //LOG_DEBUG("PlayerController::OnReconcileTimer: reconciled -> Playing");
    }
  }
}
