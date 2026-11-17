#include "ASXParser.h"

#include "PlaylistParserUtils.h"

ParseResult ASXParser::parse(const std::string &content) {
  ParseResult result;

  if (content.find("<asx") == std::string::npos) {
    result.error = "Invalid ASX file: missing <asx>";
    result.code = ErrorCode::InvalidHeader;
    return result;
  }

  auto entries = playlist_parser_utils::FindAllTagRanges(content, "entry");
  std::vector<std::pair<size_t, size_t>> ranges;
  if (!entries.empty()) {
    ranges = std::move(entries);
  } else {
    // Fallback: some ASX files use <ref> directly under <asx>.
    ranges.push_back({0, content.size()});
  }

  for (const auto &[b, e] : ranges) {
    const std::string body = content.substr(b, e - b);
    const std::string href =
        playlist_parser_utils::FindTagAttr(body, "ref", "href");
    if (href.empty())
      continue;
    Channel ch;
    ch.setUrl(playlist_parser_utils::UnescapeXml(href));
    const std::string title = playlist_parser_utils::FindTagText(body, "title");
    if (!title.empty())
      ch.setName(playlist_parser_utils::UnescapeXml(title));
    else
      ch.setName("Channel " + std::to_string(result.channels.size() + 1));
    result.channels.push_back(std::move(ch));
  }

  if (result.channels.empty()) {
    result.error = "ASX: no valid <ref href> entries";
    result.code = ErrorCode::Unknown;
    return result;
  }

  result.success = true;
  result.code = ErrorCode::OK;
  return result;
}
