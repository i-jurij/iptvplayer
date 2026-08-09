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
};

namespace EpgTime {
time_t ParseXmltvTime(const std::string &timeStr);
time_t GetStartOfDay(time_t date);
time_t GetEndOfDay(time_t date);
time_t GetCurrentLocalTime();
std::string FormatTime(time_t t);
std::string FormatTimeShort(time_t t);
} // namespace EpgTime

// ---------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------

inline bool EpgProgram::IsCurrent() const {
  time_t now = EpgTime::GetCurrentLocalTime();
  return (startTime <= now && stopTime > now);
}

inline bool EpgProgram::IsFuture() const {
  time_t now = EpgTime::GetCurrentLocalTime();
  return startTime > now;
}

inline wxDateTime::Tm EpgProgram::GetLocalStartTime() const {
  wxDateTime dt(startTime);
  return dt.GetTm(); // автоматически преобразует в локальное время
}

inline wxDateTime::Tm EpgProgram::GetLocalStopTime() const {
  wxDateTime dt(stopTime);
  return dt.GetTm();
}

inline time_t EpgTime::GetCurrentLocalTime() {
  return wxDateTime::Now().GetTicks();
}

// Парсинг XMLTV-времени
// Вместо всей функции предоставляем исправленный участок
inline time_t EpgTime::ParseXmltvTime(const std::string &timeStr) {
  if (timeStr.length() < 8)
    return 0;

  // ---- 1. Разделение на дату/время и зону ----
  std::string datePart, zonePart;
  std::string str = timeStr;

  size_t spacePos = str.find(' ');
  if (spacePos != std::string::npos) {
    datePart = str.substr(0, spacePos);
    zonePart = str.substr(spacePos + 1);
    while (!zonePart.empty() && zonePart.front() == ' ')
      zonePart.erase(0, 1);
  } else {
    size_t signPos = str.find_last_of("+-");
    if (signPos != std::string::npos && signPos > 0) {
      datePart = str.substr(0, signPos);
      zonePart = str.substr(signPos);
    } else {
      datePart = str;
      zonePart.clear();
    }
  }

  if (datePart.length() < 8)
    return 0;

  std::string padded = datePart;
  if (padded.length() < 14)
    padded += std::string(14 - padded.length(), '0');

  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(padded.c_str(), "%04d%02d%02d%02d%02d%02d", &year, &month, &day,
             &hour, &minute, &second) != 6)
    return 0;

  if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 ||
      hour > 23 || minute > 59 || second > 59)
    return 0;

  // ---- Валидация дня в месяце ----
  static const int daysInMonth[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  int maxDay = daysInMonth[month - 1];
  bool isLeap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  if (month == 2 && isLeap)
    maxDay = 29;
  if (day > maxDay) {
    LOG_DEBUG("EpgTime::ParseXmltvTime: Invalid day %d for %d-%02d", day, year,
              month);
    return 0;
  }

  // ---- Создание времени с учётом зоны ----
  if (zonePart.empty()) {
    // FIX: если зона не указана, считаем время в UTC
    wxDateTime dt(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                  minute, second, wxDateTime::UTC);
    if (dt.IsValid())
      return dt.GetTicks();
    else
      return 0;
  }

  // ---- Обработка временной зоны ----
  int sign = 1;
  size_t pos = 0;
  if (zonePart[0] == '-') {
    sign = -1;
    pos = 1;
  } else if (zonePart[0] == '+') {
    sign = 1;
    pos = 1;
  } else if (zonePart[0] == 'Z' || zonePart[0] == 'z') {
    sign = 0;
  }

  int zoneHours = 0, zoneMinutes = 0;
  if (sign != 0) {
    std::string zoneDigits = zonePart.substr(pos);
    zoneDigits.erase(std::remove_if(zoneDigits.begin(), zoneDigits.end(),
                                    [](char c) { return !std::isdigit(c); }),
                     zoneDigits.end());

    if (zoneDigits.length() >= 2) {
      zoneHours = std::stoi(zoneDigits.substr(0, 2));
      if (zoneDigits.length() >= 4)
        zoneMinutes = std::stoi(zoneDigits.substr(2, 2));
      if (zoneHours > 14)
        zoneHours = 0;
      if (zoneMinutes > 59)
        zoneMinutes = 0;
    }
  }

  // Создаём время в UTC и корректируем на смещение (если зона указана)
  wxDateTime dt(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                minute, second, wxDateTime::UTC);
  if (!dt.IsValid())
    return 0;

  if (sign != 0) {
    int offsetSeconds = (zoneHours * 3600 + zoneMinutes * 60) * sign;
    dt -= wxTimeSpan(0, 0, offsetSeconds);
  }

  return dt.GetTicks();
}

inline time_t EpgTime::GetStartOfDay(time_t date) {
  wxDateTime dt = GetLocalDateTime(date);
  if (!dt.IsValid())
    return 0;
  wxDateTime::Tm tm = dt.GetTm();
  wxDateTime start(tm.mday, static_cast<wxDateTime::Month>(tm.mon), tm.year, 0,
                   0, 0);
  if (!start.IsValid())
    return 0;
  start.MakeUTC();
  return start.GetTicks();
}

inline time_t EpgTime::GetEndOfDay(time_t date) {
  wxDateTime dt = GetLocalDateTime(date);
  if (!dt.IsValid())
    return 0;
  wxDateTime::Tm tm = dt.GetTm();
  wxDateTime end(tm.mday, static_cast<wxDateTime::Month>(tm.mon), tm.year, 23,
                 59, 59);
  if (!end.IsValid())
    return 0;
  end.MakeUTC();
  return end.GetTicks();
}

inline std::string EpgTime::FormatTime(time_t t) {
  return FormatLocalTime(t, "%d.%m.%Y %H:%M");
}

inline std::string EpgTime::FormatTimeShort(time_t t) {
  return FormatLocalTime(t, "%H:%M");
}

#endif
