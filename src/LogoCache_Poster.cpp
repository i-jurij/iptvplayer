#include "IconManager.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "Utils_Cache.h"

#include <wx/app.h>
#include <wx/dcmemory.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>

namespace {

std::mutex s_posterMutex;
// Poster cache: shared LruCache (onEvict left default-empty — no eviction
// callback needed for posters; pending callbacks keyed by key in
// s_posterPending are unaffected by eviction of already-cached entries).
LruCache<std::string, LogoCache::LogoBitmapPtr> s_posterCache(500);

struct PendingPoster {
  std::vector<LogoCache::LogoCallback> callbacks;
  bool loading = false;
};
std::unordered_map<std::string, PendingPoster> s_posterPending;

constexpr size_t kMaxPendingPerKey = 16;

wxBitmap ScalePosterToTarget(const wxBitmap &master, int targetW, int targetH,
                             const wxColour &bgColor) {
  if (!master.IsOk() || targetW <= 0 || targetH <= 0)
    return wxBitmap();

  wxImage img = master.ConvertToImage();
  if (!img.IsOk())
    return wxBitmap();

  const int origW = img.GetWidth();
  const int origH = img.GetHeight();
  if (origW <= 0 || origH <= 0)
    return wxBitmap();

  const double scale = std::min(static_cast<double>(targetW) / origW,
                                static_cast<double>(targetH) / origH);
  const int newW = std::max(1, static_cast<int>(origW * scale));
  const int newH = std::max(1, static_cast<int>(origH * scale));

  img.Rescale(newW, newH, wxIMAGE_QUALITY_HIGH);

  wxBitmap out(targetW, targetH);
  {
    wxMemoryDC mdc(out);
    mdc.SetBackground(wxBrush(bgColor));
    mdc.Clear();
    mdc.DrawBitmap(wxBitmap(img), (targetW - newW) / 2, (targetH - newH) / 2,
                   true);
  }
  return out;
}

} // namespace

std::string LogoCache::MakePosterKey(const std::string &playlist,
                                     const std::string &mediaKey,
                                     const std::string &posterUrl, int w, int h,
                                     int scale100) {
  std::string id = playlist.empty() ? mediaKey : playlist + "|" + mediaKey;
  const std::size_t urlHash = std::hash<std::string>{}(posterUrl);
  char hashBuf[20];
  std::snprintf(hashBuf, sizeof(hashBuf), "%016zx", urlHash);
  return "P|" + id + "|" + hashBuf + "|" + std::to_string(w) + "x" +
         std::to_string(h) + "|" + std::to_string(scale100);
}

LogoCache::LogoBitmapPtr LogoCache::GetCachedPosterPtr(const std::string &key) {
  std::lock_guard<std::mutex> lock(s_posterMutex);
  auto *p = s_posterCache.Get(key);
  if (p && *p && (*p)->IsOk())
    return *p;
  return nullptr;
}

void LogoCache::SetPosterCacheLimits(size_t maxPosters) {
  std::lock_guard<std::mutex> lock(s_posterMutex);
  s_posterCache.SetMaxSize(maxPosters);
}

void LogoCache::ClearPosterMemory() {
  std::vector<LogoCallback> pending;
  {
    std::lock_guard<std::mutex> lock(s_posterMutex);
    for (auto &kv : s_posterPending)
      for (auto &cb : kv.second.callbacks)
        if (cb)
          pending.push_back(cb);
    s_posterPending.clear();
    s_posterCache.Clear();
  }

  if (wxTheApp) {
    for (auto &cb : pending) {
      auto cbCopy = cb;
      wxTheApp->CallAfter([cbCopy]() { cbCopy(nullptr); });
    }
  }
}

void LogoCache::GetPosterAsync(const std::string &playlist,
                               const std::string &mediaKey,
                               const std::string &url, int targetW, int targetH,
                               const wxColour &bgColor, LogoCallback cb,
                               double scaleFactor) {
  if (targetW <= 0 || targetH <= 0 || url.empty()) {
    if (cb && wxTheApp)
      wxTheApp->CallAfter([cb]() { cb(nullptr); });
    return;
  }
  if (scaleFactor < 1.0)
    scaleFactor = 1.0;
  const int scale100 =
      std::max(100, static_cast<int>(std::round(scaleFactor * 100.0)));
  const std::string key =
      MakePosterKey(playlist, mediaKey, url, targetW, targetH, scale100);

  {
    std::lock_guard<std::mutex> lock(s_posterMutex);

    auto *cachedPtr = s_posterCache.Get(key);
    if (cachedPtr && *cachedPtr && (*cachedPtr)->IsOk()) {
      if (cb && wxTheApp) {
        auto ptr = *cachedPtr;
        wxTheApp->CallAfter([cb, ptr]() { cb(ptr); });
      }
      return;
    }

    auto &pending = s_posterPending[key];
    if (pending.loading) {
      if (cb) {
        if (pending.callbacks.size() < kMaxPendingPerKey) {
          pending.callbacks.push_back(cb);
        } else if (wxTheApp) {
          wxTheApp->CallAfter([cb]() { cb(nullptr); });
        }
      }
      return;
    }

    pending.loading = true;
    if (cb)
      pending.callbacks.push_back(cb);
  }

  IconManager::EnsureIconAsync(playlist, mediaKey, url, [=](wxBitmap master) {
    if (!master.IsOk() || master.GetWidth() < 2 || master.GetHeight() < 2) {
      std::vector<LogoCallback> cbs;
      {
        std::lock_guard<std::mutex> lock(s_posterMutex);
        auto it = s_posterPending.find(key);
        if (it != s_posterPending.end()) {
          cbs.swap(it->second.callbacks);
          s_posterPending.erase(it);
        }
      }
      if (wxTheApp) {
        for (auto &c : cbs) {
          if (c) {
            auto cc = c;
            wxTheApp->CallAfter([cc]() { cc(nullptr); });
          }
        }
      }
      return;
    }

    wxBitmap scaled = ScalePosterToTarget(master, targetW, targetH, bgColor);
    LogoBitmapPtr ptr;
    if (scaled.IsOk())
      ptr = std::make_shared<wxBitmap>(std::move(scaled));

    std::vector<LogoCallback> cbs;
    {
      std::lock_guard<std::mutex> lock(s_posterMutex);
      auto it = s_posterPending.find(key);
      if (it != s_posterPending.end()) {
        cbs.swap(it->second.callbacks);
        s_posterPending.erase(it);
      }
      if (ptr)
        s_posterCache.Put(key, ptr);
    }

    if (wxTheApp) {
      for (auto &c : cbs) {
        if (c) {
          auto cc = c;
          auto p = ptr;
          wxTheApp->CallAfter([cc, p]() { cc(p); });
        }
      }
    }
  });
}
