#include "IconManager.h"
#include "LogControl.h"
#include "Profiler.h"

#include <curl/curl.h>
#include <webp/decode.h> // WebPGetInfo, WebPDecodeRGBA, WebPFree

#include <wx/app.h>
#include <wx/bmpbndl.h> // wxBitmapBundle::FromSVG
#include <wx/dir.h>     // wxDir, wxDIR_FILES
#include <wx/ffile.h>   // wxFFile
#include <wx/filename.h>
#include <wx/image.h> // wxImage
#include <wx/log.h>
#include <wx/mstream.h> // wxMemoryInputStream, wxMemoryOutputStream
#include <wx/stdpaths.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

static constexpr size_t SOFT_LIMIT_BYTES = 1 * 1024 * 1024; // 1 MB
static constexpr size_t HARD_LIMIT_BYTES = 2 * 1024 * 1024; // 2 MB

// ============================================================================
//  STATIC FIELDS
// ============================================================================
std::atomic<bool> IconManager::shuttingDown = false;

// New: paused flag for IconManager (controls whether workers should process)
std::atomic<bool> IconManager::paused{false};

// Очередь задач (FIFO, приоритеты будут добавлены позже)
namespace {
std::mutex qMutex;
std::condition_variable qCV;

struct Task {
  int priority; // 0 = highest
  std::function<void()> fn;
};

struct TaskCmp {
  bool operator()(const Task &a, const Task &b) const {
    return a.priority > b.priority; // min-heap
  }
};

std::priority_queue<Task, std::vector<Task>, TaskCmp> taskQueue;

// Ограничение параллелизма (адаптивное)
std::atomic<int> maxWorkers = 4;
std::atomic<int> activeWorkers = 0;
} // namespace

// ============================================================================
//  EnqueueTask — кладёт задачу в очередь
// ============================================================================
std::atomic<bool> IconManager::queueStarted{false};

void IconManager::EnqueueTask(std::function<void()> fn) {
  // PROFILE_SCOPE("IconManager::EnqueueTask");
  if (shuttingDown.load())
    return;

  {
    std::lock_guard<std::mutex> lock(qMutex);
    taskQueue.push({2, std::move(fn)});
    // LOG_DEBUG("IconManager::EnqueueTask pushed task queue_size=%zu",
    //         taskQueue.size());
  }

  qCV.notify_one();

  bool expected = false;
  if (queueStarted.compare_exchange_strong(expected, true)) {
    // LOG_DEBUG("IconManager::EnqueueTask starting ProcessQueue thread");
    std::thread([]() { ProcessQueue(); }).detach();
  }
}

// ============================================================================
//  ProcessQueue — главный worker-loop
// ============================================================================
void IconManager::ProcessQueue() {
  // PROFILE_SCOPE("IconManager::ProcessQueue");
  while (!shuttingDown.load()) {

    std::function<void()> job;

    {
      std::unique_lock<std::mutex> lock(qMutex);

      // Wait until there is work AND not paused, or shutting down.
      qCV.wait(lock, [&] {
        return shuttingDown.load() ||
               (!paused.load(std::memory_order_relaxed) && !taskQueue.empty());
      });

      if (shuttingDown.load())
        return;

      // If paused, loop back to wait.
      if (paused.load(std::memory_order_relaxed))
        continue;

      if (activeWorkers.load() >= maxWorkers.load()) {
        // LOG_DEBUG("IconManager::ProcessQueue waiting active=%u max=%u",
        //         activeWorkers.load(), maxWorkers.load());
        continue;
      }

      if (taskQueue.empty())
        continue;

      job = taskQueue.top().fn;
      taskQueue.pop();
      activeWorkers++;
      // LOG_DEBUG("IconManager::ProcessQueue dispatch job active=%u max=%u "
      //         "queue_size=%zu",
      //       activeWorkers.load(), maxWorkers.load(), taskQueue.size());
    }

    std::thread([job]() {
      try {
        job();
      } catch (...) {
      }

      activeWorkers--;

      // LOG_DEBUG("IconManager::worker finished active=%u",
      // activeWorkers.load());

      if (activeWorkers.load() < maxWorkers.load() && maxWorkers.load() < 16)
        maxWorkers++;

      {
        std::lock_guard<std::mutex> lock(qMutex);
        if (taskQueue.size() > 50 && maxWorkers.load() < 16)
          maxWorkers++;
      }

      if (taskQueue.empty() && maxWorkers.load() > 2)
        maxWorkers--;

      qCV.notify_one();
    }).detach();
  }
}

