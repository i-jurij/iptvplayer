// LogoCache_Rescale.cpp
// Рескейлинг изображений, пул потоков, GetLogoAsync
#define __LOGOCACHE_CPP__
#include "LogoCache.h"
#include "Profiler.h"
#include "Utils.h"
#include <algorithm>
#include <condition_variable>
#include <queue>
#include <thread>
#include <wx/dcmemory.h>
#include <wx/log.h>

// --- Пул потоков для рескейлов ---
class RescaleThreadPool {
public:
  explicit RescaleThreadPool(size_t threads) : m_stop(false) {
    for (size_t i = 0; i < threads; ++i) {
      m_workers.emplace_back([this]() { this->WorkerLoop(); });
    }
  }

  ~RescaleThreadPool() {
    {
      std::lock_guard<std::mutex> lk(m_queueMutex);
      m_stop = true;
    }
    m_cv.notify_all();
    for (auto &t : m_workers) {
      if (t.joinable())
        t.join();
    }
  }

  void Enqueue(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lk(m_queueMutex);
      m_tasks.push(std::move(task));
    }
    m_cv.notify_one();
  }

private:
  void WorkerLoop() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lk(m_queueMutex);
        m_cv.wait(lk, [this]() { return m_stop || !m_tasks.empty(); });
        if (m_stop && m_tasks.empty())
          return;
        task = std::move(m_tasks.front());
        m_tasks.pop();
      }
      try {
        task();
      } catch (...) { /* swallow */
      }
    }
  }

  std::vector<std::thread> m_workers;
  std::queue<std::function<void()>> m_tasks;
  std::mutex m_queueMutex;
  std::condition_variable m_cv;
  bool m_stop;
};

// --- Глобальные переменные ---
static std::unique_ptr<RescaleThreadPool> s_rescalePool;
static std::once_flag s_rescalePoolInitFlag;
static std::atomic<size_t> s_maxConcurrentRescales(8);

static void EnsureRescalePoolInitialized() {
  std::call_once(s_rescalePoolInitFlag, []() {
    size_t n = s_maxConcurrentRescales.load(std::memory_order_relaxed);
    if (n == 0)
      n = 1;
    s_rescalePool.reset(new RescaleThreadPool(n));
  });
}

