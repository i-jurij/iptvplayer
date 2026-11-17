#pragma once
#include "PlaylistParser.h"

class XSPFParser : public IPlaylistParser {
public:
  ParseResult parse(const std::string &content) override;
};
