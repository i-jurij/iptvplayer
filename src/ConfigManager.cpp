#include "ConfigManager.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>

ConfigManager::ConfigManager(const std::string &configPath)
    : m_configPath(configPath), m_loaded(false) {}

ConfigManager::~ConfigManager() {
  if (m_loaded) {
    saveSettings();
  }
}

std::string ConfigManager::getConfigDirectory() const {
  namespace fs = std::filesystem;
  fs::path p(m_configPath);
  auto parent = p.parent_path();
  if (parent.empty())
    return ".";
  return parent.string();
}

ErrorCode ConfigManager::ensureConfigDirectory() {
  namespace fs = std::filesystem;
  fs::path dirPath = getConfigDirectory();
  std::error_code ec;
  fs::create_directories(dirPath, ec);
  if (ec) {
    setError("Cannot create config directory: " + dirPath.string());
    return ErrorCode::FileNotFound;
  }
  return ErrorCode::OK;
}

ErrorCode ConfigManager::createDefaultSettings() {
  m_settings["version"] = "1.0.0";
  m_settings["window_width"] = "-1";
  m_settings["window_height"] = "-1";
  m_settings["window_x"] = "-1";
  m_settings["window_y"] = "-1";
  m_settings["auto_update"] = "false";
  m_settings["media_player_path"] = "";
  m_settings["last_opened_playlist"] = "";
  m_settings["playlistTimeoutMs"] = "30000";
  m_settings["nologo"] = "true";
  m_settings["channels_grid_threshold"] = "300";
  m_settings["favorites_grid_threshold"] = "300";

  m_loaded = true;

  return ErrorCode::OK;
}

ErrorCode ConfigManager::loadSettings() {
  namespace fs = std::filesystem;

  try {
    // Проверяем наличие файла
    if (!fs::exists(m_configPath)) {
      std::cout << "Config file not found, creating default configuration\n";
      createDefaultSettings();
      m_loaded = true;
      return saveSettings();
    }

    // Проверяем размер файла
    std::error_code ec;
    auto fileSize = fs::file_size(m_configPath, ec);
    if (ec || fileSize == 0 || fileSize > 5 * 1024 * 1024) {
      std::cerr << "Config file invalid or too large, recreating\n";
      createDefaultSettings();
      m_loaded = true;
      return saveSettings();
    }

    // Читаем файл
    std::ifstream file(m_configPath, std::ios::binary);
    if (!file.is_open()) {
      setError("Cannot open config file: " + m_configPath);
      createDefaultSettings();
      m_loaded = true;
      return saveSettings();
    }

    std::string content;
    content.resize(fileSize);

    if (!file.read(&content[0], fileSize)) {
      setError("Failed to read config file");
      createDefaultSettings();
      m_loaded = true;
      return saveSettings();
    }

    file.close();

    // Парсим JSON
    rapidjson::Document doc;
    if (doc.Parse(content.c_str()).HasParseError()) {
      setError("Failed to parse config JSON");
      createDefaultSettings();
      m_loaded = true;
      return saveSettings();
    }

    if (!doc.HasMember("settings") || !doc["settings"].IsObject()) {
      setError("Invalid config structure");
      createDefaultSettings();
      m_loaded = true;
      return saveSettings();
    }

    // Загружаем настройки
    m_settings.clear();
    for (auto it = doc["settings"].MemberBegin();
         it != doc["settings"].MemberEnd(); ++it) {
      if (it->value.IsString())
        m_settings[it->name.GetString()] = it->value.GetString();
    }

    m_loaded = true;
    return ErrorCode::OK;
  } catch (const std::exception &e) {
    setError(std::string("Config exception: ") + e.what());
    createDefaultSettings();
    m_loaded = true;
    return saveSettings();
  } catch (...) {
    setError("Unknown config exception");
    createDefaultSettings();
    m_loaded = true;
    return saveSettings();
  }
}

ErrorCode ConfigManager::saveSettings() {
  if (ensureConfigDirectory() != ErrorCode::OK)
    return ErrorCode::FileNotFound;

  try {
    std::ofstream file(m_configPath);
    if (!file.is_open()) {
      setError("Cannot open config file for writing: " + m_configPath);
      return ErrorCode::FileNotFound;
    }

    rapidjson::Document doc;
    doc.SetObject();
    auto &alloc = doc.GetAllocator();

    rapidjson::Value settingsObj(rapidjson::kObjectType);
    for (const auto &[key, value] : m_settings) {
      settingsObj.AddMember(rapidjson::Value(key.c_str(), alloc),
                            rapidjson::Value(value.c_str(), alloc), alloc);
    }

    doc.AddMember("settings", settingsObj, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    file << buffer.GetString();
    file.close();

    return ErrorCode::OK;
  } catch (const std::exception &e) {
    setError(std::string("Config save exception: ") + e.what());
    return ErrorCode::Unknown;
  } catch (...) {
    setError("Unknown config save exception");
    return ErrorCode::Unknown;
  }
}

std::string ConfigManager::getSetting(const std::string &key,
                                      const std::string &defaultValue) const {
  auto it = m_settings.find(key);
  if (it != m_settings.end())
    return it->second;
  return defaultValue;
}

void ConfigManager::setSetting(const std::string &key,
                               const std::string &value) {
  m_settings[key] = value;
}

void ConfigManager::removeSetting(const std::string &key) {
  auto it = m_settings.find(key);
  if (it != m_settings.end())
    m_settings.erase(it);
}

void ConfigManager::setError(const std::string &msg) {
  m_lastError = msg;
  std::cerr << "[ConfigManager] Error: " << msg << "\n";
}

int ConfigManager::getInt(const std::string &key, int defaultValue) const {
  auto it = m_settings.find(key);
  if (it != m_settings.end()) {
    try {
      return std::stoi(it->second);
    } catch (...) {
      return defaultValue;
    }
  }
  return defaultValue;
}

void ConfigManager::setInt(const std::string &key, int value) {
  m_settings[key] = std::to_string(value);
}

std::vector<std::string> ConfigManager::getRecentFiles() const {
  std::vector<std::string> out;

  auto it = m_settings.find("video_recent");
  if (it == m_settings.end())
    return out;

  std::stringstream ss(it->second);
  std::string item;

  while (std::getline(ss, item, ';')) {
    if (!item.empty())
      out.push_back(item);
  }

  return out;
}

void ConfigManager::setRecentFiles(const std::vector<std::string> &files) {
  std::string joined;

  for (size_t i = 0; i < files.size(); ++i) {
    joined += files[i];
    if (i + 1 < files.size())
      joined += ";";
  }

  m_settings["video_recent"] = joined;
  saveSettings();
}
