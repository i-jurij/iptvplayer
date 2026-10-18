#include "IconManager.h"
#include "Utils.h"
#include "Utils_FsPath.h"
#include "LogControl.h"

#include <curl/curl.h>
#include <filesystem>
#include <webp/decode.h> // WebPGetInfo, WebPDecodeRGBA, WebPFree

#include <wx/app.h>
#include <wx/bmpbndl.h> // wxBitmapBundle::FromSVG
#include <wx/dir.h>     // wxDir, wxDIR_FILES
#include <wx/ffile.h>   // wxFFile
#include <wx/filename.h>
#include <wx/image.h> // wxImage
#include <wx/log.h>
#include <wx/mstream.h> // wxMemoryInputStream, wxMemoryOutputStream
#include <wx/stdpaths.h>

#include <atomic>
#include <mutex>
#include <set>

// ============================================================================
//  SafeCallReady — вызывает callback строго в UI-потоке
// ============================================================================
void IconManager::SafeCallReady(IconCallback cb, const wxBitmap &bmp) {
  if (!cb || shuttingDown.load())
    return;

  if (!wxTheApp) {
    return;
  }

  try {
    wxTheApp->CallAfter([cb, bmp]() {
      if (!shuttingDown.load()) {
        try {
          cb(bmp);
        } catch (...) {
          // swallow exceptions from UI callback to avoid crashing the app
        }
      }
    });
  } catch (...) {
    // In case CallAfter itself throws for some reason, avoid crashing.
  }
}

// ============================================================================
//  GetCacheDir / GetIconPath / GetSvgPath
// ============================================================================

std::string IconManager::GetCacheDir() {
  static std::string cacheDir;
  static std::once_flag initFlag;

  std::call_once(initFlag, [] {
    wxString base;

    // --- Основной путь: кроссплатформенный API wx ---
    // GetUserDir(Dir_Cache) возвращает:
    //   Linux   -> ~/.cache
    //   Windows -> C:\Users\username\AppData\Local
    //   macOS   -> ~/Library/Caches
    base = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Cache);

    // На Windows GetUserDir(Dir_Cache) возвращает только AppData\Local,
    // поэтому добавляем подкаталог приложения.
#if defined(__WXMSW__)
    if (!base.IsEmpty())
      base += "\\iptvplayer\\Cache";
#endif

    // --- Fallback 1: старый проверенный код (актуален для Linux) ---
    if (base.IsEmpty()) {
      wxString home = wxGetenv("XDG_CACHE_HOME");
      if (home.IsEmpty() || !wxFileName(home).IsAbsolute()) {
        home = wxFileName::GetHomeDir() + "/.cache";
        base = home + "/iptvplayer";
      }
      if (!base.IsEmpty())
        LOG_WARN("IconManager: GetUserDir(Dir_Cache) failed, using manual "
                     "fallback: %s",
                     base);
    }

    // --- Fallback 2: временная директория ---
    if (base.IsEmpty()) {
      base = wxFileName::GetTempDir() + wxFILE_SEP_PATH + "iptvplayer-cache";
      LOG_WARN("IconManager: using temporary cache directory: %s", base);
    }

    if (!wxFileName::DirExists(base)) {
      if (!wxFileName::Mkdir(base, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        LOG_WARN("IconManager: could not create cache directory %s", base);
      }
    }

    cacheDir = std::string(base.mb_str(wxConvUTF8));
  });

  return cacheDir;
}

std::string IconManager::GetIconPath(const std::string &playlist,
                                     const std::string &channel) {
  namespace fs = std::filesystem;

  std::string safePlaylist =
      NormalizeFileNameForDisk(playlist.empty() ? "default" : playlist);
  std::string safeChannel =
      NormalizeFileNameForDisk(channel.empty() ? "channel" : channel);

  fs::path base = ToFsPath(GetCacheDir()) / "icons" / safePlaylist / "original";
  std::error_code ec;
  if (!fs::exists(base)) {
    if (!fs::create_directories(base, ec)) {
      wxLogError("IconManager: cannot create directory '%s' (%s)",
                 base.string(), ec.message());
      // continue — return path anyway so caller can attempt to write and see
      // error
    }
  }

  fs::path file = base / (safeChannel + ".webp");
  return file.string();
}