// ============================================================================
//  Shutdown — останавливает очередь и worker-потоки
// ============================================================================
void IconManager::Shutdown() {
  shuttingDown.store(true);
  qCV.notify_all();
}

// ============================================================================
//  EnsureIconAsync — главный API IconManager
//  Загружает master-логотип (оригинальный размер), без рескейла.
//  Работает строго асинхронно, callback вызывается в UI-потоке.
// ============================================================================

void IconManager::EnsureIconAsync(const std::string &playlist,
                                  const std::string &channel,
                                  const std::string &url, IconCallback cb) {
  // PROFILE_SCOPE("IconManager::EnsureIconAsync");
  // LOG_DEBUG("IconManager::EnsureIconAsync p=%s c=%s url=%s thread=%zu",
  //         playlist.c_str(), channel.c_str(), url.c_str(),
  //       std::hash<std::thread::id>{}(std::this_thread::get_id()));

  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  EnqueueTask([playlist, channel, url, cb]() {
    LoadMasterFromDiskAsync(playlist, channel, [=](wxBitmap bmp) {
      // --- CASE A: нашли нормальный master ---
      if (bmp.IsOk() && bmp.GetWidth() > 1 && bmp.GetHeight() > 1) {
        SafeCallReady(cb, bmp);
        return;
      }

      // --- CASE B: файл есть, но логотип пустой (маркер) ---
      if (bmp.IsOk() && (bmp.GetWidth() <= 1 || bmp.GetHeight() <= 1)) {
        SafeCallReady(cb, wxNullBitmap);
        return;
      }

      // --- CASE C: файла нет → скачиваем ---
      DownloadAndProcessAsync(playlist, channel, url, [=](wxBitmap dlBmp) {
        // LOG_DEBUG("IconManager::Download start p=%s c=%s url=%s",
        //         playlist.c_str(), channel.c_str(), url.c_str());

        if (!dlBmp.IsOk() || dlBmp.GetWidth() <= 1 || dlBmp.GetHeight() <= 1) {
          SafeCallReady(cb, wxNullBitmap);
          return;
        }

        SafeCallReady(cb, dlBmp);

        // LOG_DEBUG("IconManager::Download done p=%s c=%s url=%s",
        //         playlist.c_str(), channel.c_str(), url.c_str());
      });
    });
  });
}

// ============================================================================
//  LoadMasterFromDiskAsync
//  Асинхронная загрузка master-файла с диска (SVG / PNG / WebP / raster)
//  Возвращает bitmap оригинального размера.
// ============================================================================