// --- RescaleAsync ---
void LogoCache::RescaleAsync(const LogoBitmapPtr &master, const std::string &p,
                             const std::string &c, int w, int h, int dpi,
                             LogoCallback cb) {
  PROFILE_SCOPE("LogoCache::RescaleAsync");
  // --- Проверка параметров и паузы ---
  if (w <= 0 || h <= 0 || !master || !master->IsOk() ||
      s_paused.load(std::memory_order_relaxed)) {
    if (cb) {
      auto cb_copy = cb;
      wxTheApp->CallAfter([cb_copy]() { cb_copy(nullptr); });
    }
    return;
  }

  auto sk = MakeScaledKey(p, c, w, h, dpi);
  auto mk = MakeMasterKey(p, c);
  bool needRescale = false;

  // --- Блок синхронизации: проверка кэша и установка pending ---
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    // Проверка кэша на случай, если битмап уже появился
    auto itCache = s_scaledIndex.find(sk);
    if (itCache != s_scaledIndex.end()) {
      auto sp = itCache->second.lock();
      if (sp && sp->IsOk()) {
        if (cb) {
          auto cb_copy = cb;
          auto bmp_copy = sp;
          wxTheApp->CallAfter([cb_copy, bmp_copy]() { cb_copy(bmp_copy); });
        }
        return;
      } else {
        s_scaledIndex.erase(itCache);
      }
    }

    auto it = s_scaledPending.find(sk);
    if (it == s_scaledPending.end() || !it->second.isLoading) {
      s_scaledPending[sk].isLoading = true;
      needRescale = true;
    }
    if (cb) {
      auto &ops = s_scaledPending[sk];
      if (ops.callbacks.size() >= MAX_PENDING_PER_KEY) {
        auto oldCb = ops.callbacks.front();
        ops.callbacks.erase(ops.callbacks.begin());
        if (oldCb) {
          auto cb_copy = oldCb;
          wxTheApp->CallAfter([cb_copy]() { cb_copy(nullptr); });
        }
      }
      ops.callbacks.push_back(cb);
    }
  }

  if (!needRescale) {
    return;
  }
  // rescale
  if (w <= 0 || h <= 0 || !master || !master->IsOk() ||
      s_paused.load(std::memory_order_relaxed)) {
    if (cb) {
      auto cb_copy = cb;
      wxTheApp->CallAfter([cb_copy]() { cb_copy(nullptr); });
    }
    return;
  }

  LogoBitmapPtr masterCopy = master;
  EnsureRescalePoolInitialized();

  s_rescalePool->Enqueue([masterCopy, p, c, w, h, dpi, cb]() {
    wxImage img = masterCopy->ConvertToImage();
    if (!img.IsOk() || img.GetWidth() <= 1 || img.GetHeight() <= 1) {
      if (cb) {
        auto cb_copy = cb;
        wxTheApp->CallAfter([cb_copy]() { cb_copy(nullptr); });
      }
      return;
    }

    // Trim transparent edges
    if (img.HasAlpha()) {
      // ... (вся логика обрезки)
      int iw = img.GetWidth(), ih = img.GetHeight();
      int minX = iw, minY = ih, maxX = -1, maxY = -1;
      const unsigned char *alpha = img.GetAlpha();
      for (int y = 0; y < ih; y++) {
        for (int x = 0; x < iw; x++) {
          if (alpha[y * iw + x] > 0) {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
          }
        }
      }
      if (maxX < minX || maxY < minY)
        return;
      img =
          img.GetSubImage(wxRect(minX, minY, maxX - minX + 1, maxY - minY + 1));
    }

    // Scale
    int origW = img.GetWidth(), origH = img.GetHeight();
    double scale = std::min((double)w / origW, (double)h / origH);
    int newW = std::max(1, (int)(origW * scale));
    int newH = std::max(1, (int)(origH * scale));
    img.Rescale(newW, newH, wxIMAGE_QUALITY_HIGH);

    // Final composition
    auto cb_copy = cb;
    auto mk = LogoCache::MakeMasterKey(p, c);
    auto sk = LogoCache::MakeScaledKey(p, c, w, h, dpi);
    wxTheApp->CallAfter([img = std::move(img), newW, newH, w, h, cb_copy, mk,
                         sk]() {
      if (w <= 0 || h <= 0) {
        if (cb_copy)
          cb_copy(nullptr);
        return;
      }

      wxBitmap finalBmp(w, h);
      wxMemoryDC dc(finalBmp);
      dc.SetBackground(wxBrush(GetDefaultCardBgColor()));
      dc.Clear();

      int x = (w - newW) / 2, y = (h - newH) / 2;

      // Dim logo detection and outline
      bool dim = false;
      {
        long sum = 0;
        int cnt = newW * newH;
        const unsigned char *rgb = img.GetData();
        for (int i = 0; i < cnt; i++) {
          int r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
          sum += (r * 299 + g * 587 + b * 114) / 1000;
        }
        dim = (sum / cnt < 80);
      }

      if (dim) {
        wxImage outline(newW, newH);
        outline.SetRGB(wxRect(0, 0, newW, newH), 255, 255, 255);
        outline.InitAlpha();
        const unsigned char *alpha = img.HasAlpha() ? img.GetAlpha() : nullptr;
        unsigned char *oa = outline.GetAlpha();
        for (int i = 0; i < newW * newH; i++) {
          oa[i] = alpha ? (unsigned char)(alpha[i] * 0.35) : 90;
        }
        dc.DrawBitmap(wxBitmap(outline), x + 1, y + 1, true);
      }

      dc.DrawBitmap(wxBitmap(img), x, y, true);
      dc.SelectObject(wxNullBitmap);

      if (!finalBmp.IsOk()) {
        if (cb_copy)
          cb_copy(nullptr);
        return;
      }

      LogoBitmapPtr bmpPtr = std::make_shared<wxBitmap>(std::move(finalBmp));

      {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto itMaster = s_cache.find(mk);
        if (itMaster == s_cache.end()) {
          // Мастер удалён – не сохраняем, вызываем pending-колбэки с nullptr
          auto pendingIt = s_scaledPending.find(sk);
          if (pendingIt != s_scaledPending.end()) {
            for (auto &pcb : pendingIt->second.callbacks) {
              if (pcb) {
                auto cb_copy = pcb;
                wxTheApp->CallAfter([cb_copy]() { cb_copy(nullptr); });
              }
            }
            s_scaledPending.erase(pendingIt);
          }
          // Выходим, не вызывая дополнительных колбэков (уже вызваны через pending)
          return;
        }
        // Иначе сохраняем в кэш
        auto &entry = s_cache[mk];
        entry.scaled[sk] = bmpPtr; 
        auto now = std::chrono::steady_clock::now();
        entry.scaledLastAccess[sk] = now;
        entry.lastAccess = now;
        s_scaledIndex[sk] = bmpPtr; 
        TouchEntry(mk);

        // Вызываем все ожидающие колбэки с результатом
        auto pendingIt = s_scaledPending.find(sk);
        if (pendingIt != s_scaledPending.end()) {
          for (auto &pcb : pendingIt->second.callbacks) {
            if (pcb) {
              auto cb_copy = pcb;
              auto bmp_copy = bmpPtr;
              wxTheApp->CallAfter([cb_copy, bmp_copy]() { cb_copy(bmp_copy); });
            }
          }
          s_scaledPending.erase(pendingIt);
        }
      }

      MaybeAdjustLimits();
      EnforceLimits();

      if (LogoCache::s_onScaledReady) {
        std::string sk_copy = sk;
        wxTheApp->CallAfter([sk_copy]() {
          std::lock_guard<std::mutex> lk(s_mutex);
          if (LogoCache::s_onScaledReady)
            LogoCache::s_onScaledReady(sk_copy);
        });
      }
    });
  });
}

