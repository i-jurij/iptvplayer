#pragma once

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/event.h>
#include <wx/intl.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/utils.h>
#include <wx/window.h>

#include <functional>
#include <utility>

// =============================================================================
// UTF-8-aware обёртка над _().
// _U("...") сначала декодирует литерал как UTF-8 (FromUTF8,
// locale-independent), потом пропускает через wxGetTranslation.
//
// Внимание для переводчиков: чтобы xgettext/xrc подхватывали _U,
// нужно передать ему --keyword=_U (аналогично --keyword=_).
// =============================================================================
inline wxString FromUTF8Tr(const char *s) {
  return wxGetTranslation(wxString::FromUTF8(s));
}
#define _U(s) FromUTF8Tr(s)

// =============================================================================
// Платформенный детект.
// =============================================================================
bool IsWindowsPlatform();
bool IsMacPlatform();
bool IsLinuxPlatform();
bool IsWaylandSession();
bool IsX11Session();
bool IsUIDarkTheme();
bool ShouldUseDarkColors();

// =============================================================================
// Подписчик на wxEVT_SYS_COLOUR_CHANGED.
// evt.Skip() вызывается автоматически — стандартная обработка (Refresh)
// сохраняется. Каждое окно подписывается один раз в конструкторе:
//   BindThemeWatcher(this, [this] { /* пересоздать ресурсы, invalidate кеш */
//   });
// =============================================================================
inline void BindThemeWatcher(wxWindow *w, std::function<void()> cb) {
  w->Bind(wxEVT_SYS_COLOUR_CHANGED,
          [cb = std::move(cb)](wxSysColourChangedEvent &e) {
            cb();
            e.Skip();
          });
}
