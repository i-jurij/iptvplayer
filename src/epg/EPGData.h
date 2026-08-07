#ifndef EPGDATA_H
#define EPGDATA_H

#include "LogControl.h"

#include <wx/datetime.h>

#include <cctype>
#include <ctime>
#include <regex> 
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
bool IsSameDay(time_t timestamp, time_t date);
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
inline time_t EpgTime::ParseXmltvTime(const std::string &timeStr) {
  if (timeStr.length() < 8)
    return 0;

  // ---- 1. Извлечение даты/времени и смещения ----
  std::string datePart, zonePart;
  std::string str = timeStr;

  // Удаляем возможный пробел между временем и смещением
  size_t spacePos = str.find(' ');
  if (spacePos != std::string::npos) {
    datePart = str.substr(0, spacePos);
    zonePart = str.substr(spacePos + 1);
    // Удаляем лишние пробелы в смещении
    while (!zonePart.empty() && zonePart.front() == ' ')
      zonePart.erase(0, 1);
  } else {
    // Ищем знак + или - в конце строки (без пробела)
    size_t signPos = str.find_last_of("+-");
    if (signPos != std::string::npos && signPos > 0) {
      datePart = str.substr(0, signPos);
      zonePart = str.substr(signPos);
    } else {
      datePart = str;
      zonePart = "";
    }
  }

  // ---- 2. Проверка минимальной длины даты ----
  if (datePart.length() < 8)
    return 0;

  // Поддерживаем частичные даты (YYYY, YYYYMM, YYYYMMDD)
  // Для полной даты нужно минимум 14 символов (YYYYMMDDHHMMSS)
  // Если меньше — дополняем недостающие части нулями
  std::string paddedDate = datePart;
  if (paddedDate.length() < 14) {
    // Дополняем до 14 символов нулями
    paddedDate += std::string(14 - paddedDate.length(), '0');
  }

  // ---- 3. Парсинг компонентов ----
  try {
    int year = std::stoi(paddedDate.substr(0, 4));
    int month = std::stoi(paddedDate.substr(4, 2));
    int day = std::stoi(paddedDate.substr(6, 2));
    int hour = std::stoi(paddedDate.substr(8, 2));
    int minute = std::stoi(paddedDate.substr(10, 2));
    int second = std::stoi(paddedDate.substr(12, 2));

    // Валидация
    if (year < 1970 || year > 2100)
      return 0;
    if (month < 1 || month > 12)
      return 0;
    if (day < 1 || day > 31)
      return 0;
    if (hour > 23 || minute > 59 || second > 59)
      return 0;

    // Создаём wxDateTime в локальном времени (БЕЗ MakeUTC!)
    wxDateTime dt(year, static_cast<wxDateTime::Month>(month - 1), day, hour,
                  minute, second);

    if (!dt.IsValid())
      return 0;

    // ---- 4. Применение смещения часового пояса ----
    if (!zonePart.empty()) {
      int sign = 1;
      size_t pos = 0;
      if (zonePart[0] == '-') {
        sign = -1;
        pos = 1;
      } else if (zonePart[0] == '+') {
        sign = 1;
        pos = 1;
      }

      // Извлекаем часы и минуты смещения
      std::string zoneDigits = zonePart.substr(pos);
      // Удаляем всё, кроме цифр
      zoneDigits.erase(std::remove_if(zoneDigits.begin(), zoneDigits.end(),
                                      [](char c) { return !std::isdigit(c); }),
                       zoneDigits.end());

      if (zoneDigits.length() >= 2) {
        int zoneHours = std::stoi(zoneDigits.substr(0, 2));
        int zoneMinutes = 0;
        if (zoneDigits.length() >= 4) {
          zoneMinutes = std::stoi(zoneDigits.substr(2, 2));
        }
        // Нормализация
        if (zoneHours > 14)
          zoneHours = 0;
        if (zoneMinutes > 59)
          zoneMinutes = 0;

        int offsetSeconds = (zoneHours * 3600 + zoneMinutes * 60) * sign;
        // Применяем смещение (для + смещение вычитаем, для - прибавляем)
        dt -= wxTimeSpan(0, 0, offsetSeconds);
      }
    }

    if (!dt.IsValid())
      return 0;

    // ---- 5. Преобразование в UTC ----
    dt.MakeUTC();
    return dt.GetTicks();

  } catch (const std::exception &) {
    LOG_DEBUG("Failed to parse time string: %s", timeStr.c_str());
    return 0;
  }
}

inline bool EpgTime::IsSameDay(time_t timestamp, time_t date) {
  wxDateTime ts(timestamp), d(date);
  wxDateTime::Tm tstm = ts.GetTm();
  wxDateTime::Tm dtm = d.GetTm();
  return (tstm.year == dtm.year && tstm.mon == dtm.mon &&
          tstm.mday == dtm.mday);
}

inline time_t EpgTime::GetStartOfDay(time_t date) {
  wxDateTime dt(date);
  wxDateTime::Tm tm = dt.GetTm(); // локальное время
  wxDateTime start(tm.year, tm.mon, tm.mday, 0, 0, 0);
  start.MakeUTC();
  return start.GetTicks();
}

inline time_t EpgTime::GetEndOfDay(time_t date) {
  wxDateTime dt(date);
  wxDateTime::Tm tm = dt.GetTm();
  wxDateTime end(tm.year, tm.mon, tm.mday, 23, 59, 59);
  end.MakeUTC();
  return end.GetTicks();
}

inline std::string EpgTime::FormatTime(time_t t) {
  if (t == 0)
    return "";
  wxDateTime dt(t);
  return dt.Format("%d.%m.%Y %H:%M").ToStdString();
}

inline std::string EpgTime::FormatTimeShort(time_t t) {
  if (t == 0)
    return "";
  wxDateTime dt(t);
  return dt.Format("%H:%M").ToStdString();
}

#endif
