#pragma once

#include <wx/intl.h>
#include <wx/string.h>

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
