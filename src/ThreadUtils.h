#pragma once
#include "MainFrame.h"
#include "PlaylistManager.h"
#include <cstddef>
#include <iostream>
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

// Отправка события в MainFrame
// status - основной код (например, успех/неудача или количество)
// index - дополнительный параметр (по умолчанию -1, означает "не использовать")
inline void postEvent(MainFrame *frame, wxEventType evtType, int status,
                      int index = -1) {
  if (!frame || frame->IsBeingDeleted() || frame->isClosing())
    return;
  wxTheApp->CallAfter([frame, evtType, status, index]() {
    if (frame && !frame->IsBeingDeleted() && !frame->isClosing()) {
      wxCommandEvent ev(evtType);
      ev.SetInt(status);
      if (index != -1) {
        ev.SetExtraLong(index);
      }
      frame->GetEventHandler()->ProcessEvent(ev);
    }
  });
}

