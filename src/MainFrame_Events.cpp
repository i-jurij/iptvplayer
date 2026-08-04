#include "IconManager.h"
#include "MainFrame.h"
#include "SettingsDialog.h"
#include "UpdateAllThread.h"
#include "UpdateOneThread.h"
#include "Utils.h"

#include <wx/msgdlg.h>

void MainFrame::onSettings(wxCommandEvent &WXUNUSED(event)) {
  SettingsDialog dlg(this, getConfigManager());
  dlg.ShowModal();
}

void MainFrame::onQuit(wxCommandEvent &WXUNUSED(event)) {
  IconManager::Shutdown();
  m_closing = true;
  Close(true);
}

void MainFrame::onAbout(wxCommandEvent &WXUNUSED(event)) {
  showInfo(this,
           "IPTV Player v1.0.0\n\n"
           "Milestone 5: EPG Management\n\n"
           "Author: I-Jurij",
           "About IPTV Player");
}

void MainFrame::onToggleFavoritesView(wxCommandEvent &) {
  if (m_favViewToggle->GetValue()) {
    m_favViewBook->ChangeSelection(1); // Grid
    m_favViewToggle->SetLabel("List View");
  } else {
    m_favViewBook->ChangeSelection(0); // List
    m_favViewToggle->SetLabel("Grid View");
  }
}
