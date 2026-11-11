#include "IconManager.h"
#include "LogControl.h"
#include "LogoCache.h"

#include <wx/app.h>
#include <wx/dcmemory.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <list>
#include <mutex>
#include <unordered_map>

namespace {

std::mutex s_posterMutex;
std::unordered_map<std::string, LogoCache::LogoBitmapPtr> s_posterCache;
std::list<std::string> s_posterLRU;
std::atomic<size_t> s_maxPoster(500);

struct PendingPoster {
  std::vector<LogoCache::LogoCallback> callbacks;
  bool loading = false;
};
std::unordered_map<std::string, PendingPoster> s_posterPending;

constexpr size_t kMaxPendingPerKey = 16;

void TouchPoster(const std::string &key) {
  auto it = std::find(s_posterLRU.begin(), s_posterLRU.end(), key);
  if (it != s_posterLRU.end())
    s_posterLRU.erase(it);
  s_posterLRU.push_front(key);
}

void EnforcePosterLimit() {
  while (s_posterCache.size() > s_maxPoster.load() && !s_posterLRU.empty()) {
    const std::string oldest = s_posterLRU.back();
    s_posterLRU.pop_back();
    s_posterCache.erase(oldest);
  }
}

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
                                     const std::string &mediaKey, int w, int h,
                                     int scale100) {
  std::string id = playlist.empty() ? mediaKey : playlist + "|" + mediaKey;
  return "P|" + id + "|" + std::to_string(w) + "x" + std::to_string(h) + "|" +
         std::to_string(scale100);
}

LogoCache::LogoBitmapPtr LogoCache::GetCachedPosterPtr(const std::string &key) {
  std::lock_guard<std::mutex> lock(s_posterMutex);
  auto it = s_posterCache.find(key);
  if (it != s_posterCache.end() && it->second && it->second->IsOk()) {
    TouchPoster(key);
    return it->second;
  }
  return nullptr;
}

void LogoCache::SetPosterCacheLimits(size_t maxPosters) {
  s_maxPoster.store(maxPosters);
  std::lock_guard<std::mutex> lock(s_posterMutex);
  EnforcePosterLimit();
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
    s_posterCache.clear();
    s_posterLRU.clear();
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
      MakePosterKey(playlist, mediaKey, targetW, targetH, scale100);

  {
    std::lock_guard<std::mutex> lock(s_posterMutex);

    auto it = s_posterCache.find(key);
    if (it != s_posterCache.end() && it->second && it->second->IsOk()) {
      TouchPoster(key);
      if (cb && wxTheApp) {
        auto ptr = it->second;
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
      if (ptr) {
        s_posterCache[key] = ptr;
        TouchPoster(key);
        EnforcePosterLimit();
      }
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
