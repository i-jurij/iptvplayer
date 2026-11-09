#pragma once

#include <cstdint>
#include <string>

namespace xtream {

struct UserInfo {
  bool auth = false;
  std::string status;
  std::string message;
  int64_t expDate = 0;
  int maxConnections = 0;
  int activeConnections = 0;
  bool isTrial = false;
  int64_t createdAt = 0;

  bool hasExpDate() const { return expDate > 0; }
};

struct ServerInfo {
  std::string url;
  std::string port;
  std::string httpsPort;
  std::string protocol;
  std::string timeNow;
};

UserInfo ParseUserInfo(const std::string &json);
ServerInfo ParseServerInfo(const std::string &json);

std::string HumanStatus(const UserInfo &info);

std::string FormatExpiryDate(int64_t expEpoch);
bool IsExpiringSoon(int64_t expEpoch, int64_t nowEpoch, int days = 3);

} // namespace xtream
