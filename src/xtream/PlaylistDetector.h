#pragma once

#include <functional>
#include <string>

#include <wx/string.h>

namespace xtream {

// Классификация содержимого URL по первым байтам + Content-Type.
// Используется для диспетчеризации в Add Xtream (sniffing):
// M3U → addPlaylistFromUrl as-is;
// XtreamAuth → probe (get.php) → addPlaylistFromUrl isXtream=true;
// остальное → reject с понятным сообщением.
enum class DetectionKind {
  Unknown,
  M3U,          // #EXTM3U в первых 200 байт (BOM-aware)
  XtreamAuth,   // JSON с user_info.auth == true (извлекает
                // userInfoJson/serverInfoJson)
  JsonOther,    // JSON без user_info.auth (categories, vod_info, ошибки)
  Html,         // <html / <!doctype / <script или Content-Type: text/html
  BinaryStream, // video/*, audio/*, octet-stream или magic bytes
                // (TS/MP4/RIFF/MKV)
  Empty,        // 0 байт (bare server на "/" — это норма)
  NetworkError, // timeout / refused / CURLE_*
  HttpError,    // 4xx / 5xx
};

struct Detection {
  DetectionKind kind = DetectionKind::Unknown;
  long httpCode = 0;
  std::string contentType;
  std::string userInfoJson;   // заполнено при kind == XtreamAuth
  std::string serverInfoJson; // заполнено при kind == XtreamAuth (если есть)
  std::string error;          // заполнено при ошибке
  wxString finalUrl;          // после редиректов
};

using DetectCallback = std::function<void(const Detection &)>;

// Async GET Range: bytes=0-16383, таймаут 8 сек, 16 KB prefix.
// Не делает probe, не строит get.php, не парсит M3U — только классификация.
// cb всегда вызывается на main thread (через wxTheApp->CallAfter).
void DetectContentAsync(const wxString &url, const wxString &userAgent,
                        DetectCallback cb);

} // namespace xtream
