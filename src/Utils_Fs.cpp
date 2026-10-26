#include "config.h"

#include "LogControl.h"
#include "Utils_Fs.h"
#include "Utils_FsPath.h"

#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

// ============================================================================
// Безопасные пути
// ============================================================================

bool IsSafeSubpath(const wxString &base, const wxString &candidate) {
  wxFileName baseFn(base);
  wxFileName candFn(candidate);

  baseFn.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_TILDE | wxPATH_NORM_CASE |
                   wxPATH_NORM_ABSOLUTE);
  candFn.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_TILDE | wxPATH_NORM_CASE |
                   wxPATH_NORM_ABSOLUTE);

  wxString baseAbs = baseFn.GetFullPath();
  wxString candAbs = candFn.GetFullPath();

  if (baseAbs.IsEmpty())
    return false;

#if defined(_WIN32)
  baseAbs.MakeLower();
  candAbs.MakeLower();
#endif

  wxString sep = wxFileName::GetPathSeparator();
  if (!baseAbs.EndsWith(sep))
    baseAbs += sep;

  return candAbs.StartsWith(baseAbs);
}

bool SafeRemoveDirectory(const wxString &dir, std::error_code &ec) {
  ec.clear();
  if (dir.IsEmpty())
    return false;

  wxDir wxdir(dir);
  if (!wxdir.IsOpened()) {
    ec = std::make_error_code(std::errc::no_such_file_or_directory);
    return false;
  }

  wxString filename;
  bool cont = wxdir.GetFirst(&filename, wxEmptyString,
                             wxDIR_FILES | wxDIR_DIRS | wxDIR_HIDDEN);

  while (cont) {
    wxFileName fn(dir, filename);
    if (fn.DirExists()) {
      std::error_code subEc;
      if (!SafeRemoveDirectory(fn.GetFullPath(), subEc)) {
        ec = subEc;
        return false;
      }
    } else if (fn.FileExists()) {
      if (!wxRemoveFile(fn.GetFullPath())) {
        ec = std::make_error_code(std::errc::permission_denied);
        return false;
      }
    }
    cont = wxdir.GetNext(&filename);
  }

  if (!wxRmdir(dir)) {
    ec = std::make_error_code(std::errc::permission_denied);
    return false;
  }

  return true;
}

void SafeRemoveDirectoryAsync(const wxString &dir, RemoveDirCallback cb) {
  std::thread([dir, cb]() {
    std::error_code ec;
    bool ok = SafeRemoveDirectory(dir, ec);
    wxTheApp->CallAfter([cb, ok, ec]() { cb(ok, ec); });
  }).detach();
}

bool RemoveMarkerFilesRecursive(const wxString &baseDir, size_t &removed,
                                size_t &skippedUnsafe, size_t &failed,
                                bool followSymlinks) {
  removed = skippedUnsafe = failed = 0;

  namespace fs = std::filesystem;

  std::error_code ec;

  fs::path basePath = ToFsPath(std::string(baseDir.ToUTF8().data()));

  if (!fs::exists(basePath, ec) || !fs::is_directory(basePath, ec)) {
    return false;
  }

  fs::directory_options opts = fs::directory_options::skip_permission_denied;
  if (followSymlinks)
    opts |= fs::directory_options::follow_directory_symlink;

  for (fs::recursive_directory_iterator it(basePath, opts, ec), end; it != end;
       it.increment(ec)) {
    if (ec)
      continue;

    const fs::directory_entry &entry = *it;

    std::error_code stEc;
    if (!entry.is_regular_file(stEc) || stEc)
      continue;

    fs::path p = entry.path();
    if (!p.has_extension())
      continue;

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (ext != ".marker")
      continue;

    const auto u8 = p.u8string();
    wxString markerWx = wxString::FromUTF8(
        reinterpret_cast<const char *>(u8.data()), u8.size());
    if (!IsSafeSubpath(baseDir, markerWx)) {
      skippedUnsafe++;
      continue;
    }

    std::error_code rmEc;
    bool ok = fs::remove(p, rmEc);
    if (rmEc) {
      failed++;
    } else if (ok) {
      removed++;
    } else {
      failed++;
    }
  }

  return true;
}

// ============================================================================
// UTF-8-безопасное усечение
// ============================================================================

static void TruncateUtf8Safe(std::string &s, size_t maxBytes) {
  if (s.size() <= maxBytes)
    return;
  s.resize(maxBytes);
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
    s.pop_back();
  }
  if (s.empty()) {
    s = "unnamed";
  }
}

