#ifndef EPGDATA_H
#define EPGDATA_H

#include <ctime>
#include <string>
#include <vector>
#include <wx/datetime.h>

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
// Формат: "YYYYMMDDHHMMSS" или "YYYYMMDDHHMMSS ±HHMM"
// Если смещение указано — применяется, иначе время считается UTC.
inline time_t EpgTime::ParseXmltvTime(const std::string &timeStr) {
  if (timeStr.length() < 14)
    return 0;

  int year = std::stoi(timeStr.substr(0, 4));
  int month = std::stoi(timeStr.substr(4, 2));
  int day = std::stoi(timeStr.substr(6, 2));
  int hour = std::stoi(timeStr.substr(8, 2));
  int minute = std::stoi(timeStr.substr(10, 2));
  int second = std::stoi(timeStr.substr(12, 2));

  // Парсим как UTC
  wxDateTime dt(year, wxDateTime::Month(month - 1), day, hour, minute, second);
  dt.MakeUTC();

  // Если есть смещение, применяем его
  if (timeStr.length() >= 19) {
    char sign = timeStr[14];
    int offsetHours = std::stoi(timeStr.substr(15, 2));
    int offsetMinutes = std::stoi(timeStr.substr(17, 2));
    int offsetSeconds = offsetHours * 3600 + offsetMinutes * 60;
    if (sign == '-')
      offsetSeconds = -offsetSeconds;
    dt -= wxTimeSpan(0, 0, offsetSeconds);
  }

  return dt.GetTicks();
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
