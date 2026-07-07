#include "Utils.h"

#include <wx/app.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/platform.h>
#include <wx/regex.h>
#include <wx/settings.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

// ============================================================================
// Платформенный детект (Windows / macOS / Linux, Wayland / X11)
// ============================================================================

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

// ============================================================================
// UTF-8 безопасное усечение
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

/*
NormalizeFileNameForDisk(fn.GetFullName().ToStdString()); // Disk по умолчанию
NormalizeFileNameForDisk(fn.GetFullName().ToStdString(), 128, Display);
NormalizeFileNameForDisk(fn.GetFullName().ToStdString(), 255, SafeUrl);
*/
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

    // URL-safe: только alphanum + [-._~]
    if (mode == SafeUrl && !std::isalnum(ch) && ch != '-' && ch != '.' &&
        ch != '_' && ch != '~') {
      out.push_back('_');
      continue;
    }

    out.push_back(static_cast<char>(ch));
  }

  // Устраняем повторы спецсимволов
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

  // Обрезка краёв
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

  // Reserved names — только для Disk
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
// Сообщения об ошибках и информации
// ============================================================================

void showError(wxWindow *parent, const wxString &message,
               const wxString &title) {
  if (wxIsMainThread()) {
    wxMessageBox(message, title, wxOK | wxICON_ERROR, parent);
  } else {
    wxTheApp->CallAfter([parent, message, title]() {
      wxMessageBox(message, title, wxOK | wxICON_ERROR, parent);
    });
  }
}

void showInfo(wxWindow *parent, const wxString &message,
              const wxString &caption) {
  if (wxIsMainThread()) {
    wxMessageBox(message, caption, wxOK | wxICON_INFORMATION, parent);
  } else {
    wxTheApp->CallAfter([parent, message, caption]() {
      wxMessageBox(message, caption, wxOK | wxICON_INFORMATION, parent);
    });
  }
}

// ============================================================================
// Вытаскивание URL из строки
// ============================================================================
std::vector<wxString> extractAllUrls(const wxString &s) {
  std::vector<wxString> urls;

  wxRegEx re(R"((https?://[^\s]+))", wxRE_EXTENDED);
  if (!re.IsValid())
    return urls;

  size_t searchPos = 0;

  while (searchPos < s.length()) {
    wxString sub = s.Mid(searchPos);

    if (!re.Matches(sub))
      break;

    wxString match = re.GetMatch(sub, 0);

    // Найти реальную позицию match в исходной строке
    size_t localPos = sub.find(match);
    if (localPos == wxString::npos)
      break;

    size_t globalPos = searchPos + localPos;

    wxString url = match;

    // Убираем хвостовую пунктуацию
    while (!url.empty()) {
      wxChar last = url.Last();
      if (last == ')' || last == ']' || last == '}' || last == '.' ||
          last == ',' || last == ';' || last == ':')
        url.RemoveLast();
      else
        break;
    }

    // Убираем ведущие скобки/кавычки
    while (!url.empty()) {
      wxChar first = url[0];
      if (first == '(' || first == '[' || first == '{' || first == '"' ||
          first == '\'')
        url.Remove(0, 1);
      else
        break;
    }

    if (!url.empty())
      urls.push_back(url);

    // Двигаемся дальше
    searchPos = globalPos + match.length();
  }

  return urls;
}

// ============================================================================
// Форматирование времени
// ============================================================================

wxString formatTimestamp(std::time_t timestamp) {
  if (timestamp == 0)
    return "-";
  std::tm *timeInfo = std::localtime(&timestamp);
  if (!timeInfo)
    return "-";
  std::ostringstream oss;
  oss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");
  return wxString(oss.str());
}

// ============================================================================
// Пути к иконкам
// ============================================================================

wxString getIconPath(const wxString &iconName) {
  wxString exeDir =
      wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
  wxString localPath = exeDir + "/icons/" + iconName;
  if (wxFileExists(localPath))
    return localPath;

  wxString sysPath = wxString(DATADIR) + "/iptvplayer/icons/" + iconName;
  if (wxFileExists(sysPath))
    return sysPath;

  return iconName;
}

// ============================================================================
// DPI-утилиты
// ============================================================================

int NormalizeDpi(int dpiY) {
  if (dpiY < 110)
    return 96;
  if (dpiY < 150)
    return 120;
  if (dpiY < 190)
    return 144;
  return 192;
}

