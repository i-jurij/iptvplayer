#pragma once
#include "ErrorCode.h"
#include "Playlist.h"

#include <memory>
#include <string>
#include <vector>

// Common parse result for all playlist formats. Moved here from M3UParser.h
// so that the interface and implementations share one definition without
// circular includes.
struct ParseResult {
  std::vector<Channel> channels;
  std::string error;
  ErrorCode code;
  bool success;

  ParseResult() : code(ErrorCode::Unknown), success(false) {}
};

enum class PlaylistFormat {
  M3U,
  PLS,
  XSPF,
  ASX,
  JSON,
};

class IPlaylistParser {
public:
  virtual ~IPlaylistParser() = default;
  virtual ParseResult parse(const std::string &content) = 0;
};

// Factory. Caller owns the returned parser. Returns nullptr on unknown format.
std::unique_ptr<IPlaylistParser> CreateParser(PlaylistFormat fmt);
