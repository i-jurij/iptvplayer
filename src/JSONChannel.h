#pragma once
#include "Channel.h"

#include <rapidjson/document.h>

// Parse a single JSON object into a Channel.
// Canonical field names (PlaylistJson format) take priority; common aliases
// (link/stream/src/file/location for url, title/label for name, etc.) are
// used as fallback only when the canonical field is empty.
// Unknown string members go to attributes().
Channel ParseChannelFromJson(const rapidjson::Value &obj);
