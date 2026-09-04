#pragma once

#include <atomic>
#include <wx/app.h>
#include <wx/log.h>
#include <wx/string.h>

inline std::atomic<bool> g_verboseLogging{true};

#define LOG_DEBUG(fmt, ...)                                                    \
  do {                                                                         \
    if (!g_verboseLogging.load())                                              \
      break;                                                                   \
    wxString _log_msg = wxString::Format(fmt __VA_OPT__(,) __VA_ARGS__);       \
    if (wxIsMainThread()) {                                                    \
      wxLogDebug("%s", _log_msg.c_str());                                      \
    } else {                                                                   \
      if (wxTheApp) {                                                          \
        wxTheApp->CallAfter(                                                   \
            [_log_msg]() { wxLogDebug("%s", _log_msg.c_str()); });             \
      }                                                                        \
    }                                                                          \
  } while (0)

#define LOG_INFO(fmt, ...)   wxLogInfo(fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(fmt, ...)   wxLogWarning(fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(fmt, ...)  wxLogError(fmt __VA_OPT__(,) __VA_ARGS__)
