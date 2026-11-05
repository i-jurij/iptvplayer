#pragma once

#include <functional>
#include <string>

#include <wx/string.h>

namespace xtream {

// Default User-Agent for HTTP requests to Xtream servers.
// Format: "mpv/<IPTVPLAYER_VERSION_FULL>" (backend is mpv; some
// providers whitelist by "^mpv/" prefix).
wxString DefaultUserAgent();

enum class Format {
  M3uPlusTs,
  M3uPlusHls,
};

wxString BuildM3u(const wxString &base, const wxString &user,
                  const wxString &pass, Format format);

wxString BuildXmltv(const wxString &base, const wxString &user,
                    const wxString &pass);

// Callback for ProbeAsync:
//   ok             — probe succeeded (auth OK and M3U format accepted)
//   finalUrl       — working M3U URL
//   error          — human-readable error (if !ok)
//   userInfoJson   — raw JSON of "user_info" object (auth response);
//                    empty on failure
//   serverInfoJson — raw JSON of "server_info" object (if returned);
//                    empty otherwise
using ProbeCallback = std::function<void(
    bool ok, const wxString &finalUrl, const wxString &error,
    const std::string &userInfoJson, const std::string &serverInfoJson)>;

// Probe Xtream server credentials and pick a working M3U format.
// ua — User-Agent; empty → DefaultUserAgent().
void ProbeAsync(const wxString &baseUrl, const wxString &user,
                const wxString &pass, const wxString &ua,
                ProbeCallback callback);

} // namespace xtream