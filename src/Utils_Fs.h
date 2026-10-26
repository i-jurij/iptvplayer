#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <system_error>

#include <wx/string.h>

// Безопасные пути
bool IsSafeSubpath(const wxString &base, const wxString &candidate);
bool SafeRemoveDirectory(const wxString &dir, std::error_code &ec);
using RemoveDirCallback = std::function<void(bool, const std::error_code &)>;
void SafeRemoveDirectoryAsync(const wxString &dir, RemoveDirCallback cb);
bool RemoveMarkerFilesRecursive(const wxString &baseDir, size_t &removed,
                                size_t &skippedUnsafe, size_t &failed,
                                bool followSymlinks);

// UTF-8-безопасное имя файла
enum NormalizeFileNameMode {
  Disk,    // файловая система: замена запрещённых символов, reserved-имена
  Display, // UI: убрать управляющие символы, сохранить читаемые пробелы
  SafeUrl  // URL/параметры: только безопасные символы
};
std::string NormalizeFileNameForDisk(const std::string &input,
                                     size_t maxLen = 200,
                                     NormalizeFileNameMode mode = Disk);

// Исполняемые файлы
wxString FindExecutableInPath(const wxString &name);
bool IsFileExecutable(const wxString &path);

// Ресурсы и локали
wxString getIconPath(const wxString &iconName);
wxString FindAppDataFile(const wxString &filename);
void RegisterLocaleSearchPaths();
