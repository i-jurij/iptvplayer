#include "XSPFParser.h"

#include "PlaylistParserUtils.h"

ParseResult XSPFParser::parse(const std::string &content) {
  ParseResult result;

  if (content.find("<playlist") == std::string::npos &&
      content.find("<trackList") == std::string::npos) {
    result.error = "Invalid XSPF file: missing <playlist>/<trackList>";
    result.code = ErrorCode::InvalidHeader;
    return result;
  }

  const auto tracks = playlist_parser_utils::FindAllTagRanges(content, "track");
  if (tracks.empty()) {
    result.error = "XSPF: no <track> entries";
    result.code = ErrorCode::InvalidHeader;
    return result;
  }

  for (const auto &[b, e] : tracks) {
    const std::string body = content.substr(b, e - b);
    const std::string loc =
        playlist_parser_utils::FindTagText(body, "location");
    if (loc.empty())
      continue;
    Channel ch;
    ch.setUrl(playlist_parser_utils::UnescapeXml(loc));
    const std::string title = playlist_parser_utils::FindTagText(body, "title");
    if (!title.empty())
      ch.setName(playlist_parser_utils::UnescapeXml(title));
    else
      ch.setName("Channel " + std::to_string(result.channels.size() + 1));
    result.channels.push_back(std::move(ch));
  }

  if (result.channels.empty()) {
    result.error = "XSPF: no valid <location> entries";
    result.code = ErrorCode::Unknown;
    return result;
  }

  result.success = true;
  result.code = ErrorCode::OK;
  return result;
}
