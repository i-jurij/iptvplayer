#pragma once
#include "PlaylistParser.h"

class PLSParser : public IPlaylistParser {
public:
  ParseResult parse(const std::string &content) override;
};
