#ifndef EPGDATA_H
#define EPGDATA_H

#include "LogControl.h"
#include "Utils.h"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

// Константа для обозначения ошибки при парсинге времени
constexpr time_t kTimeParseError = static_cast<time_t>(-1);

struct EpgProgram {
  std::string channelId;
  std::string title;
  std::string description;
  std::string category;
  time_t startTime = 0;
  time_t stopTime = 0;

  bool IsCurrent() const;
  bool IsFuture() const;
};

struct EpgChannel {
  std::string id;
  std::string displayName;
  std::vector<EpgProgram> programs;
};

struct EpgSource {
  std::string url;
  std::string name;
  time_t lastUpdate = 0;
  bool autoUpdate = false;
};

struct MappingEntry {
  std::string epgId;
  bool isManual = false;
  bool ignored = false;
};

namespace EpgTime {
// ParseXmltvTime: parses XMLTV time string and returns UTC epoch (time_t).
// On error returns static_cast<time_t>(-1).
time_t ParseXmltvTime(const std::string &timeStr);

// GetStartOfDay / GetEndOfDay: return UTC epoch of start/end of local day
// for the given UTC date. On error returns static_cast<time_t>(-1).
time_t GetStartOfDay(time_t date);
time_t GetEndOfDay(time_t date);

// Formatting helpers (use FormatLocalTime from Utils.cpp)
std::string FormatTime(time_t t);
std::string FormatTimeShort(time_t t);

// Current UTC epoch using std::chrono
inline time_t GetCurrentUtcEpoch() {
  using namespace std::chrono;
  return system_clock::to_time_t(system_clock::now());
}
} // namespace EpgTime

// ---------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------

inline bool EpgProgram::IsCurrent() const {
  time_t now = EpgTime::GetCurrentUtcEpoch();
  return (startTime <= now && stopTime > now);
}

inline bool EpgProgram::IsFuture() const {
  time_t now = EpgTime::GetCurrentUtcEpoch();
  return startTime > now;
}

// ---------------------------------------------------------------------
// Helper utilities (no wxWidgets dependency)
// ---------------------------------------------------------------------

inline std::string TrimWhitespace(const std::string &str) {
  size_t first = str.find_first_not_of(" \t\n\r");
  if (first == std::string::npos)
    return "";
  size_t last = str.find_last_not_of(" \t\n\r");
  return str.substr(first, last - first + 1);
}

inline bool IsAllDigits(const std::string &s) {
  if (s.empty())
    return false;
  for (char c : s) {
    if (!isdigit(static_cast<unsigned char>(c)))
      return false;
  }
  return true;
}

inline bool SafeStoi(const std::string &s, int &out) {
  if (!IsAllDigits(s))
    return false;
  try {
    out = std::stoi(s);
  } catch (...) {
    return false;
  }
  return true;
}

inline int GetOffsetFromAbbrev(const std::string &abbrev, bool &known) {
  known = true;
  std::string upper;
  upper.reserve(abbrev.size());
  for (char c : abbrev) {
    upper.push_back(toupper(static_cast<unsigned char>(c)));
  }
  if (upper == "UTC" || upper == "GMT")
    return 0;
  if (upper == "CET" || upper == "WAT")
    return 3600;
  if (upper == "CEST" || upper == "EET")
    return 7200;
  if (upper == "EEST")
    return 10800;
  if (upper == "EST")
    return -5 * 3600;
  if (upper == "EDT")
    return -4 * 3600;
  if (upper == "CST")
    return -6 * 3600;
  if (upper == "CDT")
    return -5 * 3600;
  if (upper == "MST")
    return -7 * 3600;
  if (upper == "MDT")
    return -6 * 3600;
  if (upper == "PST")
    return -8 * 3600;
  if (upper == "PDT")
    return -7 * 3600;
  if (upper == "MSK")
    return 3 * 3600;
  known = false;
  return 0;
}

// ---------------------------------------------------------------------
// Portable timegm implementation (Howard Hinnant style)
// ---------------------------------------------------------------------

static inline int64_t days_from_civil(int64_t y, unsigned m,
                                      unsigned d) noexcept {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3u : 9u)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

static inline bool safe_mul_add(int64_t a, int64_t b, int64_t c, int64_t &out) {
  if (a == 0 || b == 0) {
    out = c;
    return true;
  }
  if (std::llabs(a) > std::numeric_limits<int64_t>::max() / std::llabs(b))
    return false;
  int64_t prod = a * b;
  if ((prod > 0 && c > std::numeric_limits<int64_t>::max() - prod) ||
      (prod < 0 && c < std::numeric_limits<int64_t>::min() - prod))
    return false;
  out = prod + c;
  return true;
}

