#pragma once
#include "PlaylistParser.h"
#include "Playlist.h"
#include <string>
#include <vector>
#include <map>

class M3UParser : public IPlaylistParser {
public:
    M3UParser();
    ~M3UParser() override;

    ParseResult parse(const std::string& content) override;

private:
    Channel parseExtInfLine(const std::string& extinf, const std::string& url);
    std::string extractAttribute(const std::string& line, const std::string& attr);
    std::map<std::string, std::string> extractAllAttributes(const std::string& line);
    std::string trim(const std::string& str);
};

