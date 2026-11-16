#include "PlaylistJson.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <string>
#include <vector>

namespace {

const char *KindToString(bool isXtream) { return isXtream ? "Xtream" : "M3U"; }

bool KindFromString(const char *s) { return s && std::string(s) == "Xtream"; }

} // namespace

std::string ToJson(const Playlist &pl) {
  rapidjson::Document doc;
  doc.SetObject();
  auto &alloc = doc.GetAllocator();

  doc.AddMember("title", rapidjson::Value(pl.getTitle().c_str(), alloc), alloc);
  doc.AddMember("source", rapidjson::Value(pl.getSource().c_str(), alloc),
                alloc);
  doc.AddMember("userAgent", rapidjson::Value(pl.getUserAgent().c_str(), alloc),
                alloc);
  doc.AddMember("isUrl", pl.isUrl(), alloc);
  doc.AddMember("autoUpdate", pl.getAutoUpdate(), alloc);
  doc.AddMember("lastUpdate", static_cast<int64_t>(pl.getLastUpdate()), alloc);
  doc.AddMember("uniqueId", rapidjson::Value(pl.getUniqueId().c_str(), alloc),
                alloc);
  doc.AddMember("xtream", pl.isXtream(), alloc);
  doc.AddMember("kind", rapidjson::Value(KindToString(pl.isXtream()), alloc),
                alloc);
  doc.AddMember("hasVod", pl.hasVod(), alloc);
  doc.AddMember("hasSeries", pl.hasSeries(), alloc);
  doc.AddMember("hasRadio", pl.hasRadio(), alloc);

  if (pl.isXtream()) {
    const auto &meta = pl.getXtreamMeta();
    rapidjson::Value metaJson(rapidjson::kObjectType);
    metaJson.AddMember("userInfoJson",
                       rapidjson::Value(meta.userInfoJson.c_str(), alloc),
                       alloc);
    metaJson.AddMember("serverInfoJson",
                       rapidjson::Value(meta.serverInfoJson.c_str(), alloc),
                       alloc);
    metaJson.AddMember("baseUrl", rapidjson::Value(meta.baseUrl.c_str(), alloc),
                       alloc);
    metaJson.AddMember("username",
                       rapidjson::Value(meta.username.c_str(), alloc), alloc);
    metaJson.AddMember("password",
                       rapidjson::Value(meta.password.c_str(), alloc), alloc);
    doc.AddMember("xtreamMeta", metaJson, alloc);
  }

  rapidjson::Value channelsArr(rapidjson::kArrayType);
  for (const auto &ch : pl.getChannels()) {
    rapidjson::Value chObj(rapidjson::kObjectType);
    chObj.AddMember("name", rapidjson::Value(ch.getName().c_str(), alloc),
                    alloc);
    chObj.AddMember("url", rapidjson::Value(ch.getUrl().c_str(), alloc), alloc);
    chObj.AddMember("groupTitle",
                    rapidjson::Value(ch.getGroupTitle().c_str(), alloc), alloc);
    chObj.AddMember("country", rapidjson::Value(ch.getCountry().c_str(), alloc),
                    alloc);
    chObj.AddMember("language",
                    rapidjson::Value(ch.getLanguage().c_str(), alloc), alloc);
    chObj.AddMember("category",
                    rapidjson::Value(ch.getCategory().c_str(), alloc), alloc);
    chObj.AddMember("tvgLogo", rapidjson::Value(ch.getLogo().c_str(), alloc),
                    alloc);
    chObj.AddMember("tvg-id", rapidjson::Value(ch.getTvgId().c_str(), alloc),
                    alloc);
    chObj.AddMember("tvg-name",
                    rapidjson::Value(ch.getTvgName().c_str(), alloc), alloc);
    chObj.AddMember("playlistId",
                    rapidjson::Value(ch.getPlaylistId().c_str(), alloc), alloc);
    chObj.AddMember("series", rapidjson::Value(ch.getSeries().c_str(), alloc),
                    alloc);
    chObj.AddMember("season", rapidjson::Value(ch.getSeason().c_str(), alloc),
                    alloc);
    chObj.AddMember("mediaType", static_cast<int>(ch.getMediaType()), alloc);
    chObj.AddMember("streamId", static_cast<int64_t>(ch.getStreamId()), alloc);

    for (const auto &[key, value] : ch.attributes()) {
      chObj.AddMember(rapidjson::Value(key.c_str(), alloc),
                      rapidjson::Value(value.c_str(), alloc), alloc);
    }
    channelsArr.PushBack(chObj, alloc);
  }
  doc.AddMember("channels", channelsArr, alloc);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return buffer.GetString();
}

