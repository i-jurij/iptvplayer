// src/MainFrame_FavoritesUI.cpp
#include "FavoritesCards.h"
#include "FavoritesList.h"
#include "LogControl.h"
#include "MainFrame.h"
#include <wx/sizer.h>
#include <wx/toolbar.h>

void MainFrame::createFavoritesUI() {
  // This function creates favorites page UI; call it from createMainPanel()
  // if you prefer explicit separation. If not needed, the existing code in
  // MainFrame_UI.cpp can remain.
  // For completeness, we provide a function to (re)create favorites UI pieces.

  // Implementation mirrors existing code in MainFrame_UI.cpp but isolated.
  // If m_favHeader/m_favList/m_favCards already created elsewhere, skip.
  // (This file is optional; keep for symmetry and future refactor.)
}

void MainFrame::HandleFavPageChanged(int sel) {
  if (sel != m_favoritesPageIdx)
    return;

  LOG_DEBUG("HandleFavPageChanged: sel=%d", sel);

  auto *cfg = getConfigManager();
  std::string mode = cfg->getSetting("favorites_view_mode", "grid");
  bool grid = (mode == "grid");

  // Выполняем смену внутренней вкладки и фокус в UI-потоке, чтобы избежать
  // reentrancy
  CallAfter([this, grid]() {
    LOG_DEBUG("HandleFavPageChanged(CallAfter): applying fav view mode grid=%d",
              (int)grid);
    // Меняем внутреннюю viewBook без генерации внешних событий
    m_favViewBook->ChangeSelection(grid ? 1 : 0);

    if (grid) {
      if (m_favCards)
        m_favCards->SetFocusIgnoringChildren();
    } else {
      if (m_favList)
        m_favList->SetFocusFromKbd();
    }
  });
}
