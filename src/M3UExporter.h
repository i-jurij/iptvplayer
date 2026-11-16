#pragma once
#include "Channel.h"
#include <string>
#include <vector>

// External M3U export. Free function, no state.
std::string ExportToM3U(const std::vector<Channel> &channels,
                        const std::string &playlistTitle);
                        