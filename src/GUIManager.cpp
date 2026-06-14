#include "GUIManager.h"
#include "Application.h"
#include "MainFrame.h"

GUIManager::GUIManager() : m_mainFrame(nullptr), m_application(nullptr) {}

GUIManager::~GUIManager() {
  if (m_mainFrame) {
    m_mainFrame->Destroy();
    m_mainFrame = nullptr;
  }
}

bool GUIManager::initialize() {
  if (m_mainFrame)
    return true;

  m_mainFrame =
      new MainFrame(m_application, m_application->GetPlayerController());

  return m_mainFrame != nullptr;
}

void GUIManager::setApplication(Application *app) {
  m_application = app;

  if (m_mainFrame) {
    m_mainFrame->SetApplication(app);
    m_mainFrame->RefreshPlaylistView();
  }
}
