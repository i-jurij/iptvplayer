#include "PlaylistParser.h"

#include "ASXParser.h"
#include "JSONPlaylistParser.h"
#include "M3UParser.h"
#include "PLSParser.h"
#include "XSPFParser.h"

std::unique_ptr<IPlaylistParser> CreateParser(PlaylistFormat fmt) {
  switch (fmt) {
  case PlaylistFormat::M3U:
    return std::make_unique<M3UParser>();
  case PlaylistFormat::PLS:
    return std::make_unique<PLSParser>();
  case PlaylistFormat::XSPF:
    return std::make_unique<XSPFParser>();
  case PlaylistFormat::ASX:
    return std::make_unique<ASXParser>();
  case PlaylistFormat::JSON:
    return std::make_unique<JSONPlaylistParser>();
  }
  return nullptr;
}
