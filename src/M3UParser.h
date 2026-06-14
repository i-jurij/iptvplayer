#pragma once
#include "ErrorCode.h"
#include "Playlist.h"
#include <string>
#include <vector>
#include <map>

struct ParseResult {
    std::vector<Channel> channels;
    std::string error;
    ErrorCode code;
    bool success;

    ParseResult() : code(ErrorCode::Unknown), success(false) {}
};

class M3UParser {
public:
    M3UParser();
    ~M3UParser();

	ErrorCode parse(const std::string& content, Playlist* playlist);
    ParseResult parse(const std::string& content);
    std::string exportToM3U(const std::vector<Channel>& channels,
                            const std::string& playlistTitle);

private:
    Channel parseExtInfLine(const std::string& extinf, const std::string& url);
    std::string extractAttribute(const std::string& line, const std::string& attr);
    std::map<std::string, std::string> extractAllAttributes(const std::string& line);
    std::string trim(const std::string& str);
};

