#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

// Small header-only helpers for lightweight XML tag scanning (XSPF, ASX).
// Not a full XML parser: no CDATA, no namespaces, no attributes with '>' in
// value. Sufficient for playlist formats (real-world XSPF/ASX are simple).
namespace playlist_parser_utils {

inline std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

inline std::string Trim(const std::string &s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
    return {};
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

inline std::string UnescapeXml(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '&') {
      if (s.compare(i, 5, "&amp;") == 0) {
        out += '&';
        i += 5;
        continue;
      }
      if (s.compare(i, 4, "&lt;") == 0) {
        out += '<';
        i += 4;
        continue;
      }
      if (s.compare(i, 4, "&gt;") == 0) {
        out += '>';
        i += 4;
        continue;
      }
      if (s.compare(i, 6, "&quot;") == 0) {
        out += '"';
        i += 6;
        continue;
      }
      if (s.compare(i, 6, "&apos;") == 0) {
        out += '\'';
        i += 6;
        continue;
      }
    }
    out += s[i++];
  }
  return out;
}

// Returns {contentBegin, contentEnd} for every "<tag ...>...</tag>"
// (tag name case-insensitive; content kept as-is).
inline std::vector<std::pair<size_t, size_t>>
FindAllTagRanges(const std::string &haystack, const std::string &tag) {
  std::vector<std::pair<size_t, size_t>> out;
  const std::string hayLower = ToLower(haystack);
  const std::string openTag = "<" + ToLower(tag);
  const std::string closeTag = "</" + ToLower(tag) + ">";
  size_t pos = 0;
  while (true) {
    const size_t start = hayLower.find(openTag, pos);
    if (start == std::string::npos)
      break;
    const size_t after = start + openTag.size();
    if (after < hayLower.size()) {
      const char c = hayLower[after];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
        pos = after;
        continue;
      }
    }
    const size_t gt = hayLower.find('>', after);
    if (gt == std::string::npos)
      break;
    const size_t contentBegin = gt + 1;
    const size_t close = hayLower.find(closeTag, contentBegin);
    if (close == std::string::npos)
      break;
    out.push_back({contentBegin, close});
    pos = close + closeTag.size();
  }
  return out;
}

// Text of the first <tag>...</tag> in haystack (from position `from`).
// Empty string if absent, self-closing, or empty content.
inline std::string FindTagText(const std::string &haystack,
                               const std::string &tag, size_t from = 0) {
  const std::string hayLower = ToLower(haystack);
  const std::string openTag = "<" + ToLower(tag);
  const std::string closeTag = "</" + ToLower(tag) + ">";
  const size_t start = hayLower.find(openTag, from);
  if (start == std::string::npos)
    return {};
  const size_t after = start + openTag.size();
  if (after < hayLower.size()) {
    const char c = hayLower[after];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
      return {};
  }
  const size_t gt = hayLower.find('>', after);
  if (gt == std::string::npos)
    return {};
  if (gt > 0 && haystack[gt - 1] == '/')
    return {};
  const size_t contentBegin = gt + 1;
  const size_t close = hayLower.find(closeTag, contentBegin);
  if (close == std::string::npos)
    return {};
  return Trim(haystack.substr(contentBegin, close - contentBegin));
}

// Attribute value in the first <tag ... attr="value" ...> (from `from`).
inline std::string FindTagAttr(const std::string &haystack,
                               const std::string &tag, const std::string &attr,
                               size_t from = 0) {
  const std::string hayLower = ToLower(haystack);
  const std::string openTag = "<" + ToLower(tag);
  const size_t start = hayLower.find(openTag, from);
  if (start == std::string::npos)
    return {};
  const size_t after = start + openTag.size();
  if (after < hayLower.size()) {
    const char c = hayLower[after];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
      return {};
  }
  const size_t gt = hayLower.find('>', after);
  if (gt == std::string::npos)
    return {};
  const std::string tagBody = haystack.substr(start, gt - start + 1);
  const std::string tagBodyLower = ToLower(tagBody);
  const std::string attrKey = ToLower(attr) + "=";
  const size_t apos = tagBodyLower.find(attrKey);
  if (apos == std::string::npos)
    return {};
  size_t vpos = apos + attrKey.size();
  if (vpos >= tagBody.size())
    return {};
  const char quote = tagBody[vpos];
  if (quote != '"' && quote != '\'')
    return {};
  ++vpos;
  const size_t vend = tagBody.find(quote, vpos);
  if (vend == std::string::npos)
    return {};
  return tagBody.substr(vpos, vend - vpos);
}

} // namespace playlist_parser_utils