void IconManager::LoadMasterFromDiskAsync(const std::string &playlist,
                                          const std::string &channel,
                                          IconCallback cb) {
  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  const std::string svgPath = GetSvgPath(playlist, channel);
  const std::string webpPath = GetIconPath(playlist, channel);
  const std::string pngPath = webpPath.substr(0, webpPath.size() - 5) + ".png";
  const std::string markerPath =
      webpPath.substr(0, webpPath.size() - 5) + ".marker";

  EnqueueTask([=]() {
    // ---------------------------------------------------------
    // 0) Маркер — логотипа нет
    // ---------------------------------------------------------
    {
      wxFileName marker(wxString::FromUTF8(markerPath));
      if (marker.FileExists()) {
        SafeCallReady(cb, wxNullBitmap);
        // LOG_DEBUG(
        //   "IconManager::LoadMasterFromDiskAsync marker exists p=%s c=%s",
        // playlist.c_str(), channel.c_str());

        return;
      }
    }

    // ---------------------------------------------------------
    // 1) SVG
    // ---------------------------------------------------------
    {
      wxFileName fn(wxString::FromUTF8(svgPath));
      if (fn.FileExists()) {
        wxFFile f(fn.GetFullPath(), "rb");
        if (f.IsOpened()) {
          wxString svgText;
          f.ReadAll(&svgText);

          if (!svgText.empty() && svgText.StartsWith("<svg")) {
            RenderSvgAsync(std::string(svgText.ToUTF8()),
                           [cb, markerPath, channel, playlist](wxBitmap bmp) {
                             if (!bmp.IsOk()) {
                               wxFFile m(wxString::FromUTF8(markerPath), "wb");
                               SafeCallReady(cb, wxNullBitmap);
                               return;
                             }
                             SafeCallReady(cb, bmp);
                             // LOG_DEBUG("IconManager::LoadMasterFromDiskAsync
                             // "
                             //         "found SVG p=%s c=%s",
                             //       playlist.c_str(), channel.c_str());
                           });
            return;
          }
        }
      }
    }

    // ---------------------------------------------------------
    // 2) PNG
    // ---------------------------------------------------------
    {
      wxFileName fn(wxString::FromUTF8(pngPath));
      if (fn.FileExists()) {

        std::vector<unsigned char> data;
        {
          wxFFile f(fn.GetFullPath(), "rb");
          if (f.IsOpened()) {
            // LOG_DEBUG("IconManager::LoadMasterFromDiskAsync reading PNG p=%s
            // "
            //         "c=%s path=%s",
            //       playlist.c_str(), channel.c_str(), pngPath.c_str());

            size_t size = f.Length();
            data.resize(size);
            f.Read(data.data(), size);
          }
        }

        if (!data.empty()) {

          // PROFILE_SCOPE("IconManager::PNG_Decode");
          // LOG_DEBUG("IconManager::PNG_Decode start p=%s c=%s",
          // playlist.c_str(), channel.c_str());

          DecodePngAsync(data,
                         [cb, markerPath, channel, playlist](wxBitmap bmp) {
                           if (!bmp.IsOk()) {
                             wxFFile m(wxString::FromUTF8(markerPath), "wb");
                             SafeCallReady(cb, wxNullBitmap);
                             return;
                           }
                           SafeCallReady(cb, bmp);

                           // LOG_DEBUG("IconManager::PNG_Decode done p=%s c=%s
                           // w=%d h=%d",
                           //         playlist.c_str(), channel.c_str(),
                           //         bmp.GetWidth(),
                           //       bmp.GetHeight());
                         });
          return;
        }
      }
    }

    // ---------------------------------------------------------
    // 3) WebP
    // ---------------------------------------------------------
    {
      wxFileName fn(wxString::FromUTF8(webpPath));
      if (fn.FileExists()) {

        std::vector<unsigned char> data;
        {
          wxFFile f(fn.GetFullPath(), "rb");
          if (f.IsOpened()) {
            size_t size = f.Length();
            data.resize(size);
            f.Read(data.data(), size);
          }
        }

        if (!data.empty()) {
          DecodeWebpAsync(data, [cb, markerPath](wxBitmap bmp) {
            if (!bmp.IsOk()) {
              wxFFile m(wxString::FromUTF8(markerPath), "wb");
              SafeCallReady(cb, wxNullBitmap);
              return;
            }
            SafeCallReady(cb, bmp);
          });
          return;
        }
      }
    }

    // ---------------------------------------------------------
    // 4) НИЧЕГО НЕ НАЙДЕНО → ВОЗВРАЩАЕМ ПУСТОЙ BITMAP
    // ---------------------------------------------------------
    SafeCallReady(cb, wxNullBitmap);
  });
}

// ============================================================================
//  ProcessDownloadedIconAsync
//  Обработка скачанного файла (SVG / PNG / WebP / raster)
//  - определяет формат
//  - сохраняет оригинал на диск
//  - декодирует в worker-потоке
//  - возвращает master-bitmap
// ============================================================================

