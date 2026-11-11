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

inline wxString FromUTF8TrPlural(const char *singular, const char *plural,
                                 unsigned n) {
  return wxGetTranslation(wxString::FromUTF8(singular),
                          wxString::FromUTF8(plural), n);
}
#define _UP(singular, plural, n) FromUTF8TrPlural(singular, plural, n)

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
