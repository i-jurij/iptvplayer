#include "Application.h"
#include "ConfigManager.h"
#include "ErrorCode.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "PlaylistManager.h"
#include "epg/EPGManager.h"

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

  m_configDir = baseDir;
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

  // init UI
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

  if (m_epgManager) {
    m_epgManager->SaveSourcesToConfig();
  }
}

bool Application::OnInit() {
  try {
    setlocale(LC_ALL, "");
    wxLocale *m_locale = new wxLocale();

    // log enable
    wxLog::SetLogLevel(wxLOG_Debug);
    wxLog::SetActiveTarget(new wxLogStderr());

    wxInitAllImageHandlers();

    if (!m_locale->Init(wxLANGUAGE_DEFAULT)) {
      LOG_ERROR("Failed to initialize locale");
    } else {
      m_locale->AddCatalog("iptvplayer");
    }

    // 1) Загружаем настройки
    ErrorCode cfgStatus = m_configManager->loadSettings();
    if (cfgStatus != ErrorCode::OK) {
      std::cerr << "Failed to load settings: "
                << m_configManager->getLastError() << std::endl;
      wxMessageBox("Failed to load settings.\nSee log for details.", "Error",
                   wxOK | wxICON_ERROR);
      return false;
    }

    // 2) Создаём EPGManager
    m_epgManager = std::make_unique<EPGManager>(m_configManager.get(),
                                                m_playlistManager.get());

    // 3) Устанавливаем путь к БД (вместе с конфигом, а не в кэше)
    wxString dbPath = m_configDir + "/epg.db";
    m_epgManager->SetDbPath(dbPath.ToUTF8().data());
    m_epgManager->OpenDatabase();

    // 4) Удаляем старый JSON-кэш (если остался)
    wxString oldCachePath =
        wxFileName::GetHomeDir() + "/.cache/iptvplayer/epg/epg_cache.json";
    if (wxFileExists(oldCachePath)) {
      wxRemoveFile(oldCachePath);
      LOG_DEBUG("Removed old EPG cache file: %s", oldCachePath.ToUTF8().data());
    }

    // 5) Инициализируем GUI
    if (!m_guiManager->initialize()) {
      LOG_ERROR("Failed to initialize GUI", "Error", wxOK | wxICON_ERROR);
      return false;
    }

    MainFrame *mf = m_guiManager->getMainFrame();
    mf->Show(true);

    // 6) Старт приложения
    if (!start()) {
      LOG_ERROR("Failed to initialize application", "Error",
                wxOK | wxICON_ERROR);
      return false;
    }

    CallAfter([mf]() {
      if (mf) {
        mf->RefreshPlaylistView();
        mf->startAutoUpdateFromSavedPlaylists();
      }
    });

    return true;

  } catch (const std::exception &e) {
    LOG_ERROR(wxString::Format("OnInit std::exception: %s", e.what()));
    return false;
  } catch (...) {
    LOG_ERROR("OnInit unknown exception");
    return false;
  }
}

bool Application::start() {
  // Загрузка плейлистов
  ErrorCode plStatus = m_playlistManager->loadPlaylists();
  if (plStatus != ErrorCode::OK) {
    std::cerr << "Failed to load playlists: "
              << m_playlistManager->getLastError() << std::endl;
  }

  // Инициализация EPGManager
  if (m_epgManager) {
    m_epgManager->StartAutoUpdate();

    // Загрузка региональных суффиксов из конфигурационной директории
    std::string suffixesPath =
        (m_configDir + "/regional_suffixes.json").ToUTF8().data();
    m_epgManager->LoadRegionalSuffixes(suffixesPath);
  }

  std::cout << "Application started successfully" << std::endl;
  return true;
}

int Application::OnExit() { return wxApp::OnExit(); }

PlaylistManager *Application::getPlaylistManager() const noexcept {
  return m_playlistManager.get();
}

ConfigManager *Application::getConfigManager() const noexcept {
  return m_configManager.get();
}

GUIManager *Application::getGUIManager() const noexcept {
  return m_guiManager.get();
}