std::string NormalizeFileNameForDisk(const std::string &input, size_t maxLen,
                                     NormalizeFileNameMode mode) {
  if (input.empty())
    return "unnamed";

  std::string out;
  out.reserve(input.size());

  for (unsigned char ch : input) {
    if (ch == 0)
      continue;
    if (ch < 0x20 || ch == 0x7F) {
      out.push_back((mode == Disk || mode == SafeUrl) ? '_' : ' ');
      continue;
    }

    if (mode == Disk) {
      static const std::string forbidden = "\\/:*?\"<>|";
      if (forbidden.find(ch) != std::string::npos) {
        out.push_back('_');
        continue;
      }
    }

    if (ch == ' ' || ch == '\t') {
      out.push_back((mode == Disk || mode == SafeUrl) ? '_' : ' ');
      continue;
    }

    if (mode == SafeUrl && !std::isalnum(ch) && ch != '-' && ch != '.' &&
        ch != '_' && ch != '~') {
      out.push_back('_');
      continue;
    }

    out.push_back(static_cast<char>(ch));
  }

  {
    std::string tmp;
    tmp.reserve(out.size());
    const char special = (mode == Disk) ? '_' : ' ';
    bool lastWasSpecial = false;

    for (unsigned char c : out) {
      if (c == special) {
        if (!lastWasSpecial && !tmp.empty()) {
          tmp.push_back(c);
          lastWasSpecial = true;
        }
      } else {
        tmp.push_back(c);
        lastWasSpecial = false;
      }
    }
    out.swap(tmp);
  }

  if (!out.empty()) {
    if (mode == Disk) {
      while (!out.empty() &&
             (out.front() == '_' || out.front() == '.' || out.front() == ' '))
        out.erase(out.begin());
      while (!out.empty() &&
             (out.back() == '_' || out.back() == '.' || out.back() == ' '))
        out.pop_back();
    } else {
      while (!out.empty() && (out.front() == ' ' || out.front() == '_'))
        out.erase(out.begin());
      while (!out.empty() && (out.back() == ' ' || out.back() == '_'))
        out.pop_back();
    }
  }

  if (out.empty())
    out = "unnamed";

  TruncateUtf8Safe(out, maxLen);

  if (mode == Disk) {
    std::string asciiUpper;
    asciiUpper.reserve(out.size());
    for (unsigned char c : out) {
      if (c < 0x80) {
        asciiUpper.push_back(static_cast<char>(std::toupper(c)));
      } else {
        break;
      }
    }

    static const std::vector<std::string> reserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const auto &r : reserved) {
      if (asciiUpper == r) {
        out += "_file";
        break;
      }
    }
  }

  return out;
}

// ============================================================================
// Поиск исполняемых файлов
// ============================================================================

wxString FindExecutableInPath(const wxString &name) {
  if (name.IsEmpty())
    return wxEmptyString;

  if (name.Find('/') != wxNOT_FOUND || name.Find('\\') != wxNOT_FOUND) {
    return name;
  }

  wxString pathEnv;
  if (!wxGetEnv("PATH", &pathEnv) || pathEnv.IsEmpty())
    return wxEmptyString;

  wxStringTokenizer tk(pathEnv, wxPATH_SEP);
  while (tk.HasMoreTokens()) {
    wxString dir = tk.GetNextToken();
    wxFileName fn(dir, name);

#if defined(_WIN32)
    wxString pathext;
    wxGetEnv("PATHEXT", &pathext);
    if (pathext.IsEmpty())
      pathext = ".COM;.EXE;.BAT;.CMD";
    wxStringTokenizer extTk(pathext, ";");
    while (extTk.HasMoreTokens()) {
      wxString ext = extTk.GetNextToken();
      if (!ext.StartsWith("."))
        ext.Prepend(".");
      wxFileName fnExt = fn;
      fnExt.SetExt(ext.Mid(1));
      if (fnExt.FileExists())
        return fnExt.GetFullPath();
    }
    if (fn.FileExists())
      return fn.GetFullPath();
#else
    if (fn.FileExists())
      return fn.GetFullPath();
#endif
  }

  return wxEmptyString;
}

bool IsFileExecutable(const wxString &path) {
  if (path.IsEmpty())
    return false;

#if defined(_WIN32)
  wxFileName fn(path);
  if (!fn.FileExists())
    return false;

  wxString ext = fn.GetExt().Lower();
  if (ext == "exe" || ext == "com" || ext == "bat" || ext == "cmd")
    return true;

  return false;
#else
  const char *cpath = path.mb_str(wxConvUTF8);
  return (access(cpath, X_OK) == 0);
#endif
}

// ============================================================================
// Пути к иконкам / локали
// ============================================================================

wxString getIconPath(const wxString &iconName) {
  return FindAppDataFile("icons/" + iconName);
}

