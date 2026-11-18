#include "JSONPlaylistParser.h"

#include "JSONChannel.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <string>

namespace {

constexpr size_t kMaxBodyBytes = 100 * 1024 * 1024; // sanity cap

// Remove a leading UTF-8 BOM (EF BB BF) if present. RapidJSON::Document::Parse
// does not strip BOM by itself.
std::string StripUtf8Bom(const std::string &content) {
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB &&
      static_cast<unsigned char>(content[2]) == 0xBF) {
    return content.substr(3);
  }
  return content;
}

std::string MakeFallbackName(size_t index) {
  return "Channel " + std::to_string(index + 1);
}

} // namespace

ParseResult JSONPlaylistParser::parse(const std::string &raw) {
  ParseResult result;

  if (raw.size() > kMaxBodyBytes) {
    result.error = "JSON body too large";
    result.code = ErrorCode::InvalidHeader;
    return result;
  }

  const std::string content = StripUtf8Bom(raw);

  rapidjson::Document d;
  d.Parse(content.data(), content.size());
  if (d.HasParseError() || (!d.IsArray() && !d.IsObject())) {
    result.error = d.HasParseError()
                       ? std::string("JSON parse error: ") +
                             rapidjson::GetParseError_En(d.GetParseError())
                       : std::string("JSON root is neither object nor array");
    result.code = ErrorCode::InvalidHeader;
    return result;
  }

  // Locate the channel array: root-array → "channels" → "items" → "streams".
  const rapidjson::Value *arr = nullptr;
  if (d.IsArray()) {
    arr = &d;
  } else {
    for (const char *k : {"channels", "items", "streams"}) {
      if (d.HasMember(k) && d[k].IsArray()) {
        arr = &d[k];
        break;
      }
    }
  }

  if (arr) {
    for (auto &el : arr->GetArray()) {
      if (el.IsObject()) {
        Channel ch = ParseChannelFromJson(el);
        if (ch.getUrl().empty())
          continue;
        if (ch.getName().empty())
          ch.setName(MakeFallbackName(result.channels.size()));
        result.channels.push_back(std::move(ch));
      } else if (el.IsString()) {
        Channel ch;
        ch.setUrl(el.GetString());
        ch.setName(MakeFallbackName(result.channels.size()));
        result.channels.push_back(std::move(ch));
      }
    }
  } else if (d.IsObject()) {
    // Root-object as a single channel (GPAC-style: single root object).
    Channel ch = ParseChannelFromJson(d);
    if (!ch.getUrl().empty()) {
      if (ch.getName().empty())
        ch.setName(MakeFallbackName(0));
      result.channels.push_back(std::move(ch));
    }
  }

  if (result.channels.empty()) {
    result.error = "JSON: no channel entries found";
    result.code = ErrorCode::InvalidHeader;
    return result;
  }

  result.success = true;
  result.code = ErrorCode::OK;
  return result;
}
