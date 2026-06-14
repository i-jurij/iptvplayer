#include "Application.h"
#include "ConfigManager.h"
#include "ErrorCode.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "PlaylistManager.h"
#include "BackendFactory.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include <iostream>

Application::Application() {
  wxString baseDir;

#if defined(__linux__)
  const char *home = std::getenv("HOME");
  if (!home)
    home = ".";
  baseDir = wxString(home) + "/.config/iptvplayer";
#elif defined(__APPLE__)
  const char *home = std::getenv("HOME");
  if (!home)
    home = ".";
  baseDir = wxString(home) + "/Library/Application Support/iptvplayer";
#elif defined(_WIN32)
  baseDir = wxStandardPaths::Get().GetUserConfigDir() + "\\iptvplayer";
#else
  const char *home = std::getenv("HOME");
  if (!home)
    home = ".";
  baseDir = wxString(home) + "/iptvplayer";
#endif

  wxFileName configFile(baseDir, "config.json");
  wxFileName playlistsPath(baseDir, "");

#ifdef _WIN32
  m_configManager =
      std::make_unique<ConfigManager>(configFile.GetFullPath().ToStdWstring());
  m_playlistManager = std::make_unique<PlaylistManager>(
      playlistsPath.GetFullPath().ToStdWstring());
#else
  m_configManager =
      std::make_unique<ConfigManager>(configFile.GetFullPath().ToUTF8().data());
  m_playlistManager = std::make_unique<PlaylistManager>(
      playlistsPath.GetFullPath().ToUTF8().data());
#endif

  wxString favPathWx =
      configFile.GetPath() + wxFILE_SEP_PATH + "favorites.json";
  m_favoritesManager =
      std::make_unique<FavoritesManager>(favPathWx.ToStdString());

  m_guiManager = std::make_unique<GUIManager>();
  m_guiManager->setApplication(this);
}

Application::~Application() {
  if (m_configManager) {
    ErrorCode ec = m_configManager->saveSettings();
    if (ec != ErrorCode::OK) {
      std::cerr << "Failed to save settings: "
                << m_configManager->getLastError() << std::endl;
    }
  }
}

bool Application::OnInit() {
  try {
    setlocale(LC_ALL, "");
    wxLocale *m_locale = new wxLocale();

    // В релизе :
    // ENABLE_PROFILER = 0,
    // g_verboseLogging = false,
    // wxLog::SetActiveTarget(new wxLogNull())

    // log enable
    wxLog::SetLogLevel(wxLOG_Debug);
    // wxLog::SetLogLevel(wxLOG_Info);

    // Перенаправляем все сообщения wxLog в консоль
    wxLog::SetActiveTarget(new wxLogStderr());
    // Чтобы полностью отключить вывод:
    // wxLog::SetActiveTarget(new wxLogNull());

    wxInitAllImageHandlers();
    // degug handlers
    // auto &list = wxImage::GetHandlers();
    // for (auto node = list.GetFirst(); node; node = node->GetNext()) {
    //      wxImageHandler *h = static_cast<wxImageHandler *>(node->GetData());
    // LOG_DEBUG("Handler: %s", h->GetName());
    //  }

    if (!m_locale->Init(wxLANGUAGE_DEFAULT)) {
      wxLogError("Failed to initialize locale");
    } else {
      m_locale->AddCatalog("iptvplayer");
    }

    if (!start()) {
      wxLogError("Failed to initialize application", "Error",
                 wxOK | wxICON_ERROR);
      return false;
    }

    if (!m_guiManager->initialize()) {
      wxLogError("Failed to initialize GUI", "Error", wxOK | wxICON_ERROR);
      return false;
    }

    MainFrame *mf = m_guiManager->getMainFrame();
    mf->Show(true);
    CallAfter([mf]() {
      if (mf) {
        mf->RefreshPlaylistView();
        mf->startAutoUpdateFromSavedPlaylists();
      }
    });

    return true;

  } catch (const std::exception &e) {
    wxLogError(wxString::Format("OnInit std::exception: %s", e.what()));
    return false;
  } catch (...) {
    wxLogError("OnInit unknown exception");
    return false;
  }
}

int Application::OnExit() { return 0; }

bool Application::start() {
  ErrorCode cfgStatus = m_configManager->loadSettings();

  if (cfgStatus != ErrorCode::OK) {
    std::cerr << "Failed to load settings: " << m_configManager->getLastError()
              << std::endl;
    return false;
  }

  ErrorCode plStatus = m_playlistManager->loadPlaylists();
  if (plStatus != ErrorCode::OK) {
    std::cerr << "Failed to load playlists: "
              << m_playlistManager->getLastError() << std::endl;
  }

  auto backend = CreateBackend();
  m_playerController = std::make_unique<PlayerController>(std::move(backend));
  std::cout << "Backend initialized: " << m_playerController->GetBackendName()
            << std::endl;

  std::cout << "Application started successfully" << std::endl;
  return true;
}

PlaylistManager *Application::getPlaylistManager() const noexcept {
  return m_playlistManager.get();
}

ConfigManager *Application::getConfigManager() const noexcept {
  return m_configManager.get();
}

GUIManager *Application::getGUIManager() const noexcept {
  return m_guiManager.get();
}