wxString FindAppDataFile(const wxString &filename) {
  if (filename.IsEmpty())
    return wxEmptyString;

  static const wxString APP_NAME = "iptvplayer";

  wxArrayString searchPaths;
  auto pushUnique = [&searchPaths](const wxString &p) {
    if (!p.IsEmpty() && searchPaths.Index(p) == wxNOT_FOUND)
      searchPaths.Add(p);
  };

  auto makeAbs = [](const wxString &p) -> wxString {
    if (p.IsEmpty())
      return p;
    wxFileName fn(p);
    fn.MakeAbsolute();
    return fn.GetFullPath();
  };

  for (const char *var : {"SHARUN_DIR", "APPDIR"}) {
    const char *v = std::getenv(var);
    if (!v || !*v)
      continue;

    wxString probe = makeAbs(wxString::FromUTF8(v));
    while (probe.Length() > 1 && probe.EndsWith("/"))
      probe.RemoveLast();

    for (int up = 0; up < 4 && probe.Length() > 1; ++up) {
      pushUnique(probe + "/share/" + APP_NAME);
      pushUnique(probe + "/usr/share/" + APP_NAME);
      pushUnique(probe + "/resources");
      pushUnique(probe + "/share/" + APP_NAME + "/resources");

      int slash = probe.Find('/', true);
      if (slash <= 0)
        break;
      probe = probe.Left(slash);
    }
  }

  {
    wxString exePath = wxStandardPaths::Get().GetExecutablePath();
    wxFileName exeFn(exePath);
    exeFn.MakeAbsolute();
    wxString exeDir = exeFn.GetPath();

    pushUnique(exeDir);
    pushUnique(exeDir + "/resources");
    pushUnique(exeDir + "/../share/" + APP_NAME);
    pushUnique(exeDir + "/../usr/share/" + APP_NAME);
    pushUnique(exeDir + "/../Resources/" + APP_NAME);
  }

  pushUnique(wxStandardPaths::Get().GetLocalDataDir() + "/" + APP_NAME);
  pushUnique(wxStandardPaths::Get().GetDataDir() + "/" + APP_NAME);
  pushUnique(wxStandardPaths::Get().GetResourcesDir() + "/" + APP_NAME);
#ifdef DATADIR
  pushUnique(wxString::FromUTF8(DATADIR) + "/" + APP_NAME);
#endif

  pushUnique(wxGetCwd() + "/resources");
  pushUnique(wxGetCwd() + "/install/share/" + APP_NAME);
  pushUnique(wxGetCwd() + "/install/share/" + APP_NAME + "/resources");

  static bool dumped = false;
  if (!dumped) {
    dumped = true;
    LOG_DEBUG("FindAppDataFile('%s'): SHARUN_DIR=%s APPDIR=%s exe=%s cwd=%s",
              filename.utf8_str().data(),
              std::getenv("SHARUN_DIR") ? std::getenv("SHARUN_DIR") : "(unset)",
              std::getenv("APPDIR") ? std::getenv("APPDIR") : "(unset)",
              wxStandardPaths::Get().GetExecutablePath().utf8_str().data(),
              wxGetCwd().utf8_str().data());
    for (const auto &p : searchPaths)
      LOG_DEBUG("  candidate: %s", p.utf8_str().data());
  }

  const wxString sep = wxFileName::GetPathSeparator();
  for (const auto &base : searchPaths) {
    wxString full = base;
    if (!full.EndsWith(sep))
      full += sep;
    full += filename;
    if (wxFileExists(full))
      return full;
  }

  return wxEmptyString;
}

void RegisterLocaleSearchPaths() {
  wxArrayString paths;
  auto pushUnique = [&paths](const wxString &p) {
    if (!p.IsEmpty() && paths.Index(p) == wxNOT_FOUND)
      paths.Add(p);
  };

  auto makeAbs = [](const wxString &p) -> wxString {
    if (p.IsEmpty())
      return p;
    wxFileName fn(p);
    fn.MakeAbsolute();
    return fn.GetFullPath();
  };

  for (const char *var : {"SHARUN_DIR", "APPDIR"}) {
    const char *v = std::getenv(var);
    if (!v || !*v)
      continue;

    wxString probe = makeAbs(wxString::FromUTF8(v));
    while (probe.Length() > 1 && probe.EndsWith("/"))
      probe.RemoveLast();

    for (int up = 0; up < 4 && probe.Length() > 1; ++up) {
      pushUnique(probe + "/share/locale");
      pushUnique(probe + "/usr/share/locale");

      int slash = probe.Find('/', true);
      if (slash <= 0)
        break;
      probe = probe.Left(slash);
    }
  }

  {
    wxFileName exeFn(wxStandardPaths::Get().GetExecutablePath());
    exeFn.MakeAbsolute();
    wxString exeDir = exeFn.GetPath();

    pushUnique(exeDir + "/locale");

#if defined(__WXMAC__)
    pushUnique(exeDir + "/../Resources");
    pushUnique(exeDir + "/../share/locale");
#elif defined(__WXMSW__)
    pushUnique(exeDir);
    pushUnique(exeDir + "/../share/locale");
#else
    pushUnique(exeDir + "/../share/locale");
    pushUnique(exeDir + "/../usr/share/locale");
#endif
  }

  pushUnique(wxStandardPaths::Get().GetDataDir() + "/locale");

#if defined(__WXMAC__)
  pushUnique(wxStandardPaths::Get().GetDataDir());
#endif

#ifdef DATADIR
  pushUnique(wxString::FromUTF8(DATADIR) + "/locale");
#endif

  for (const auto &p : paths)
    wxLocale::AddCatalogLookupPathPrefix(p);

  static bool dumped = false;
  if (!dumped) {
    dumped = true;
    for (const auto &p : paths)
      LOG_DEBUG("Locale search path: %s", p.utf8_str().data());
  }
}