static inline time_t timegm_portable(const struct tm &tm) {
  int64_t year = static_cast<int64_t>(tm.tm_year) + 1900;
  unsigned month = static_cast<unsigned>(tm.tm_mon) + 1;
  unsigned day = static_cast<unsigned>(tm.tm_mday);
  int64_t days = days_from_civil(year, month, day);
  int64_t secs = 0;
  if (!safe_mul_add(days, 86400, 0, secs))
    return kTimeParseError;
  int64_t h = static_cast<int64_t>(tm.tm_hour) * 3600;
  int64_t mm = static_cast<int64_t>(tm.tm_min) * 60;
  if ((h > 0 && secs > std::numeric_limits<int64_t>::max() - h) ||
      (h < 0 && secs < std::numeric_limits<int64_t>::min() - h))
    return kTimeParseError;
  secs += h;
  if ((mm > 0 && secs > std::numeric_limits<int64_t>::max() - mm) ||
      (mm < 0 && secs < std::numeric_limits<int64_t>::min() - mm))
    return kTimeParseError;
  secs += mm;
  if ((tm.tm_sec > 0 &&
       secs > std::numeric_limits<int64_t>::max() - tm.tm_sec) ||
      (tm.tm_sec < 0 && secs < std::numeric_limits<int64_t>::min() - tm.tm_sec))
    return kTimeParseError;
  secs += tm.tm_sec;

  if (secs < static_cast<int64_t>(std::numeric_limits<time_t>::min()) ||
      secs > static_cast<int64_t>(std::numeric_limits<time_t>::max()))
    return kTimeParseError;

  return static_cast<time_t>(secs);
}

// ---------------------------------------------------------------------
// Parsing XMLTV time
// ---------------------------------------------------------------------

inline time_t EpgTime::ParseXmltvTime(const std::string &timeStr) {
  if (timeStr.length() < 8)
    return kTimeParseError;

  std::string datePart, zonePart;
  size_t spacePos = timeStr.find(' ');
  if (spacePos != std::string::npos) {
    datePart = timeStr.substr(0, spacePos);
    zonePart = TrimWhitespace(timeStr.substr(spacePos + 1));
  } else {
    size_t signPos = timeStr.find_first_of("+-Zz");
    if (signPos != std::string::npos) {
      datePart = timeStr.substr(0, signPos);
      zonePart = timeStr.substr(signPos);
    } else {
      datePart = timeStr;
      zonePart = "";
    }
  }

  if (datePart.length() < 8)
    return kTimeParseError;

  std::string padded = datePart;
  if (padded.length() < 14)
    padded += std::string(14 - padded.length(), '0');

  int year, month, day, hour, minute, second;
  if (sscanf(padded.c_str(), "%04d%02d%02d%02d%02d%02d", &year, &month, &day,
             &hour, &minute, &second) != 6)
    return kTimeParseError;

  if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 ||
      hour > 23 || minute > 59 || second > 59)
    return kTimeParseError;

  static const int daysInMonth[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  int maxDay = daysInMonth[month - 1];
  bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  if (month == 2 && leap)
    maxDay = 29;
  if (day > maxDay)
    return kTimeParseError;

  int offsetSeconds = 0;
  bool hasZone = false;

  if (!zonePart.empty()) {
    if (zonePart[0] == 'Z' || zonePart[0] == 'z') {
      offsetSeconds = 0;
      hasZone = true;
    } else if (zonePart[0] == '+' || zonePart[0] == '-') {
      int sign = (zonePart[0] == '-') ? -1 : 1;
      std::string digits = zonePart.substr(1);

      int zoneHours = 0, zoneMinutes = 0, zoneSeconds = 0;
      bool valid = true;
      if (digits.size() >= 5 && digits[2] == ':' &&
          IsAllDigits(digits.substr(0, 2)) &&
          IsAllDigits(digits.substr(3, 2))) {
        if (!SafeStoi(digits.substr(0, 2), zoneHours) ||
            !SafeStoi(digits.substr(3, 2), zoneMinutes)) {
          valid = false;
        }
        if (digits.size() >= 8 && digits[5] == ':') {
          if (IsAllDigits(digits.substr(6, 2))) {
            if (!SafeStoi(digits.substr(6, 2), zoneSeconds))
              valid = false;
          } else {
            valid = false;
          }
        }
      } else if (digits.size() >= 4 && IsAllDigits(digits.substr(0, 4))) {
        if (!SafeStoi(digits.substr(0, 2), zoneHours) ||
            !SafeStoi(digits.substr(2, 2), zoneMinutes)) {
          valid = false;
        }
      } else if (digits.size() >= 2 && IsAllDigits(digits.substr(0, 2))) {
        if (!SafeStoi(digits.substr(0, 2), zoneHours)) {
          valid = false;
        }
        zoneMinutes = 0;
      } else {
        valid = false;
      }

      if (valid) {
        if (zoneHours > 14 || zoneMinutes > 59 || zoneSeconds > 59) {
          valid = false;
          LOG_ERROR("EpgTime::ParseXmltvTime: zone offset out of range in '%s'",
                    zonePart.c_str());
        }
      }

      if (valid) {
        offsetSeconds =
            (zoneHours * 3600 + zoneMinutes * 60 + zoneSeconds) * sign;
        hasZone = true;
      } else {
        LOG_ERROR("EpgTime::ParseXmltvTime: invalid numeric zone format '%s', "
                  "treating as no zone (local time)",
                  zonePart.c_str());
        hasZone = false;
      }
    } else {
      bool known = true;
      offsetSeconds = GetOffsetFromAbbrev(zonePart, known);
      if (!known) {
        LOG_ERROR("EpgTime::ParseXmltvTime: unknown timezone abbreviation "
                  "'%s', treating as no zone (local time)",
                  zonePart.c_str());
        hasZone = false;
      } else {
        hasZone = true;
      }
    }
  }

  // If no timezone information, interpret as local time (system timezone)
  if (!hasZone) {
    struct tm tm_local{};
    tm_local.tm_year = year - 1900;
    tm_local.tm_mon = month - 1;
    tm_local.tm_mday = day;
    tm_local.tm_hour = hour;
    tm_local.tm_min = minute;
    tm_local.tm_sec = second;
    tm_local.tm_isdst = -1;
    time_t local_ts = mktime(&tm_local);
    if (local_ts == static_cast<time_t>(-1))
      return kTimeParseError;
    return local_ts;
  }

  // Timezone is specified: build UTC components and subtract offset.
  struct tm tm_utc{};
  tm_utc.tm_year = year - 1900;
  tm_utc.tm_mon = month - 1;
  tm_utc.tm_mday = day;
  tm_utc.tm_hour = hour;
  tm_utc.tm_min = minute;
  tm_utc.tm_sec = second;
  tm_utc.tm_isdst = 0; // UTC: no DST

  time_t utc_ts;
#if defined(HAVE_TIMEGM)
  utc_ts = timegm(&tm_utc);
#elif defined(_WIN32)
  utc_ts = _mkgmtime(&tm_utc);
#else
  utc_ts = timegm_portable(tm_utc);
#endif

  if (utc_ts == static_cast<time_t>(-1))
    return kTimeParseError;

  // Correct UTC epoch: subtract the offset (because the given time is local
  // time + offset)
  time_t result = utc_ts - offsetSeconds;
  return result;
}

