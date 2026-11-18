#pragma once
#include "PlaylistParser.h"

class JSONPlaylistParser : public IPlaylistParser {
public:
  ParseResult parse(const std::string &content) override;
};
