#include "Utils_Platform.h"

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/utils.h>

bool IsWindowsPlatform() {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

bool IsMacPlatform() {
#if defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

bool IsLinuxPlatform() {
#if defined(__linux__)
  return true;
#else
  return false;
#endif
}

bool IsWaylandSession() {
#if defined(__linux__)
  wxString session;
  if (wxGetEnv("XDG_SESSION_TYPE", &session)) {
    session.MakeLower();
    if (session == "wayland")
      return true;
  }
  wxString waylandDisplay;
  if (wxGetEnv("WAYLAND_DISPLAY", &waylandDisplay)) {
    if (!waylandDisplay.IsEmpty())
      return true;
  }
  return false;
#else
  return false;
#endif
}

bool IsX11Session() {
#if defined(__linux__)
  wxString display;
  if (wxGetEnv("DISPLAY", &display)) {
    return !display.IsEmpty();
  }
  return false;
#else
  return false;
#endif
}

bool IsUIDarkTheme() {
  wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
  // Взвешенная яркость (Rec. 601)
  int brightness = (bg.Red() * 299 + bg.Green() * 587 + bg.Blue() * 114) / 1000;
  return brightness < 128;
}

bool ShouldUseDarkColors() {
#if defined(__WXMSW__)
  return IsUIDarkTheme();
#else
  return wxSystemSettings::GetAppearance().IsDark() || IsUIDarkTheme();
#endif
}