void IconManager::ProcessDownloadedIconAsync(
    const std::vector<unsigned char> &data, const std::string &finalPathWebP,
    IconCallback cb) {
  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  EnqueueTask([data, finalPathWebP, cb]() {
    // -------------------------
    // 0) Проверка на HTML / битые данные
    // -------------------------
    auto isHtml = [&](const std::vector<unsigned char> &d) {
      if (d.empty())
        return true;

      size_t headLen = std::min<size_t>(d.size(), 512);
      std::string head((const char *)d.data(), headLen);
      std::string lower = head;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });

      return lower.find("<html") != std::string::npos ||
             lower.find("<!doctype") != std::string::npos ||
             lower.find("<script") != std::string::npos ||
             lower.find("error") != std::string::npos ||
             lower.find("not found") != std::string::npos ||
             lower.find("404") != std::string::npos;
    };

    if (isHtml(data)) {
      SaveNoLogoMarkerAsync(finalPathWebP, cb);
      return;
    }

    // -------------------------
    // Пути
    // -------------------------
    wxString webpPath = wxString::FromUTF8(finalPathWebP);
    wxString basePath = webpPath.BeforeLast('.');
    wxString svgPath = basePath + ".svg";
    wxString pngPath = basePath + ".png";

    // -------------------------
    // 1) SVG
    // -------------------------
    {
      std::string head((const char *)data.data(),
                       std::min<size_t>(data.size(), 1024));
      std::string lower = head;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });

      if (lower.find("<svg") != std::string::npos) {

        // сохраняем SVG как есть
        {
          wxFFile f(svgPath, "wb");
          if (f.IsOpened())
            f.Write(data.data(), data.size());
        }

        // рендерим SVG → bitmap
        RenderSvgAsync(std::string((const char *)data.data(), data.size()), cb);
        return;
      }
    }

    // -------------------------
    // 2) WebP
    // -------------------------
    if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' &&
        data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'E' &&
        data[10] == 'B' && data[11] == 'P') {
      // сохраняем WebP как есть
      {
        wxFFile f(webpPath, "wb");
        if (f.IsOpened())
          f.Write(data.data(), data.size());
      }

      // декодируем
      DecodeWebpAsync(data, cb);
      return;
    }

    // -------------------------
    // 3) PNG
    // -------------------------
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' &&
        data[2] == 'N' && data[3] == 'G') {
      // сохраняем PNG как есть
      {
        wxFFile f(pngPath, "wb");
        if (f.IsOpened())
          f.Write(data.data(), data.size());
      }

      DecodePngAsync(data, cb);
      return;
    }

    // -------------------------
    // 4) Raster (JPEG, BMP, GIF, etc.)
    // -------------------------
    {
      wxMemoryInputStream stream(data.data(), data.size());
      wxImage img(stream);

      if (img.IsOk()) {
        // сохраняем как PNG (оригинал)
        wxMemoryOutputStream out;
        img.SaveFile(out, wxBITMAP_TYPE_PNG);

        size_t sz = out.GetSize();
        std::vector<unsigned char> pngBuf(sz);
        out.CopyTo(pngBuf.data(), sz);

        {
          wxFFile f(pngPath, "wb");
          if (f.IsOpened())
            f.Write(pngBuf.data(), pngBuf.size());
        }

        wxBitmap bmp(img);
        SafeCallReady(cb, bmp);
        return;
      }
    }

    // -------------------------
    // 5) Unknown format → fallback
    // -------------------------
    SaveNoLogoMarkerAsync(finalPathWebP, cb);
  });
}
// ============================================================================
//  SaveNoLogoMarkerAsync — создаёт 1×1 прозрачный PNG и возвращает bitmap
// ============================================================================
void IconManager::SaveNoLogoMarkerAsync(const std::string &pathWebP,
                                        IconCallback cb) {
  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  EnqueueTask([pathWebP, cb]() {
    wxString webp = wxString::FromUTF8(pathWebP.c_str());
    wxString dir = webp.BeforeLast('/');
    if (!wxFileName::DirExists(dir)) {
      if (!wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        if (!wxFileName::DirExists(dir)) {
          wxLogError("Не удалось создать каталог '%s'", dir);
        }
      }
    }
    wxString markerPath = webp.BeforeLast('.') + ".marker";
    wxFFile f(markerPath, "wb");
    SafeCallReady(cb, wxNullBitmap);
  });
}

// ============================================================================
//  DownloadAndProcessAsync — скачивание + передача в
//  ProcessDownloadedIconAsync
// ============================================================================
void IconManager::DownloadAndProcessAsync(const std::string &playlist,
                                          const std::string &channel,
                                          const std::string &url,
                                          IconCallback cb) {
  if (shuttingDown.load()) {
    SafeCallReady(cb, wxNullBitmap);
    return;
  }

  const std::string basePath = GetIconPath(playlist, channel);
  const std::string markerPath =
      basePath.substr(0, basePath.size() - 5) + ".marker";

  EnqueueTask([playlist, channel, url, cb, basePath, markerPath]() {
    // --- 1. Проверяем маркер ---
    {
      wxFileName marker(wxString::FromUTF8(markerPath));
      if (marker.FileExists()) {
        SafeCallReady(cb, wxNullBitmap);
        return;
      }
    }

    // --- 2. Скачиваем ---
    std::vector<unsigned char> buffer;
    if (!DownloadIconToBuffer(url, buffer)) {
      SaveNoLogoMarkerAsync(basePath, cb);
      return;
    }

    // --- 3. Обрабатываем скачанное ---
    ProcessDownloadedIconAsync(
        buffer, basePath, [cb, markerPath](wxBitmap bmp) {
          if (!bmp.IsOk()) {
            // декодирование не удалось → создаём маркер
            wxFFile m(wxString::FromUTF8(markerPath), "wb");
            SafeCallReady(cb, wxNullBitmap);
            return;
          }

          SafeCallReady(cb, bmp);
        });
  });
}

