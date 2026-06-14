#pragma once
#include "ErrorCode.h"
#include <string>
#include <unordered_map>
#include <vector>

class ConfigManager {
public:
    explicit ConfigManager(const std::string& configPath);
    ~ConfigManager();

    // Загрузка и сохранение настроек приложения
    ErrorCode loadSettings();
    ErrorCode saveSettings();

    // Доступ к настройкам
    std::string getSetting(const std::string& key,
                           const std::string& defaultValue = "") const;
    void setSetting(const std::string& key, const std::string& value);
    void removeSetting(const std::string& key);

    // Директория конфигурации
    std::string getConfigDirectory() const;

    // Последняя ошибка
    const std::string& getLastError() const { return m_lastError; }

    int getInt(const std::string& key, int defaultValue) const;
    void setInt(const std::string &key, int value);
    
    std::vector<std::string> getRecentFiles() const;
    void setRecentFiles(const std::vector<std::string> &files);

  private:
    std::string m_configPath;
    bool m_loaded;

    std::unordered_map<std::string, std::string> m_settings;
    mutable std::string m_lastError;

    void setError(const std::string& msg);

    ErrorCode ensureConfigDirectory();
    ErrorCode createDefaultSettings();
};