bool FromJson(const std::string &json, Playlist &pl) {
  rapidjson::Document doc;
  if (doc.Parse(json.c_str()).HasParseError())
    return false;

  if (!doc.HasMember("channels") || !doc["channels"].IsArray())
    return false;

  // Capture current lastUpdate; will be restored if JSON has no explicit
  // value. setChannels() below resets m_lastUpdate to now() as a side effect.
  const std::time_t savedLastUpdate = pl.getLastUpdate();

  // Basic fields
  if (doc.HasMember("title") && doc["title"].IsString())
    pl.setTitle(doc["title"].GetString());
  if (doc.HasMember("source") && doc["source"].IsString())
    pl.setSource(doc["source"].GetString());
  if (doc.HasMember("userAgent") && doc["userAgent"].IsString())
    pl.setUserAgent(doc["userAgent"].GetString());
  if (doc.HasMember("isUrl") && doc["isUrl"].IsBool())
    pl.setIsUrl(doc["isUrl"].GetBool());
  if (doc.HasMember("autoUpdate") && doc["autoUpdate"].IsBool())
    pl.setAutoUpdate(doc["autoUpdate"].GetBool());

  // uniqueId
  if (doc.HasMember("uniqueId") && doc["uniqueId"].IsString())
    pl.setUniqueId(doc["uniqueId"].GetString());
  else
    pl.setUniqueId(GenerateUUID());

  // Kind migration (10.1 semantics): "kind" (string) priority;
  // fallback "xtream" (legacy bool); default M3U.
  if (doc.HasMember("kind") && doc["kind"].IsString()) {
    pl.setXtream(KindFromString(doc["kind"].GetString()));
  } else if (doc.HasMember("xtream") && doc["xtream"].IsBool()) {
    pl.setXtream(doc["xtream"].GetBool());
  } else {
    pl.setXtream(false);
  }

  if (doc.HasMember("hasVod") && doc["hasVod"].IsBool())
    pl.setHasVod(doc["hasVod"].GetBool());
  if (doc.HasMember("hasSeries") && doc["hasSeries"].IsBool())
    pl.setHasSeries(doc["hasSeries"].GetBool());
  if (doc.HasMember("hasRadio") && doc["hasRadio"].IsBool())
    pl.setHasRadio(doc["hasRadio"].GetBool());

  // xtreamMeta
  if (doc.HasMember("xtreamMeta") && doc["xtreamMeta"].IsObject()) {
    XtreamMeta meta = pl.getXtreamMeta();
    const auto &m = doc["xtreamMeta"];
    if (m.HasMember("userInfoJson") && m["userInfoJson"].IsString())
      meta.userInfoJson = m["userInfoJson"].GetString();
    if (m.HasMember("serverInfoJson") && m["serverInfoJson"].IsString())
      meta.serverInfoJson = m["serverInfoJson"].GetString();
    if (m.HasMember("baseUrl") && m["baseUrl"].IsString())
      meta.baseUrl = m["baseUrl"].GetString();
    if (m.HasMember("username") && m["username"].IsString())
      meta.username = m["username"].GetString();
    if (m.HasMember("password") && m["password"].IsString())
      meta.password = m["password"].GetString();
    pl.setXtreamMeta(std::move(meta));
  }

  // Channels
  std::vector<Channel> channels;
  channels.reserve(doc["channels"].GetArray().Size());
  for (auto &chVal : doc["channels"].GetArray()) {
    Channel ch;
    if (chVal.HasMember("name") && chVal["name"].IsString())
      ch.setName(chVal["name"].GetString());

    if (chVal.HasMember("url") && chVal["url"].IsString())
      ch.setUrl(chVal["url"].GetString());

    if (chVal.HasMember("groupTitle") && chVal["groupTitle"].IsString())
      ch.setGroupTitle(chVal["groupTitle"].GetString());

    if (chVal.HasMember("country") && chVal["country"].IsString())
      ch.setCountry(chVal["country"].GetString());

    if (chVal.HasMember("language") && chVal["language"].IsString())
      ch.setLanguage(chVal["language"].GetString());

    if (chVal.HasMember("category") && chVal["category"].IsString())
      ch.setCategory(chVal["category"].GetString());

    if (chVal.HasMember("tvgLogo") && chVal["tvgLogo"].IsString())
      ch.setLogo(chVal["tvgLogo"].GetString());

    if (chVal.HasMember("tvg-id") && chVal["tvg-id"].IsString())
      ch.setTvgId(chVal["tvg-id"].GetString());

    if (chVal.HasMember("tvg-name") && chVal["tvg-name"].IsString())
      ch.setTvgName(chVal["tvg-name"].GetString());

    if (chVal.HasMember("playlistId") && chVal["playlistId"].IsString())
      ch.setPlaylistId(chVal["playlistId"].GetString());

    if (chVal.HasMember("series") && chVal["series"].IsString())
      ch.setSeries(chVal["series"].GetString());

    if (chVal.HasMember("season") && chVal["season"].IsString())
      ch.setSeason(chVal["season"].GetString());

    if (chVal.HasMember("mediaType") && chVal["mediaType"].IsInt()) {
      const int mt = chVal["mediaType"].GetInt();
      if (mt >= 0 && mt <= 3)
        ch.setMediaType(static_cast<MediaType>(mt));
    }
    if (chVal.HasMember("streamId")) {
      const auto &sid = chVal["streamId"];
      if (sid.IsInt64())
        ch.setStreamId(sid.GetInt64());
      else if (sid.IsInt())
        ch.setStreamId(sid.GetInt());
      else if (sid.IsUint())
        ch.setStreamId(static_cast<int64_t>(sid.GetUint()));
      else if (sid.IsUint64())
        ch.setStreamId(static_cast<int64_t>(sid.GetUint64()));
      else if (sid.IsString()) {
        try {
          ch.setStreamId(std::stoll(sid.GetString()));
        } catch (...) {
        }
      }
    }

    // Extra attributes (everything except known fields).
    for (auto it = chVal.MemberBegin(); it != chVal.MemberEnd(); ++it) {
      std::string key = it->name.GetString();
      if (key != "name" && key != "url" && key != "groupTitle" &&
          key != "country" && key != "language" && key != "category" &&
          key != "tvgLogo" && key != "tvg-id" && key != "tvg-name" &&
          key != "playlistId" && key != "series" && key != "season" &&
          key != "mediaType" && key != "streamId") {
        if (it->value.IsString()) {
          ch.attributes()[key] = it->value.GetString();
        }
      }
    }

    channels.push_back(std::move(ch));
  }
  // setChannels sets playlistId = m_uniqueId on each channel and resets
  // m_lastUpdate to now() — lastUpdate is corrected immediately below.
  pl.setChannels(std::move(channels));

  // Restore lastUpdate: JSON value wins; else preserve pre-call value.
  if (doc.HasMember("lastUpdate") && doc["lastUpdate"].IsInt64())
    pl.setLastUpdate(static_cast<std::time_t>(doc["lastUpdate"].GetInt64()));
  else
    pl.setLastUpdate(savedLastUpdate);

  return true;
}
