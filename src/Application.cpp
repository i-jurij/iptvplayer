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

  // Инициализация EPGManager
  m_epgManager = std::make_unique<EPGManager>(m_configManager.get(),
                                              m_playlistManager.get());

  // Определяем путь кэша EPG
  wxString cacheDir;
#if defined(__linux__)
  cacheDir = wxFileName::GetHomeDir() + "/.cache/iptvplayer/epg";
#elif defined(__APPLE__)
  cacheDir = wxFileName::GetHomeDir() + "/Library/Caches/iptvplayer/epg";
#elif defined(_WIN32)
  cacheDir =
      wxStandardPaths::Get().GetUserLocalDataDir() + "\\iptvplayer\\cache\\epg";
#else
  cacheDir = wxFileName::GetHomeDir() + "/.cache/iptvplayer/epg";
#endif

  if (!wxFileName::DirExists(cacheDir)) {
    wxFileName::Mkdir(cacheDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  m_epgManager->SetCachePath(cacheDir.ToUTF8().data());

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
    m_epgManager->SaveToCache();
    m_epgTimer->Stop();
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

    if (!m_locale->Init(wxLANGUAGE_DEFAULT)) {
      LOG_ERROR("Failed to initialize locale");
    } else {
      m_locale->AddCatalog("iptvplayer");
    }

    // 1) СНАЧАЛА грузим настройки, чтобы MainFrame видел уже загруженный конфиг
    ErrorCode cfgStatus = m_configManager->loadSettings();
    if (cfgStatus != ErrorCode::OK) {
      std::cerr << "Failed to load settings: "
                << m_configManager->getLastError() << std::endl;
      wxMessageBox("Failed to load settings.\nSee log for details.", "Error",
                   wxOK | wxICON_ERROR);
      return false;
    }

    // 2) Затем инициализируем GUI (создаём главное окно)
    if (!m_guiManager->initialize()) {
      LOG_ERROR("Failed to initialize GUI", "Error", wxOK | wxICON_ERROR);
      return false;
    }

    MainFrame *mf = m_guiManager->getMainFrame();
    mf->Show(true);

    // 3) Старт приложения (загрузка плейлистов и прочее)
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
  // Настройки уже загружены в OnInit(), здесь только плейлисты
  ErrorCode plStatus = m_playlistManager->loadPlaylists();
  if (plStatus != ErrorCode::OK) {
    std::cerr << "Failed to load playlists: "
              << m_playlistManager->getLastError() << std::endl;
    // плейлисты могут быть пустыми/ошибочными, но это не критично для старта
    // GUI
  }

  // Инициализация EPGManager и таймера
  if (m_epgManager) {
    m_epgManager->LoadFromCache();
    if (m_epgManager->IsAutoUpdateEnabled()) {
      // Инициализация таймера (перенесена из конструктора)
      m_epgTimer->SetOwner(this);
      Bind(wxEVT_TIMER, &Application::OnEpgTimer, this, m_epgTimer->GetId());

      int intervalHours = m_epgManager->GetUpdateIntervalHours();
      if (intervalHours < 1)
        intervalHours = 1;
      
      long intervalMs = intervalHours * 3600 * 1000;
      m_epgTimer->Start(intervalMs, wxTIMER_CONTINUOUS);
      LOG_DEBUG(
          "Application: EPG auto-update timer started (interval %d hours)",
          intervalHours);
    }
  }

  std::cout << "Application started successfully" << std::endl;
  return true;
}

int Application::OnExit() {
  m_epgTimer->Stop();
  return 0;
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

void Application::OnEpgTimer(wxTimerEvent &) {
  if (!m_epgManager)
    return;
  LOG_DEBUG("Application: EPG auto-update timer triggered");
  m_epgManager->Refresh();
}
