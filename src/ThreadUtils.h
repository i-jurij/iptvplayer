#pragma once
#include "MainFrame.h"
#include "PlaylistManager.h"
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <wx/event.h>

// Проверка менеджера и индекса
inline bool validatePlaylistAccess(PlaylistManager* pm, std::size_t idx) {
    if (!pm) {
        std::cerr << "ThreadUtils: PlaylistManager is null" << std::endl;
        return false;
    }
    if (idx >= pm->size()) {
        std::cerr << "ThreadUtils: Invalid playlist index " << idx
                  << " (size=" << pm->size() << ")" << std::endl;
        return false;
    }
    if (!pm->getPlaylist(idx)) {
        std::cerr << "ThreadUtils: Null playlist at index " << idx << std::endl;
        return false;
    }
    return true;
}

// Safely resolve MainFrame* from the shared handle under mutex.
// Returns nullptr if frame was already destroyed.
inline MainFrame* resolveFrame(const std::shared_ptr<FrameHandle> &handle) {
    if (!handle) return nullptr;
    std::lock_guard<std::mutex> lk(handle->mutex);
    return handle->frame;
}

// Post EVT_UPDATE_PROGRESS-like event to MainFrame (UI thread).
// Safe to call from detached worker even if MainFrame is destroyed — the
// shared_ptr<FrameHandle> keeps the handle alive, and `frame` is nulled
// at the start of ~MainFrame.
inline void postEvent(std::shared_ptr<FrameHandle> handle, wxEventType evtType,
                      int status, int index = -1) {
    if (!handle || !wxTheApp) return;
    wxTheApp->CallAfter([handle, evtType, status, index]() {
        MainFrame *f = resolveFrame(handle);
        if (!f || f->IsBeingDeleted() || f->isClosing()) return;
        wxCommandEvent ev(evtType);
        ev.SetInt(status);
        if (index != -1) ev.SetExtraLong(index);
        f->GetEventHandler()->ProcessEvent(ev);
    });
}

// Post EVT_UPDATE_PROGRESS (processed/total) to MainFrame (UI thread).
inline void postUpdateProgress(std::shared_ptr<FrameHandle> handle,
                               int processed, int total) {
    if (!handle || !wxTheApp) return;
    wxTheApp->CallAfter([handle, processed, total]() {
        MainFrame *f = resolveFrame(handle);
        if (!f || f->IsBeingDeleted() || f->isClosing()) return;
        wxCommandEvent ev(EVT_UPDATE_PROGRESS);
        ev.SetInt(processed);
        ev.SetExtraLong(total);
        f->GetEventHandler()->ProcessEvent(ev);
    });
}