// --- Main async API ---
void LogoCache::GetLogoAsync(const std::string &p, const std::string &c,
                             const std::string &url, int w, int h, int dpiY,
                             LogoCallback cb) {
  PROFILE_SCOPE("LogoCache::GetLogoAsync");
  if (w <= 0 || h <= 0) {
    if (cb)
      wxTheApp->CallAfter([=]() { cb(nullptr); });
    return;
  }

  int dpi = NormalizeDpi(dpiY);
  auto mk = MakeMasterKey(p, c);
  auto sk = MakeScaledKey(p, c, w, h, dpi);

  LogoBitmapPtr masterCopy = nullptr;

  {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_cache.find(mk);
    if (it != s_cache.end()) {
      auto &e = it->second;

      // --- scaled найден ---
      auto it2 = e.scaled.find(sk);
      if (it2 != e.scaled.end() && it2->second && it2->second->IsOk()) {

        // NEW: обновляем timestamp scaled
        auto now = std::chrono::steady_clock::now();
        e.scaledLastAccess[sk] = now;
        e.lastAccess = now;

        if (cb) {
          auto cb_copy = cb;
          auto bmp_copy = it2->second;
          wxTheApp->CallAfter([cb_copy, bmp_copy]() { cb_copy(bmp_copy); });
        }
        return;
      }

      // --- master найден ---
      if (e.master && e.master->IsOk()) {
        masterCopy = e.master;

        // обновляем timestamp master
        e.lastAccess = std::chrono::steady_clock::now();
        TouchEntry(mk);
      }
    }
  }

  if (s_paused.load(std::memory_order_relaxed)) {
    if (cb)
      wxTheApp->CallAfter([=]() { cb(nullptr); });
    return;
  }

  if (masterCopy) {
    RescaleAsync(masterCopy, p, c, w, h, dpi, cb);
    return;
  }

  EnsureMaster(p, c, url, [=](LogoBitmapPtr masterPtr) {
    if (!masterPtr || !masterPtr->IsOk()) {
      if (cb)
        cb(nullptr);
      return;
    }
    RescaleAsync(masterPtr, p, c, w, h, dpi, cb);
  });
}