#include "M3UParser.h"
#include <sstream>

// ------------------------------------------------------------
//  M3UParser
// ------------------------------------------------------------
M3UParser::M3UParser() = default;
M3UParser::~M3UParser() = default;

// ------------------------------------------------------------
//  trim()
// ------------------------------------------------------------
std::string M3UParser::trim(const std::string &str) {
  const std::string whitespace = " \t\r\n";
  size_t first = str.find_first_not_of(whitespace);
  if (first == std::string::npos)
    return "";
  size_t last = str.find_last_not_of(whitespace);
  return str.substr(first, last - first + 1);
}

// ------------------------------------------------------------
//  extractAttribute() — безопасный
// ------------------------------------------------------------
std::string M3UParser::extractAttribute(const std::string &line,
                                        const std::string &attr) {
  const std::string search = attr + "=\"";
  size_t pos = line.find(search);
  if (pos == std::string::npos)
    return "";

  pos += search.length();
  size_t endPos = line.find('"', pos);
  if (endPos == std::string::npos || endPos <= pos)
    return "";

  return line.substr(pos, endPos - pos);
}

// ------------------------------------------------------------
//  extractAllAttributes() — полностью безопасный
// ------------------------------------------------------------
std::map<std::string, std::string>
M3UParser::extractAllAttributes(const std::string &line) {
  std::map<std::string, std::string> attrs;

  size_t pos = 0;
  while (true) {
    // ищем =" (начало значения)
    pos = line.find("=\"", pos);
    if (pos == std::string::npos)
      break;

    // ищем начало ключа (последний пробел перед pos)
    size_t start = line.rfind(' ', pos);
    if (start == std::string::npos) {
      pos += 2;
      continue;
    }

    // вычисляем длину ключа
    if (start + 1 >= pos) {
      pos += 2;
      continue;
    }

    size_t keyLen = pos - start - 1;
    if (keyLen == 0) {
      pos += 2;
      continue;
    }

    std::string key;
    try {
      key = line.substr(start + 1, keyLen);
    } catch (...) {
      pos += 2;
      continue;
    }

    // ищем конец значения
    size_t end = line.find('"', pos + 2);
    if (end == std::string::npos || end <= pos + 2) {
      pos += 2;
      continue;
    }

    std::string value;
    try {
      value = line.substr(pos + 2, end - (pos + 2));
    } catch (...) {
      pos = end + 1;
      continue;
    }

    if (!key.empty())
      attrs[key] = value;

    pos = end + 1;
  }

  return attrs;
}

// ------------------------------------------------------------
//  parseExtInfLine() — безопасный
// ------------------------------------------------------------
Channel M3UParser::parseExtInfLine(const std::string &extinf,
                                   const std::string &url) {
  Channel channel;
  channel.setUrl(trim(url));

  // безопасные атрибуты
  std::string tvgId = extractAttribute(extinf, "tvg-id");
  std::string tvgName = extractAttribute(extinf, "tvg-name");
  std::string tvgLogo = extractAttribute(extinf, "tvg-logo");
  std::string groupTitle = extractAttribute(extinf, "group-title");

  if (!tvgId.empty())
    channel.setTvgId(tvgId);
  if (!tvgName.empty())
    channel.setTvgName(tvgName);
  if (!tvgLogo.empty())
    channel.setLogo(tvgLogo);
  if (!groupTitle.empty())
    channel.setGroupTitle(groupTitle);

  // безопасный парсинг всех атрибутов
  try {
    channel.attributes() = extractAllAttributes(extinf);
  } catch (...) {
    // игнорируем битые атрибуты
  }

  // имя канала
  size_t commaPos = extinf.find_last_of(',');
  if (commaPos != std::string::npos && commaPos + 1 < extinf.length()) {
    channel.setName(trim(extinf.substr(commaPos + 1)));
  } else {
    channel.setName("Unknown Channel");
  }

  return channel;
}

// ------------------------------------------------------------
//  parse() — безопасный, не бросает исключений
// ------------------------------------------------------------
ParseResult M3UParser::parse(const std::string &content) {
  ParseResult result;

  try {
    std::istringstream stream(content);
    std::string line, currentExtInf;

    if (!std::getline(stream, line) ||
        line.find("#EXTM3U") == std::string::npos) {
      result.error = "Invalid M3U file: missing #EXTM3U header";
      result.code = ErrorCode::InvalidHeader;
      return result;
    }

    while (std::getline(stream, line)) {
      line = trim(line);

      if (line.empty() || line[0] == '#') {
        if (line.find("#EXTINF:") == 0)
          currentExtInf = line;
        continue;
      }

      if (!currentExtInf.empty()) {
        try {
          result.channels.push_back(parseExtInfLine(currentExtInf, line));
        } catch (...) {
          // пропускаем битый канал
        }
        currentExtInf.clear();
      } else {
        Channel ch;
        ch.setUrl(line);
        ch.setName("Channel " + std::to_string(result.channels.size() + 1));
        result.channels.push_back(std::move(ch));
      }
    }

    result.success = true;
    result.code = ErrorCode::OK;
    return result;
  } catch (const std::exception &e) {
    result.success = false;
    result.error = std::string("Parser exception: ") + e.what();
    result.code = ErrorCode::Unknown;
    return result;
  } catch (...) {
    result.success = false;
    result.error = "Parser unknown exception";
    result.code = ErrorCode::Unknown;
    return result;
  }
}

// ------------------------------------------------------------
//  exportToM3U()
// ------------------------------------------------------------
std::string M3UParser::exportToM3U(const std::vector<Channel> &channels,
                                   const std::string &playlistTitle) {
  std::ostringstream oss;
  oss << "#EXTM3U\n";
  if (!playlistTitle.empty())
    oss << "#PLAYLIST:" << playlistTitle << "\n\n";

  for (const auto &ch : channels) {
    oss << "#EXTINF:-1";
    for (const auto &[key, value] : ch.attributes()) {
      if (!key.empty() && !value.empty()) {
        oss << " " << key << "=\"" << value << "\"";
      }
    }
    oss << "," << ch.getName() << "\n";
    oss << ch.getUrl() << "\n";
  }
  return oss.str();
}