std::string IconManager::GetSvgPath(const std::string &playlist,
                                    const std::string &channel) {
  namespace fs = std::filesystem;

  std::string safePlaylist =
      NormalizeFileNameForDisk(playlist.empty() ? "default" : playlist);
  std::string safeChannel =
      NormalizeFileNameForDisk(channel.empty() ? "channel" : channel);

  fs::path base = ToFsPath(GetCacheDir()) / "icons" / safePlaylist / "original";
  std::error_code ec;
  if (!fs::exists(base)) {
    if (!fs::create_directories(base, ec)) {
      wxLogError("IconManager: cannot create directory '%s' (%s)",
                 base.string(), ec.message());
    }
  }

  fs::path file = base / (safeChannel + ".svg");
  return file.string();
}

// ============================================================================
//  RenderSvgAsync — рендер SVG в bitmap (оригинальный размер)
// ============================================================================
void IconManager::RenderSvgAsync(const std::string &svgText, IconCallback cb) {
  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  if (!wxTheApp) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  EnqueueTask([svgText, cb]() {
    wxString wxSvg = wxString::FromUTF8(svgText);
    const wxSize baseSize(256, 256);

    wxTheApp->CallAfter([cb, wxSvg, baseSize]() {
      if (shuttingDown.load()) {
        SafeCallReady(cb, wxNullBitmap);
        return;
      }

      std::string svgUtf8(wxSvg.ToUTF8().data());
      wxBitmapBundle bundle =
          wxBitmapBundle::FromSVG(svgUtf8.c_str(), baseSize);
      if (!bundle.IsOk()) {
        SafeCallReady(cb, wxNullBitmap);
        return;
      }

      wxBitmap bmp = bundle.GetBitmap(baseSize);
      if (!bmp.IsOk() || bmp.GetWidth() <= 1 || bmp.GetHeight() <= 1) {
        SafeCallReady(cb, wxNullBitmap);
        return;
      }

      SafeCallReady(cb, bmp);
    });
  });
}

// ============================================================================
//  DecodePngAsync — декод PNG из памяти → bitmap
// ============================================================================
void IconManager::DecodePngAsync(const std::vector<unsigned char> &data,
                                 IconCallback cb) {
  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  EnqueueTask([data, cb]() {
    wxMemoryInputStream stream(data.data(), data.size());
    wxImage img(stream, wxBITMAP_TYPE_PNG);

    if (!img.IsOk() || img.GetWidth() <= 1 || img.GetHeight() <= 1) {
      SafeCallReady(cb, wxNullBitmap);
      return;
    }

    // Safety: ensure wxTheApp exists before calling CallAfter
    if (wxTheApp) {
      wxTheApp->CallAfter([cb, img]() {
        wxBitmap bmp(img);
        SafeCallReady(cb, bmp);
      });
    } else {
      // GUI not available — return empty result
      SafeCallReady(cb, wxNullBitmap);
    }
  });
}
// ============================================================================
//  DecodeWebpAsync — декод WebP из памяти → bitmap
// ============================================================================
void IconManager::DecodeWebpAsync(const std::vector<unsigned char> &data,
                                  IconCallback cb) {
  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  EnqueueTask([data, cb]() {
    int w = 0, h = 0;

    if (!WebPGetInfo(data.data(), data.size(), &w, &h) || w <= 0 || h <= 0) {
      SafeCallReady(cb, wxNullBitmap);
      return;
    }

    uint8_t *decoded = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
    if (!decoded) {
      SafeCallReady(cb, wxNullBitmap);
      return;
    }

    wxImage img(w, h);
    unsigned char *rgb = img.GetData();
    unsigned char *alpha = new unsigned char[w * h];

    for (int i = 0; i < w * h; ++i) {
      rgb[i * 3 + 0] = decoded[i * 4 + 0];
      rgb[i * 3 + 1] = decoded[i * 4 + 1];
      rgb[i * 3 + 2] = decoded[i * 4 + 2];
      alpha[i] = decoded[i * 4 + 3];
    }

    img.SetAlpha(alpha, true);
    WebPFree(decoded);

    if (wxTheApp) {
      wxTheApp->CallAfter([cb, img]() {
        wxBitmap bmp(img);
        if (!bmp.IsOk() || bmp.GetWidth() <= 1 || bmp.GetHeight() <= 1)
          SafeCallReady(cb, wxNullBitmap);
        else
          SafeCallReady(cb, bmp);
      });
    } else {
      SafeCallReady(cb, wxNullBitmap);
    }
  });
}

