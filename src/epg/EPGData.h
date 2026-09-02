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
  wxDateTime dt(startTime);
  return dt.GetTm();
}

inline wxDateTime::Tm EpgProgram::GetLocalStopTime() const {
  wxDateTime dt(stopTime);
  return dt.GetTm();
}

// Вспомогательная функция: обрезает пробелы в начале и конце строки
inline std::string TrimWhitespace(const std::string &str) {
  size_t first = str.find_first_not_of(" \t\n\r");
  if (first == std::string::npos)
    return "";
  size_t last = str.find_last_not_of(" \t\n\r");
  return str.substr(first, last - first + 1);
}

// Вспомогательная функция: проверяет, состоит ли строка только из цифр
inline bool IsAllDigits(const std::string &s) {
  if (s.empty())
    return false;
  for (char c : s) {
    if (!isdigit(static_cast<unsigned char>(c)))
      return false;
  }
  return true;
}

// Вспомогательная функция: безопасное преобразование строки в число
// Возвращает true и записывает результат, если строка содержит только цифры.
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

// Таблица известных аббревиатур временных зон (смещение в секундах)
inline int GetOffsetFromAbbrev(const std::string &abbrev) {
  // Приводим к верхнему регистру
  std::string upper;
  upper.reserve(abbrev.size());
  for (char c : abbrev) {
    upper.push_back(toupper(static_cast<unsigned char>(c)));
  }
  // Распространённые аббревиатуры
  if (upper == "UTC" || upper == "GMT")
    return 0;
  if (upper == "CET" ||
      upper == "WAT") // Central European Time, West Africa Time
    return 3600;
  if (upper == "CEST" ||
      upper == "EET") // Central European Summer, Eastern European
    return 7200;
  if (upper == "EEST") // Eastern European Summer
    return 10800;
  if (upper == "EST") // Eastern Standard (North America)
    return -5 * 3600;
  if (upper == "EDT") // Eastern Daylight
    return -4 * 3600;
  if (upper == "CST") // Central Standard (North America)
    return -6 * 3600;
  if (upper == "CDT") // Central Daylight
    return -5 * 3600;
  if (upper == "MST") // Mountain Standard
    return -7 * 3600;
  if (upper == "MDT") // Mountain Daylight
    return -6 * 3600;
  if (upper == "PST") // Pacific Standard
    return -8 * 3600;
  if (upper == "PDT") // Pacific Daylight
    return -7 * 3600;
  if (upper == "MSK") // Moscow Standard
    return 3 * 3600;
  // Можно добавить другие при необходимости
  return 0; // неизвестная зона — считаем UTC
}

// Парсинг XMLTV-времени
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

  // ---- 3. Валидация даты и времени ----
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

  // ---- 4. Определение смещения временной зоны ----
  int offsetSeconds = 0;
  bool applyOffset = false; // нужно ли корректировать время

  if (!zonePart.empty()) {
    // Зона указана
    if (zonePart[0] == 'Z' || zonePart[0] == 'z') {
      // UTC — ничего не делаем, offset = 0
      applyOffset = true; // можно не вычитать, но для единообразия
    } else if (zonePart[0] == '+' || zonePart[0] == '-') {
      // Числовое смещение
      int sign = (zonePart[0] == '-') ? -1 : 1;
      std::string digits = zonePart.substr(1);

      int zoneHours = 0, zoneMinutes = 0, zoneSeconds = 0;
      bool valid = true;

      // Формат ±HH:MM[:SS]
      if (digits.size() >= 5 && digits[2] == ':' &&
          IsAllDigits(digits.substr(0, 2)) &&
          IsAllDigits(digits.substr(3, 2))) {
        if (!SafeStoi(digits.substr(0, 2), zoneHours) ||
            !SafeStoi(digits.substr(3, 2), zoneMinutes)) {
          valid = false;
        }
        // Проверка секунд, если есть
        if (digits.size() >= 8 && digits[5] == ':') {
          if (IsAllDigits(digits.substr(6, 2))) {
            if (!SafeStoi(digits.substr(6, 2), zoneSeconds)) {
              valid = false;
            }
          } else {
            valid = false;
          }
        }
      }
      // Формат ±HHMM (без двоеточия)
      else if (digits.size() >= 4 && IsAllDigits(digits.substr(0, 4))) {
        if (!SafeStoi(digits.substr(0, 2), zoneHours) ||
            !SafeStoi(digits.substr(2, 2), zoneMinutes)) {
          valid = false;
        }
      }
      // Формат ±HH (только часы)
      else if (digits.size() >= 2 && IsAllDigits(digits.substr(0, 2))) {
        if (!SafeStoi(digits.substr(0, 2), zoneHours)) {
          valid = false;
        }
        zoneMinutes = 0;
      } else {
        valid = false;
      }

      // Проверка диапазонов: часы 0–14, минуты 0–59, секунды 0–59
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
        applyOffset = true;
      } else {
        LOG_ERROR("EpgTime::ParseXmltvTime: invalid numeric zone format '%s', "
                  "treating as UTC",
                  zonePart.c_str());
        applyOffset = false; // не применять смещение
      }
    } else {
      // Аббревиатура временной зоны
      offsetSeconds = GetOffsetFromAbbrev(zonePart);
      applyOffset = true; // применяем смещение (даже если 0)
    }
  }

  // ---- 5. Создание wxDateTime ----
  wxDateTime dt;
  if (zonePart.empty()) {
    // Зона не указана — интерпретируем как локальное время
    dt = wxDateTime(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                    minute, second);
  } else {
    // Зона указана — создаём как UTC, затем скорректируем
    dt = wxDateTime(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                    minute, second, wxDateTime::UTC);
  }

  if (!dt.IsValid())
    return 0;

  // Применяем смещение, если нужно
  if (!zonePart.empty() && applyOffset) {
    dt -= wxTimeSpan(0, 0, offsetSeconds);
  }

  return dt.GetTicks();
}

inline time_t EpgTime::GetStartOfDay(time_t date) {
  if (date <= 0)
    return 0;
  wxDateTime dt(date); // интерпретирует date как локальное время
  if (!dt.IsValid())
    return 0;
  wxDateTime::Tm tm = dt.GetTm(); // локальные компоненты (день, месяц, год)
  wxDateTime startOfDay(tm.mday, static_cast<wxDateTime::Month>(tm.mon),
                        tm.year, 0, 0, 0, 0); // локальное начало дня
  return startOfDay.GetTicks(); // преобразует локальное начало дня в UTC-тики
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

inline std::string EpgTime::FormatTime(time_t t) {
  return FormatLocalTime(t, "%d.%m.%Y %H:%M");
}

inline std::string EpgTime::FormatTimeShort(time_t t) {
  return FormatLocalTime(t, "%H:%M");
}

#endif