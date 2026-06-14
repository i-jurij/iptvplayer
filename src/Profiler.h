#pragma once

/*
Profiler.h
 ├─ ENABLE_PROFILER (вкл/выкл)
 ├─ глобальная карта g_prof
 ├─ ScopedProfile
 ├─ PROFILE_SCOPE(name)
 └─ DumpGlobalProfile()

CardsBase.cpp
 ├─ PROFILE_SCOPE("RenderTile")
 ├─ PROFILE_SCOPE("OnPaint")
 └─ DumpGlobalProfile() каждые 2 сек

IconManager.cpp
 ├─ PROFILE_SCOPE("IconManager::EnsureIcon")
 ├─ PROFILE_SCOPE("PNG_Load")
 ├─ PROFILE_SCOPE("PNG_Rescale")
 ├─ PROFILE_SCOPE("PNG_Decode")
 └─ PROFILE_SCOPE("Download")
*/

// =============================
//  ГЛОБАЛЬНЫЙ ПЕРЕКЛЮЧАТЕЛЬ
// =============================
#define ENABLE_PROFILER 1 // ← включить
// #define ENABLE_PROFILER 0 // ← выключить

// =============================
//  СТРУКТУРА ЗАПИСИ
// =============================
struct ProfileEntry {
  long long total = 0;
  long long max = 0;
  long long count = 0;
};

// =============================
//  ГЛОБАЛЬНОЕ ХРАНИЛИЩЕ (потокобезопасное)
// =============================
#if ENABLE_PROFILER
#include <mutex>
#include <string>
#include <unordered_map>
#include <wx/datetime.h>
#include <wx/log.h>

inline std::unordered_map<std::string, ProfileEntry> g_prof;
inline std::mutex g_prof_mutex;
inline wxLongLong g_profLastDump = 0;
#endif

// =============================
//  СКОП-ЗОНА
// =============================
#if ENABLE_PROFILER

#include <wx/datetime.h>

class ScopedProfile {
public:
  explicit ScopedProfile(const char *name) : m_name(name) {
    m_start = wxGetUTCTimeMillis();
  }

  ~ScopedProfile() {
    wxLongLong end = wxGetUTCTimeMillis();
    long long dt = (end - m_start).ToLong();

    std::lock_guard<std::mutex> lock(g_prof_mutex);
    auto &e = g_prof[m_name];
    e.total += dt;
    e.count += 1;
    if (dt > e.max)
      e.max = dt;
  }

private:
  const char *m_name;
  wxLongLong m_start;
};

#define PROFILE_SCOPE(name) ScopedProfile _prof_##__LINE__(name)

#else

#define PROFILE_SCOPE(name)

#endif

// =============================
//  ВЫВОД ПРОФИЛЯ
// =============================
inline void DumpGlobalProfile() {
#if ENABLE_PROFILER
  std::unordered_map<std::string, ProfileEntry> snapshot;

  {
    std::lock_guard<std::mutex> lock(g_prof_mutex);
    if (g_prof.empty()) {
      // обновим время дампа, чтобы не спамить
      g_profLastDump = wxGetUTCTimeMillis();
      return;
    }
    snapshot.swap(g_prof); // быстро забираем данные и очищаем глобальную карту
  }

  wxLogDebug("===== GLOBAL PROFILE DUMP =====");

  for (auto &kv : snapshot) {
    const std::string &name = kv.first;
    const ProfileEntry &e = kv.second;

    if (e.count == 0)
      continue;

    long long avg = e.total / e.count;

    wxLogDebug("%s: avg=%lld ms, max=%lld ms, calls=%lld", name.c_str(), avg,
               e.max, e.count);
  }

  wxLogDebug("================================");

  g_profLastDump = wxGetUTCTimeMillis();
#endif
}