// ============================================================================
//  DeletePlaylistIcons / CleanupUnusedIcons
// ============================================================================
ErrorCode IconManager::DeletePlaylistIcons(const std::string &playlist) {
  // **Используем точно ту же форму построения пути, что и в вашем оригинале**
  wxString base = wxString::FromUTF8(GetCacheDir()) + "/icons/" +
                  wxString::FromUTF8(playlist);

  // Безопасность: базовый icons каталог (без playlist)
  wxString iconsBase = wxString::FromUTF8(GetCacheDir()) + "/icons";

  if (playlist.empty()) {
    LOG_WARN(
        "IconManager::DeletePlaylistIcons: empty playlist name, refusing to "
        "delete specific folder. Use DeleteAllIcons() to remove everything.");
    return ErrorCode::InvalidIndex;
  }

  if (!IsSafeSubpath(iconsBase, base)) {
    wxLogError("IconManager::DeletePlaylistIcons: unsafe path detected: %s",
               base);
    return ErrorCode::UnsafePath;
  }

  if (!wxDirExists(base)) {
    wxLogInfo("IconManager::DeletePlaylistIcons: directory not found: %s",
              base);
    return ErrorCode::FileNotFound;
  }

  bool ok = wxFileName::Rmdir(base, wxPATH_RMDIR_RECURSIVE);
  if (!ok) {
    if (!wxDirExists(base)) {
      wxLogInfo("IconManager::DeletePlaylistIcons: directory disappeared "
                "during operation: %s",
                base);
      return ErrorCode::FileNotFound;
    }
    wxLogError("IconManager::DeletePlaylistIcons failed to remove: %s", base);
    return ErrorCode::IoError;
  }

  wxLogInfo("IconManager::DeletePlaylistIcons removed: %s", base);
  return ErrorCode::OK;
}

ErrorCode IconManager::DeleteAllIcons() {
  // **Не меняем расположение каталогов — используем вашу GetCacheDir()**
  wxString iconsBase = wxString::FromUTF8(GetCacheDir()) + "/icons";

  if (iconsBase.IsEmpty()) {
    wxLogError("IconManager::DeleteAllIcons: icons base is empty");
    return ErrorCode::Unknown;
  }

  if (!IsSafeSubpath(wxString::FromUTF8(GetCacheDir()), iconsBase)) {
    wxLogError(
        "IconManager::DeleteAllIcons: icons base is outside cache dir: %s",
        iconsBase);
    return ErrorCode::UnsafePath;
  }

  if (!wxDirExists(iconsBase)) {
    wxLogInfo("IconManager::DeleteAllIcons: icons directory not found: %s",
              iconsBase);
    return ErrorCode::FileNotFound;
  }

  bool ok = wxFileName::Rmdir(iconsBase, wxPATH_RMDIR_RECURSIVE);
  if (!ok) {
    wxLogError("IconManager::DeleteAllIcons failed to remove: %s", iconsBase);
    return ErrorCode::IoError;
  }

  wxLogInfo("IconManager::DeleteAllIcons removed: %s", iconsBase);
  return ErrorCode::OK;
}

void IconManager::CleanupUnusedIcons(const std::string &playlist,
                                     const std::vector<std::string> &valid) {
  wxString base =
      wxString::FromUTF8(GetCacheDir()) + "/icons/" + playlist + "/original/";

  if (!wxDirExists(base))
    return;

  std::set<std::string> validSet(valid.begin(), valid.end());

  wxDir dir(base);
  wxString filename;

  auto cleanup = [&](const char *ext) {
    bool cont = dir.GetFirst(&filename, ext, wxDIR_FILES);
    while (cont) {
      wxString name = filename.BeforeLast('.');
      if (!validSet.count(name.ToStdString())) {
        wxRemoveFile(base + filename);
      }
      cont = dir.GetNext(&filename);
    }
  };

  cleanup("*.webp");
  cleanup("*.png");
  cleanup("*.svg");
}
