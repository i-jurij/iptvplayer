#pragma once
#include "PlaylistParser.h"

class ASXParser : public IPlaylistParser {
public:
  ParseResult parse(const std::string &content) override;
};
