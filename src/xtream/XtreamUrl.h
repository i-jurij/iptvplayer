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
// Strips query, trailing "/", and last segment if it ends with ".php".
// Subpath preserved:
//   http://host/player_api.php      → http://host
//   http://host/iptv/player_api.php → http://host/iptv
//   http://host/iptv                → http://host/iptv
//   http://host:8080/               → http://host:8080
wxString StripPhpSuffix(const wxString &url);

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
// Callback для DialProbeAsync. Отличается от ProbeCallback полем isM3u:
//   isM3u=true  — URL подтверждён как M3U по содержимому (добавить как M3U)
//   isM3u=false — Xtream auth endpoint (finalUrl = get.php URL с meta)
using DialProbeCallback =
    std::function<void(bool ok, const wxString &finalUrl, const wxString &error,
                       const std::string &userInfoJson,
                       const std::string &serverInfoJson, bool isM3u)>;

// Probe Xtream server credentials and pick a working M3U format.
// ua — User-Agent; empty → DefaultUserAgent().
void ProbeAsync(const wxString &baseUrl, const wxString &user,
                const wxString &pass, const wxString &ua,
                ProbeCallback callback);
// Sniffing-диспетчер. Классифицирует содержимое URL (M3U / XtreamAuth /
// JSON-other / HTML / BinaryStream / Empty / Error) и:
//   M3U + query-creds → probe → Xtream playlist (isM3u=false)
//   M3U без creds     → M3U playlist (isM3u=true, meta пустой)
//   XtreamAuth        → get.php → Xtream playlist
//   JsonOther/Html/BinaryStream → ok=false с понятной ошибкой
//   Empty/HttpError/NetworkError → fallback на ProbeAsync (bare server)
// ua — User-Agent; пусто → DefaultUserAgent().
void DialProbeAsync(const wxString &inputUrl, const wxString &user,
                    const wxString &pass, const wxString &ua,
                    DialProbeCallback callback);

} // namespace xtream