#include "xtream/XtreamInfo.h"

#include "Utils_UI.h"

#include <rapidjson/document.h>

#include <ctime>

namespace xtream {

namespace {

int64_t Int64FromAny(const rapidjson::Value &v) {
  if (v.IsNull())
    return 0;
  if (v.IsInt64())
    return v.GetInt64();
  if (v.IsInt())
    return v.GetInt();
  if (v.IsUint())
    return static_cast<int64_t>(v.GetUint());
  if (v.IsUint64())
    return static_cast<int64_t>(v.GetUint64());
  if (v.IsDouble())
    return static_cast<int64_t>(v.GetDouble());
  if (v.IsString()) {
    try {
      return std::stoll(v.GetString());
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

bool BoolFromAny(const rapidjson::Value &v) {
  if (v.IsBool())
    return v.GetBool();
  if (v.IsInt())
    return v.GetInt() != 0;
  if (v.IsString()) {
    const std::string s = v.GetString();
    return s == "1" || s == "true" || s == "True";
  }
  return false;
}

std::string StringFromAny(const rapidjson::Value &v) {
  if (v.IsString())
    return v.GetString();
  if (v.IsInt64())
    return std::to_string(v.GetInt64());
  if (v.IsInt())
    return std::to_string(v.GetInt());
  if (v.IsUint64())
    return std::to_string(v.GetUint64());
  if (v.IsUint())
    return std::to_string(v.GetUint());
  return std::string();
}

} // namespace

UserInfo ParseUserInfo(const std::string &json) {
  UserInfo out;
  if (json.empty())
    return out;

  rapidjson::Document d;
  if (d.Parse(json.c_str()).HasParseError() || !d.IsObject())
    return out;

  if (d.HasMember("auth"))
    out.auth = BoolFromAny(d["auth"]);
  if (d.HasMember("status"))
    out.status = StringFromAny(d["status"]);
  if (d.HasMember("message"))
    out.message = StringFromAny(d["message"]);
  if (d.HasMember("exp_date"))
    out.expDate = Int64FromAny(d["exp_date"]);
  if (d.HasMember("max_connections"))
    out.maxConnections = static_cast<int>(Int64FromAny(d["max_connections"]));
  if (d.HasMember("active_cons"))
    out.activeConnections = static_cast<int>(Int64FromAny(d["active_cons"]));
  if (d.HasMember("is_trial"))
    out.isTrial = BoolFromAny(d["is_trial"]);
  if (d.HasMember("created_at"))
    out.createdAt = Int64FromAny(d["created_at"]);

  return out;
}

ServerInfo ParseServerInfo(const std::string &json) {
  ServerInfo out;
  if (json.empty())
    return out;

  rapidjson::Document d;
  if (d.Parse(json.c_str()).HasParseError() || !d.IsObject())
    return out;

  if (d.HasMember("url"))
    out.url = StringFromAny(d["url"]);
  if (d.HasMember("port"))
    out.port = StringFromAny(d["port"]);
  if (d.HasMember("https_port"))
    out.httpsPort = StringFromAny(d["https_port"]);
  if (d.HasMember("server_protocol"))
    out.protocol = StringFromAny(d["server_protocol"]);
  if (d.HasMember("time_now"))
    out.timeNow = StringFromAny(d["time_now"]);

  return out;
}

std::string HumanStatus(const UserInfo &info) {
  if (!info.status.empty())
    return info.status;
  return info.auth ? "Active" : "Inactive";
}

std::string FormatExpiryDate(int64_t expEpoch) {
  if (expEpoch <= 0)
    return std::string();
  return FormatLocalTime(static_cast<time_t>(expEpoch), "%d.%m.%Y");
}

bool IsExpiringSoon(int64_t expEpoch, int64_t nowEpoch, int days) {
  if (expEpoch <= 0)
    return false;
  const int64_t secs = expEpoch - nowEpoch;
  return secs > 0 && secs <= static_cast<int64_t>(days) * 86400;
}

} // namespace xtream