bool IconManager::DownloadIconToBuffer(const std::string &url,
                                       std::vector<unsigned char> &buffer) {
  PROFILE_SCOPE("IconManager::DownloadIconToBuffer");
  // LOG_DEBUG("IconManager::DownloadIconToBuffer start url=%s", url.c_str());

  struct DownloadContext {
    std::vector<unsigned char> *buf;
    bool softExceeded;
    DownloadContext(std::vector<unsigned char> *b)
        : buf(b), softExceeded(false) {}
  };

  // static write callback (C-compatible)
  auto write_cb =
      +[](void *ptr, size_t size, size_t nmemb, void *userdata) -> size_t {
    DownloadContext *ctx = static_cast<DownloadContext *>(userdata);
    size_t total = size * nmemb;

    // safety: check pointer
    if (!ctx || !ctx->buf)
      return 0;

    // HARD_LIMIT_BYTES and SOFT_LIMIT_BYTES must be visible here
    if (ctx->buf->size() + total > HARD_LIMIT_BYTES) {
      // Too large — abort transfer
      wxLogWarning("IconManager: icon too large (> %zu bytes), aborting.",
                   (size_t)HARD_LIMIT_BYTES);
      return 0; // causes libcurl to abort with CURLE_WRITE_ERROR
    }
    if (ctx->buf->size() + total > SOFT_LIMIT_BYTES) {
      ctx->softExceeded = true;
    }

    ctx->buf->insert(ctx->buf->end(), (unsigned char *)ptr,
                     (unsigned char *)ptr + total);
    return total;
  };

  CURL *curl = curl_easy_init();
  if (!curl) {
    wxLogWarning("IconManager: curl_easy_init() failed.");
    return false;
  }

  buffer.clear();
  DownloadContext ctx(&buffer);

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 iptvplayer/1.0");

#if defined(CURLOPT_MAXFILESIZE_LARGE)
  curl_off_t hard_limit = (curl_off_t)HARD_LIMIT_BYTES;
  curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, hard_limit);
#else
  // fallback (may be 32-bit)
  curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, (long)HARD_LIMIT_BYTES);
#endif

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

  CURLcode res = curl_easy_perform(curl);

  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

  char *contentType = nullptr;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);

  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    wxLogWarning("IconManager: curl error: %s", curl_easy_strerror(res));
    return false;
  }

  if (httpCode < 200 || httpCode >= 300) {
    wxLogWarning("IconManager: HTTP %ld for %s", httpCode, url.c_str());
    return false;
  }

  if (buffer.empty()) {
    wxLogWarning("IconManager: empty buffer for %s", url.c_str());
    return false;
  }

  if (contentType &&
      std::string(contentType).find("text/html") != std::string::npos) {
    wxLogWarning("IconManager: content-type HTML for %s", url.c_str());
    return false;
  }

  return true;
}

// ============================================================================
//  New control methods: Pause/Resume/ClearMemory for IconManager
// ============================================================================
void IconManager::PauseLoading() {
  bool expected = false;
  if (paused.compare_exchange_strong(expected, true)) {
    LOG_DEBUG("IconManager::PauseLoading - paused");
  }
}

void IconManager::ResumeLoading() {
  bool expected = true;
  if (paused.compare_exchange_strong(expected, false)) {
    LOG_DEBUG("IconManager::ResumeLoading - resumed");
    qCV.notify_all();
  }
}

bool IconManager::IsPausedLoaded() {
  return paused.load(std::memory_order_relaxed);
}

void IconManager::ClearMemory() {
  {
    std::lock_guard<std::mutex> lock(qMutex);
    while (!taskQueue.empty())
      taskQueue.pop();
  }
  qCV.notify_all();
}