int GetRawDPI(wxWindow *ctx) {
  int dpi;
  if (ctx && ctx->GetDPI().y > 0)
    dpi = ctx->GetDPI().y;
  else
    dpi = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);

  if (dpi <= 0)
    dpi = 96;
  return dpi;
}

int GetNormDPI(wxWindow *ctx) {
  int raw = GetRawDPI(ctx);
  return NormalizeDpi(raw);
}

int GetDpiLogoSizeList(wxWindow *ctx) {
  int dpi = GetNormDPI(ctx);

  double scale = dpi / 96.0;
  int size = (int)std::round(32 * scale);

  if (size < 24)
    size = 24;
  if (size > 64)
    size = 64;

  return size;
}

std::pair<int, int> GetCardSizeForDPI(int dpi) {
  if (dpi < 110)
    return {340, 90};
  if (dpi < 150)
    return {420, 110};
  return {500, 130};
}

int GetScaledCardSize(int dipValue, int dpi) {
  double scale = dpi / 96.0;
  return static_cast<int>(dipValue * scale + 0.5);
}

std::pair<int, int> ComputeLogoSizeForDPI(int dpi) {
  auto cs = GetCardSizeForDPI(dpi);
  int cardW = GetScaledCardSize(cs.first, dpi);
  int cardH = GetScaledCardSize(cs.second, dpi);

  int pad = GetScaledCardSize(6, dpi);
  int logoGap = GetScaledCardSize(10, dpi);
  int favZoneSize = cardH;

  int logoZoneLeft = pad;
  int logoZoneRight = cardW - pad - favZoneSize;
  int logoZoneW = std::max(1, logoZoneRight - logoZoneLeft - logoGap);
  int logoH = std::max(1, cardH - 2 * pad);

  return {logoZoneW, logoH};
}

// ============================================================================
// Sleep helper
// ============================================================================

static inline void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ============================================================================
// CPU load (кросс-платформенно)
// ============================================================================

#if defined(__linux__)
#include <fstream>
#include <string>
#include <unistd.h>

static bool ReadProcStat(uint64_t &idle, uint64_t &total) {
  std::ifstream f("/proc/stat");
  if (!f.is_open())
    return false;
  std::string line;
  if (!std::getline(f, line))
    return false;
  const char *s = line.c_str();
  while (*s && !(*s >= '0' && *s <= '9'))
    ++s;
  uint64_t user = 0, nice = 0, system = 0, idlev = 0, iowait = 0, irq = 0,
           softirq = 0, steal = 0, guest = 0, guest_nice = 0;
  sscanf(s, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system,
         &idlev, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
  idle = idlev + iowait;
  total = user + nice + system + idlev + iowait + irq + softirq + steal +
          guest + guest_nice;
  return true;
}
#endif

#if defined(__APPLE__)
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <unistd.h>

static bool ReadMachCPUTicks(uint64_t &idle, uint64_t &total) {
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  host_cpu_load_info_data_t cpuinfo;
  kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                                     (host_info_t)&cpuinfo, &count);
  if (kr != KERN_SUCCESS)
    return false;
  uint64_t user = cpuinfo.cpu_ticks[CPU_STATE_USER];
  uint64_t nice = cpuinfo.cpu_ticks[CPU_STATE_NICE];
  uint64_t system = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
  uint64_t idlev = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
  idle = idlev;
  total = user + nice + system + idlev;
  return true;
}
#endif

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>

static bool ReadWindowsCPUTimes(uint64_t &idle, uint64_t &total) {
  FILETIME idleTime, kernelTime, userTime;
  if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    return false;
  auto FileTimeToUint64 = [](const FILETIME &ft) -> uint64_t {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
           static_cast<uint64_t>(ft.dwLowDateTime);
  };
  uint64_t idle64 = FileTimeToUint64(idleTime);
  uint64_t kernel64 = FileTimeToUint64(kernelTime);
  uint64_t user64 = FileTimeToUint64(userTime);
  uint64_t system64 = kernel64 - idle64;
  idle = idle64;
  total = idle64 + system64 + user64;
  return true;
}
#endif

