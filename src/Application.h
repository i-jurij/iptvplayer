#pragma once

#include "FavoritesManager.h"
#include "GUIManager.h"
#include "epg/EPGManager.h"

#include <wx/app.h>

#include <memory>
#include <string>
#include <wx/timer.h>

class ConfigManager;
class PlaylistManager;
class GUIManager;

class Application : public wxApp {
public:
    Application();
    Application(const std::string& configPath,
                const std::string& configDir);

    ~Application();

    virtual bool OnInit() override;
    virtual int OnExit() override;

    bool start();

    ConfigManager*   getConfigManager()   const noexcept;
    PlaylistManager* getPlaylistManager() const noexcept;
    GUIManager*      getGUIManager()      const noexcept;
    FavoritesManager &getFavoritesManager() { return *m_favoritesManager; }
    EPGManager *GetEPGManager() const { return m_epgManager.get(); }

    void RestartEpgTimer();

  private:
    std::unique_ptr<EPGManager> m_epgManager;
    wxTimer *m_epgTimer = nullptr;
    void OnEpgTimer(wxTimerEvent &event);
    
    std::unique_ptr<ConfigManager>   m_configManager;
    std::unique_ptr<PlaylistManager> m_playlistManager;
    std::unique_ptr<GUIManager>      m_guiManager;
    std::unique_ptr<FavoritesManager> m_favoritesManager;
};

wxDECLARE_APP(Application);