// ---------------------------------------------------------------------
// GetStartOfDay / GetEndOfDay
// ---------------------------------------------------------------------

inline time_t EpgTime::GetStartOfDay(time_t date) {
  if (date <= 0)
    return kTimeParseError;

  struct tm tm_local;
#ifdef _WIN32
  if (localtime_s(&tm_local, &date) != 0)
    return kTimeParseError;
#else
  if (localtime_r(&date, &tm_local) == nullptr)
    return kTimeParseError;
#endif

  struct tm tm_midnight = {};
  tm_midnight.tm_year = tm_local.tm_year;
  tm_midnight.tm_mon = tm_local.tm_mon;
  tm_midnight.tm_mday = tm_local.tm_mday;
  tm_midnight.tm_hour = 0;
  tm_midnight.tm_min = 0;
  tm_midnight.tm_sec = 0;
  tm_midnight.tm_isdst = -1; // let system determine DST

  time_t start = mktime(&tm_midnight);
  if (start == static_cast<time_t>(-1))
    return kTimeParseError;
  return start;
}

inline time_t EpgTime::GetEndOfDay(time_t date) {
  if (date <= 0)
    return kTimeParseError;

  struct tm tm_local;
#ifdef _WIN32
  if (localtime_s(&tm_local, &date) != 0)
    return kTimeParseError;
#else
  if (localtime_r(&date, &tm_local) == nullptr)
    return kTimeParseError;
#endif

  struct tm tm_end = {};
  tm_end.tm_year = tm_local.tm_year;
  tm_end.tm_mon = tm_local.tm_mon;
  tm_end.tm_mday = tm_local.tm_mday;
  tm_end.tm_hour = 23;
  tm_end.tm_min = 59;
  tm_end.tm_sec = 59;
  tm_end.tm_isdst = -1;

  time_t end = mktime(&tm_end);
  if (end == static_cast<time_t>(-1))
    return kTimeParseError;
  return end;
}

// ---------------------------------------------------------------------
// Formatting (uses FormatLocalTime from Utils.cpp)
// ---------------------------------------------------------------------

inline std::string EpgTime::FormatTime(time_t t) {
  return FormatLocalTime(t, "%d.%m.%Y %H:%M");
}

inline std::string EpgTime::FormatTimeShort(time_t t) {
  return FormatLocalTime(t, "%H:%M");
}

#endif