double GetSystemCPULoadPercent() {
  if (IsLinuxPlatform()) {
#if defined(__linux__)
    uint64_t idle1 = 0, total1 = 0;
    if (!ReadProcStat(idle1, total1))
      return -1.0;
    SleepMs(120);
    uint64_t idle2 = 0, total2 = 0;
    if (!ReadProcStat(idle2, total2))
      return -1.0;
    uint64_t idleDelta = (idle2 > idle1) ? (idle2 - idle1) : 0;
    uint64_t totalDelta = (total2 > total1) ? (total2 - total1) : 0;
    if (totalDelta == 0)
      return -1.0;
    double usage = (1.0 - (double)idleDelta / (double)totalDelta) * 100.0;
    if (usage < 0.0)
      usage = 0.0;
    if (usage > 100.0)
      usage = 100.0;
    return usage;
#else
    return -1.0;
#endif
  } else if (IsMacPlatform()) {
#if defined(__APPLE__)
    uint64_t idle1 = 0, total1 = 0;
    if (!ReadMachCPUTicks(idle1, total1))
      return -1.0;
    SleepMs(120);
    uint64_t idle2 = 0, total2 = 0;
    if (!ReadMachCPUTicks(idle2, total2))
      return -1.0;
    uint64_t idleDelta = (idle2 > idle1) ? (idle2 - idle1) : 0;
    uint64_t totalDelta = (total2 > total1) ? (total2 - total1) : 0;
    if (totalDelta == 0)
      return -1.0;
    double usage = (1.0 - (double)idleDelta / (double)totalDelta) * 100.0;
    if (usage < 0.0)
      usage = 0.0;
    if (usage > 100.0)
      usage = 100.0;
    return usage;
#else
    return -1.0;
#endif
  } else if (IsWindowsPlatform()) {
#if defined(_WIN32)
    uint64_t idle1 = 0, total1 = 0;
    if (!ReadWindowsCPUTimes(idle1, total1))
      return -1.0;
    SleepMs(120);
    uint64_t idle2 = 0, total2 = 0;
    if (!ReadWindowsCPUTimes(idle2, total2))
      return -1.0;
    uint64_t idleDelta = (idle2 > idle1) ? (idle2 - idle1) : 0;
    uint64_t totalDelta = (total2 > total1) ? (total2 - total1) : 0;
    if (totalDelta == 0)
      return -1.0;
    double usage = (1.0 - (double)idleDelta / (double)totalDelta) * 100.0;
    if (usage < 0.0)
      usage = 0.0;
    if (usage > 100.0)
      usage = 100.0;
    return usage;
#else
    return -1.0;
#endif
  } else {
    return -1.0;
  }
}

// ============================================================================
// Доступная память (кросс-платформенно)
// ============================================================================

#if defined(__WXMSW__)
#include <windows.h>
#elif defined(__WXMAC__)
#include <sys/sysctl.h>
#elif defined(__UNIX__) || defined(__LINUX__)
#include <fstream>
#include <string>
#include <unistd.h>
#endif

size_t GetAvailableRAM_MB() {
  if (IsWindowsPlatform()) {
#if defined(__WXMSW__)
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
      return static_cast<size_t>(statex.ullAvailPhys / (1024 * 1024));
    }
    return 4096;
#else
    return 4096;
#endif
  } else if (IsMacPlatform()) {
#if defined(__WXMAC__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
      return static_cast<size_t>(memsize / (1024 * 1024));
    }
    return 4096;
#else
    return 4096;
#endif
  } else if (IsLinuxPlatform()) {
#if defined(__UNIX__) || defined(__LINUX__)
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
      std::string line;
      while (std::getline(meminfo, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
          size_t kb = 0;
          if (sscanf(line.c_str(), "MemAvailable: %zu kB", &kb) == 1) {
            return kb / 1024;
          }
        }
      }
    }
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
      return static_cast<size_t>((static_cast<long long>(pages) * page_size) /
                                 (1024 * 1024));
    }
    return 4096;
#else
    return 4096;
#endif
  } else {
    return 4096;
  }
}

// ============================================================================
// Performance tuning / LRU
// ============================================================================

PerformanceMode DetectPerformanceMode(size_t availMB, unsigned cores,
                                      size_t modelCount) {
  int score = 0;

  if (availMB < 4096)
    score += 0;
  else if (availMB < 8192)
    score += 1;
  else
    score += 2;

  if (cores <= 2)
    score += 0;
  else if (cores <= 6)
    score += 1;
  else
    score += 2;

  if (modelCount < 2000)
    score += 2;
  else if (modelCount < 10000)
    score += 1;
  else
    score += 0;

  if (score <= 2)
    return PerformanceMode::Eco;
  if (score <= 4)
    return PerformanceMode::Balanced;
  return PerformanceMode::Fast;
}

