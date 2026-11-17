#include "PLSParser.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace {

std::string Trim(const std::string &s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
    return {};
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool AllDigits(const std::string &s, size_t pos) {
  if (pos >= s.size())
    return false;
  for (size_t i = pos; i < s.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(s[i])))
      return false;
  return true;
}

int ParseIndex(const std::string &s, size_t pos) {
  try {
    return std::stoi(s.substr(pos));
  } catch (...) {
    return -1;
  }
}

} // namespace

ParseResult PLSParser::parse(const std::string &content) {
  ParseResult result;

  std::istringstream stream(content);
  std::string line;
  std::map<int, std::string> urls;   // FileN  -> URL
  std::map<int, std::string> titles; // TitleN -> name

  while (std::getline(stream, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#')
      continue;
    if (line.front() == '[')
      continue; // section header, no per-entry data inside [playlist]

    const size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = Trim(line.substr(0, eq));
    const std::string val = Trim(line.substr(eq + 1));
    const std::string keyLower = [&] {
      std::string s = key;
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      return s;
    }();

    if (keyLower.rfind("file", 0) == 0 && AllDigits(keyLower, 4)) {
      const int n = ParseIndex(keyLower, 4);
      if (n > 0)
        urls[n] = val;
    } else if (keyLower.rfind("title", 0) == 0 && AllDigits(keyLower, 5)) {
      const int n = ParseIndex(keyLower, 5);
      if (n > 0)
        titles[n] = val;
    }
  }

  if (urls.empty()) {
    result.error = "Invalid PLS file: no FileN entries";
    result.code = ErrorCode::InvalidHeader;
    return result;
  }

  for (const auto &[n, url] : urls) {
    if (url.empty())
      continue;
    Channel ch;
    ch.setUrl(url);
    auto it = titles.find(n);
    if (it != titles.end() && !it->second.empty())
      ch.setName(it->second);
    else
      ch.setName("Channel " + std::to_string(result.channels.size() + 1));
    result.channels.push_back(std::move(ch));
  }

  result.success = true;
  result.code = ErrorCode::OK;
  return result;
}
