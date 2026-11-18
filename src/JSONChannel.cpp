#include "JSONChannel.h"

#include <string>

namespace {

std::string GetStr(const rapidjson::Value &obj, const char *key) {
  if (!obj.IsObject() || !obj.HasMember(key))
    return {};
  const auto &v = obj[key];
  if (v.IsString())
    return v.GetString();
  return {};
}

std::string GetStrAny(const rapidjson::Value &obj,
                      std::initializer_list<const char *> keys) {
  for (const char *k : keys) {
    const std::string v = GetStr(obj, k);
    if (!v.empty())
      return v;
  }
  return {};
}

int64_t GetInt64Flexible(const rapidjson::Value &obj, const char *key) {
  if (!obj.IsObject() || !obj.HasMember(key))
    return 0;
  const auto &v = obj[key];
  if (v.IsInt64())
    return v.GetInt64();
  if (v.IsInt())
    return static_cast<int64_t>(v.GetInt());
  if (v.IsUint())
    return static_cast<int64_t>(v.GetUint());
  if (v.IsUint64())
    return static_cast<int64_t>(v.GetUint64());
  if (v.IsString()) {
    try {
      return std::stoll(v.GetString());
    } catch (...) {
    }
  }
  return 0;
}

int GetIntFlexible(const rapidjson::Value &obj, const char *key) {
  if (!obj.IsObject() || !obj.HasMember(key))
    return -1;
  const auto &v = obj[key];
  if (v.IsInt())
    return v.GetInt();
  if (v.IsUint())
    return static_cast<int>(v.GetUint());
  return -1;
}

// Keys consumed by ParseChannelFromJson. Any other string member of the
// source object is preserved in attributes().
bool IsKnownKey(const std::string &k) {
  static const char *const kKeys[] = {
      // canonical
      "name",
      "url",
      "groupTitle",
      "country",
      "language",
      "category",
      "tvgLogo",
      "tvg-id",
      "tvg-name",
      "playlistId",
      "series",
      "season",
      "mediaType",
      "streamId",
      // fallback aliases
      "link",
      "stream",
      "src",
      "file",
      "location",
      "title",
      "label",
      "group-title",
      "group",
      "tvg-logo",
      "logo",
      "icon",
      "tvgId",
      "tvgName",
  };
  for (const char *s : kKeys) {
    if (k == s)
      return true;
  }
  return false;
}

} // namespace

Channel ParseChannelFromJson(const rapidjson::Value &obj) {
  Channel ch;

  // URL: canonical "url" → fallback link/stream/src/file/location.
  ch.setUrl(GetStr(obj, "url"));
  if (ch.getUrl().empty()) {
    const std::string v =
        GetStrAny(obj, {"link", "stream", "src", "file", "location"});
    if (!v.empty())
      ch.setUrl(v);
  }

  // Name: canonical "name" → fallback title/label.
  ch.setName(GetStr(obj, "name"));
  if (ch.getName().empty()) {
    const std::string v = GetStrAny(obj, {"title", "label"});
    if (!v.empty())
      ch.setName(v);
  }

  // GroupTitle: canonical "groupTitle" → fallback group-title/group.
  {
    const std::string v = GetStr(obj, "groupTitle");
    if (!v.empty())
      ch.setGroupTitle(v);
    else {
      const std::string fb = GetStrAny(obj, {"group-title", "group"});
      if (!fb.empty())
        ch.setGroupTitle(fb);
    }
  }

  ch.setCountry(GetStr(obj, "country"));
  ch.setLanguage(GetStr(obj, "language"));
  ch.setCategory(GetStr(obj, "category"));

  // Logo: canonical "tvgLogo" → fallback tvg-logo/logo/icon.
  {
    const std::string v = GetStr(obj, "tvgLogo");
    if (!v.empty())
      ch.setLogo(v);
    else {
      const std::string fb = GetStrAny(obj, {"tvg-logo", "logo", "icon"});
      if (!fb.empty())
        ch.setLogo(fb);
    }
  }

  // TvgId: canonical "tvg-id" → fallback "tvgId".
  {
    const std::string v = GetStr(obj, "tvg-id");
    if (!v.empty())
      ch.setTvgId(v);
    else {
      const std::string fb = GetStr(obj, "tvgId");
      if (!fb.empty())
        ch.setTvgId(fb);
    }
  }

  // TvgName: canonical "tvg-name" → fallback "tvgName".
  {
    const std::string v = GetStr(obj, "tvg-name");
    if (!v.empty())
      ch.setTvgName(v);
    else {
      const std::string fb = GetStr(obj, "tvgName");
      if (!fb.empty())
        ch.setTvgName(fb);
    }
  }

  ch.setPlaylistId(GetStr(obj, "playlistId"));
  ch.setSeries(GetStr(obj, "series"));
  ch.setSeason(GetStr(obj, "season"));

  const int mt = GetIntFlexible(obj, "mediaType");
  if (mt >= 0 && mt <= 3)
    ch.setMediaType(static_cast<MediaType>(mt));

  const int64_t sid = GetInt64Flexible(obj, "streamId");
  if (sid != 0)
    ch.setStreamId(sid);

  // Preserve remaining string members as attributes.
  if (obj.IsObject()) {
    for (auto it = obj.MemberBegin(); it != obj.MemberEnd(); ++it) {
      if (!it->name.IsString() || !it->value.IsString())
        continue;
      const std::string key = it->name.GetString();
      if (IsKnownKey(key))
        continue;
      ch.attributes()[key] = it->value.GetString();
    }
  }

  return ch;
}
