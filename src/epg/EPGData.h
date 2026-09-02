#ifndef EPGDATA_H
#define EPGDATA_H

#include "LogControl.h"
#include "Utils.h"

#include <wx/datetime.h>

#include <cctype>
#include <ctime>
#include <string>
#include <vector>

struct EpgProgram {
  std::string channelId;
  std::string title;
  std::string description;
  std::string category;
  time_t startTime = 0;
  time_t stopTime = 0;

  bool IsCurrent() const;
  bool IsFuture() const;
  wxDateTime::Tm GetLocalStartTime() const;
  wxDateTime::Tm GetLocalStopTime() const;
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
time_t ParseXmltvTime(const std::string &timeStr);
time_t GetStartOfDay(time_t date);
time_t GetEndOfDay(time_t date);
std::string FormatTime(time_t t);
std::string FormatTimeShort(time_t t);
inline time_t GetCurrentUtcEpoch() {
  return static_cast<time_t>(wxDateTime::Now().GetTicks());
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

inline wxDateTime::Tm EpgProgram::GetLocalStartTime() const {
  wxDateTime dt(static_cast<time_t>(startTime), wxDateTime::UTC);
  dt.MakeTimezone(wxDateTime::Local);
  return dt.GetTm();
}

inline wxDateTime::Tm EpgProgram::GetLocalStopTime() const {
  wxDateTime dt(static_cast<time_t>(stopTime), wxDateTime::UTC);
  dt.MakeTimezone(wxDateTime::Local);
  return dt.GetTm();
}

// Вспомогательные функции (TrimWhitespace, IsAllDigits, SafeStoi) 
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

inline time_t EpgTime::ParseXmltvTime(const std::string &timeStr) {
  if (timeStr.length() < 8)
    return 0;

  // ---- 1. Разделение на дату и зону ----
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
    return 0;

  // ---- 2. Дополнение до 14 символов ----
  std::string padded = datePart;
  if (padded.length() < 14)
    padded += std::string(14 - padded.length(), '0');

  int year, month, day, hour, minute, second;
  if (sscanf(padded.c_str(), "%04d%02d%02d%02d%02d%02d", &year, &month, &day,
             &hour, &minute, &second) != 6)
    return 0;

  // ---- 3. Валидация ----
  if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 ||
      hour > 23 || minute > 59 || second > 59)
    return 0;

  static const int daysInMonth[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  int maxDay = daysInMonth[month - 1];
  bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  if (month == 2 && leap)
    maxDay = 29;
  if (day > maxDay)
    return 0;

  // ---- 4. Парсинг смещения зоны ----
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
                  "treating as UTC",
                  zonePart.c_str());
        hasZone = false;
      }
    } else {
      // Аббревиатура
      bool known = true;
      offsetSeconds = GetOffsetFromAbbrev(zonePart, known);
      if (!known) {
        LOG_ERROR("EpgTime::ParseXmltvTime: unknown timezone abbreviation "
                  "'%s', treating as UTC",
                  zonePart.c_str());
        hasZone = false;
      } else {
        hasZone = true;
      }
    }
  }

  // ---- 5. Если зона не указана, интерпретируем как локальное время ----
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
    if (local_ts == (time_t)-1)
      return 0;
    return local_ts; // локальное время системы
  }

  // ---- 6. Зона указана — вычисляем UTC напрямую ----
  struct tm tm_utc{};
  tm_utc.tm_year = year - 1900;
  tm_utc.tm_mon = month - 1;
  tm_utc.tm_mday = day;
  tm_utc.tm_hour = hour;
  tm_utc.tm_min = minute;
  tm_utc.tm_sec = second;
  tm_utc.tm_isdst = -1; // не используется для UTC, но оставляем

  time_t utc_ts;
#if defined(HAVE_TIMEGM)
  utc_ts = timegm(&tm_utc);
#elif defined(_WIN32)
  utc_ts = _mkgmtime(&tm_utc);
#else
  // Резервный итеративный метод (если timegm недоступен)
  // Берём local_ts из mktime и корректируем до UTC
  struct tm tm_local = tm_utc;
  time_t local_ts = mktime(&tm_local);
  if (local_ts == (time_t)-1)
    return 0;
  utc_ts = local_ts;
  int iterations = 0;
  while (iterations < 10) {
    struct tm tm_check = *gmtime(&utc_ts);
    int diff = (tm_check.tm_hour - tm_utc.tm_hour) * 3600 +
               (tm_check.tm_min - tm_utc.tm_min) * 60 +
               (tm_check.tm_sec - tm_utc.tm_sec);
    if (diff == 0)
      break;
    utc_ts -= diff;
    iterations++;
  }
  if (iterations >= 10) {
    LOG_ERROR(
        "EpgTime::ParseXmltvTime: fallback UTC calculation did not converge");
    return 0;
  }
#endif

  // ---- 7. Вычитаем смещение, чтобы получить UTC ----
  time_t result = utc_ts - offsetSeconds;
  return result;
}

// ========================================================================
// GetStartOfDay / GetEndOfDay
// ========================================================================
inline time_t EpgTime::GetStartOfDay(time_t date) {
  if (date <= 0)
    return 0;
  wxDateTime dt(date);
  if (!dt.IsValid())
    return 0;
  wxDateTime::Tm tm = dt.GetTm();
  wxDateTime startOfDay(tm.mday, static_cast<wxDateTime::Month>(tm.mon),
                        tm.year, 0, 0, 0, 0);
  return startOfDay.GetTicks();
}

inline time_t EpgTime::GetEndOfDay(time_t date) {
  if (date <= 0)
    return 0;
  wxDateTime dt(date);
  if (!dt.IsValid())
    return 0;
  wxDateTime::Tm tm = dt.GetTm();
  wxDateTime endOfDay(tm.mday, static_cast<wxDateTime::Month>(tm.mon), tm.year,
                      23, 59, 59, 0);
  return endOfDay.GetTicks();
}

// ========================================================================
// Форматирование времени (UTC → локальное)
// ========================================================================
inline std::string EpgTime::FormatTime(time_t t) {
  return FormatLocalTime(t, "%d.%m.%Y %H:%M");
}

inline std::string EpgTime::FormatTimeShort(time_t t) {
  return FormatLocalTime(t, "%H:%M");
}

#endif
