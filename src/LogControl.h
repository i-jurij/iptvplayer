#pragma once

#include <atomic>
#include <utility>

#include <wx/app.h>
#include <wx/log.h>
#include <wx/string.h>

inline std::atomic<bool> g_verboseLogging{true};

namespace log_detail {

// Ноль аргументов форматирования — возвращаем fmt как есть.
inline wxString format(const wxString &fmt) { return fmt; }

// Один или больше — wxString::Format. В wxWidgets 3.1+ это шаблон
// с проверкой типов на этапе компиляции, так что forward идеально
// подходит. В C++17 работает без if constexpr: перегрузка разрешается
// на этапе компиляции по количеству аргументов.
template <typename T, typename... Rest>
wxString format(const wxString &fmt, T &&first, Rest &&...rest) {
  return wxString::Format(fmt, std::forward<T>(first),
                          std::forward<Rest>(rest)...);
}

// Единая точка вывода debug-сообщения: логика «main thread или
// CallAfter» дублировалась в каждом раскрытии макроса. Теперь это
// inline-функция, раскрытие макроса — три строки.
inline void debug_impl(const wxString &msg) {
  if (wxIsMainThread()) {
    wxLogDebug("%s", msg.c_str());
  } else if (wxTheApp) {
    wxTheApp->CallAfter([msg]() { wxLogDebug("%s", msg.c_str()); });
  }
}

} // namespace log_detail

// Публичные макросы. __VA_ARGS__ передаётся как единый пакет в
// log_detail::format(...) — никаких __VA_OPT__ (C++20) и никаких
// , ##__VA_ARGS__ (GNU extension). Работает на GCC 7.5+, Clang,
// MSVC 2017+ в режимах C++17 и C++20 без правок.
//
//   LOG_INFO("hello")           → wxLogInfo("%s", "hello")
//   LOG_INFO("hello %s", name)  → wxLogInfo("%s", wxString::Format("hello %s",
//   name))

#define LOG_DEBUG(...)                                                         \
  do {                                                                         \
    if (!g_verboseLogging.load())                                              \
      break;                                                                   \
    log_detail::debug_impl(log_detail::format(__VA_ARGS__));                   \
  } while (0)

#define LOG_INFO(...) wxLogInfo("%s", log_detail::format(__VA_ARGS__).c_str())
#define LOG_WARN(...)                                                          \
  wxLogWarning("%s", log_detail::format(__VA_ARGS__).c_str())
#define LOG_ERROR(...) wxLogError("%s", log_detail::format(__VA_ARGS__).c_str())
