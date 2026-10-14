#pragma once

#include <filesystem>
#include <string>

// UTF-8 std::string -> std::filesystem::path.
//
// На Windows std::filesystem::path(std::string) интерпретирует байты как
// CP_ACP, что ломает не-ASCII пути (C:\Users\Иван\AppData\...).
// Создание path из std::u8string конвертирует UTF-8 в UTF-16 через WinAPI.
//
// На Linux байты совпадают, оба варианта эквивалентны.
inline std::filesystem::path ToFsPath(const std::string &utf8) {
#ifdef _WIN32
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
#else
  return std::filesystem::path(utf8);
#endif
}
