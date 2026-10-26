#pragma once

// ============================================================================
// Umbrella header. ПЕРЕХОДНЫЙ — будет удалён в Phase 4.
// Для нового кода: подключайте конкретный Utils_*.h:
//   Utils_Platform.h — _U, платформа, тема, BindThemeWatcher
//   Utils_Net.h      — curl, UrlAvailability
//   Utils_Fs.h       — пути, filenames, locale
//   Utils_UI.h       — DPI, layout, диалоги, время
//   Utils_Perf.h     — RAM/CPU/perf tuning
// ============================================================================

#include "ConfigManager.h" // транзитивная зависимость 18 файлов; уйдёт в Phase 3

#include "Utils_Fs.h"
#include "Utils_Net.h"
#include "Utils_Perf.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"
