#pragma once

#include "GUIManager.h"
#include "FavoritesManager.h"

#include <wx/app.h>

#include <memory>
#include <string>

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
    FavoritesManager& getFavoritesManager() { return *m_favoritesManager; }

private:
    std::unique_ptr<ConfigManager>   m_configManager;
    std::unique_ptr<PlaylistManager> m_playlistManager;
    std::unique_ptr<GUIManager>      m_guiManager;
    std::unique_ptr<FavoritesManager> m_favoritesManager;
};

wxDECLARE_APP(Application);
