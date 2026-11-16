#include "M3UExporter.h"

#include <sstream>

std::string ExportToM3U(const std::vector<Channel> &channels,
                        const std::string &playlistTitle) {
  std::ostringstream oss;
  oss << "#EXTM3U\n";
  if (!playlistTitle.empty())
    oss << "#PLAYLIST:" << playlistTitle << "\n\n";

  for (const auto &ch : channels) {
    oss << "#EXTINF:-1";
    for (const auto &[key, value] : ch.attributes()) {
      if (!key.empty() && !value.empty()) {
        oss << " " << key << "=\"" << value << "\"";
      }
    }
    oss << "," << ch.getName() << "\n";
    oss << ch.getUrl() << "\n";
  }
  return oss.str();
}
