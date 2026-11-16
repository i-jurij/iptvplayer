#pragma once
#include "Playlist.h"
#include <string>

// Internal JSON serialization for Playlist (config storage format).
// Free functions, no state. Keeps Playlist free of rapidjson dependency
// and of the serialization concern.
std::string ToJson(const Playlist &pl);
bool FromJson(const std::string &json, Playlist &pl);