PerformanceTuning GetPerformanceTuning(size_t availMB, unsigned cores,
                                       size_t modelCount) {
  PerformanceMode mode = DetectPerformanceMode(availMB, cores, modelCount);
  PerformanceTuning t{};

  switch (mode) {
  case PerformanceMode::Eco:
    t.maxConcurrentLoads = 2;
    t.maxTotalPending = 600;
    t.basePrefetch = 200;
    t.lru = {80, 200};
    break;

  case PerformanceMode::Balanced:
    t.maxConcurrentLoads = 6;
    t.maxTotalPending = 2500;
    t.basePrefetch = 1000;
    t.lru = {100, 300};
    break;

  case PerformanceMode::Fast:
    t.maxConcurrentLoads = 10;
    t.maxTotalPending = 8000;
    t.basePrefetch = 3000;
    t.lru = {140, 400};
    break;
  }

  return t;
}

LRULimits GetRecommendedLRULimits() {
  const size_t availableMB = GetAvailableRAM_MB();
  unsigned cores = std::max<unsigned>(1, std::thread::hardware_concurrency());
  PerformanceTuning t = GetPerformanceTuning(availableMB, cores, 0);
  return t.lru;
}

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

// ============================================================================
// SafeRemoveDirectory / Async / Marker files
// ============================================================================

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

  std::string baseUtf8 = baseDir.ToUTF8().data();
  fs::path basePath(baseUtf8);

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

    // Проверка безопасности
    wxString markerWx = wxString::FromUTF8(p.string());
    if (!IsSafeSubpath(baseDir, markerWx)) {
      skippedUnsafe++;
      continue;
    }

    // Удаление
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
  // POSIX: просто проверяем X_OK
  const char *cpath = path.mb_str(wxConvUTF8);
  return (access(cpath, X_OK) == 0);
#endif
}

bool EnsureXWaylandForEmbeddedVideo(bool needsEmbeddedVideo) {
#if defined(__linux__)
  if (!needsEmbeddedVideo)
    return true;

  if (!IsWaylandSession())
    return true;

  const char *curBackend = std::getenv("GDK_BACKEND");
  if (curBackend && std::string(curBackend) == "x11")
    return true;

  // Устанавливаем X11
  setenv("GDK_BACKEND", "x11", 1);

  // Путь к exe
  wxString exeWx = wxStandardPaths::Get().GetExecutablePath();
  std::string exe = exeWx.ToUTF8().data();

  // Собираем argv в UTF-8
  std::vector<std::string> argsUtf8;
  argsUtf8.reserve(wxTheApp->argc);
  for (int i = 0; i < wxTheApp->argc; ++i)
    argsUtf8.emplace_back(wxString(wxTheApp->argv[i]).ToUTF8().data());

  // Преобразуем в char*[]
  std::vector<char *> cargv;
  cargv.reserve(argsUtf8.size() + 1);
  cargv.push_back(const_cast<char *>(exe.c_str()));
  for (auto &s : argsUtf8)
    cargv.push_back(const_cast<char *>(s.c_str()));
  cargv.push_back(nullptr);

  // Перезапуск процесса
  execv(exe.c_str(), cargv.data());

  return false; // если execv вернулся — ошибка
#else
  (void)needsEmbeddedVideo;
  return true;
#endif
}

bool NeedsEmbeddedVideoBackend(ConfigManager *cfg) {
  if (!cfg)
    return false;

  // Путь к системному плееру
  std::string path = cfg->getSetting("media_player_path");

  // 1) Путь пустой → встроенный backend → embed
  if (path.empty())
    return true;

  // 2) Путь НЕ пустой → определяем тип внешнего плеера
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  // mpv → поддерживает embed
  if (lower.find("mpv") != std::string::npos)
    return true;

  // vlc → поддерживает embed
  if (lower.find("vlc") != std::string::npos)
    return true;

  // ffplay / ffmpeg → НЕ поддерживают embed
  if (lower.find("ffplay") != std::string::npos)
    return false;

  if (lower.find("ffmpeg") != std::string::npos)
    return false;

  // неизвестный плеер → считаем НЕ embed
  return false;
}
