#include "MainFrame.h"

#include "ConfigManager.h"
#include "LogControl.h"
#include "Playlist.h"
#include "PlaylistManager.h"
#include "xtream/XtreamDatabase.h"

#include <ctime>

namespace {

constexpr long kWriteIntervalMs = 30000;
constexpr double kMinPositionSec = 10.0;

} // namespace

void MainFrame::OnPlaybackProgress(const std::string &mediaKey, double pos,
                                   double dur, bool flush) {
  if (mediaKey.empty())
    return;
  if (pos < 0.0)
    return;

  if (m_hotPos.mediaKey != mediaKey) {
    FlushPlaybackPosition(true);
    m_hotPos = HotPlaybackPos{};
    m_hotPos.mediaKey = mediaKey;
  }

  m_hotPos.position = pos;
  m_hotPos.duration = dur;
  m_hotPos.hasData = true;

  const wxLongLong now = wxGetUTCTimeMillis();
  if (flush || m_hotPos.lastWriteMs == 0 ||
      (now - m_hotPos.lastWriteMs).GetValue() >= kWriteIntervalMs) {
    FlushPlaybackPosition(true);
  }
}

void MainFrame::FlushPlaybackPosition(bool force) {
  if (m_hotPos.mediaKey.empty() || !m_hotPos.hasData)
    return;
  if (m_hotPos.position < kMinPositionSec)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  int threshold = 95;
  if (auto *cfg = getConfigManager())
    threshold = cfg->getInt("xtream_resume_complete_percent", 95);
  if (threshold < 80)
    threshold = 80;
  if (threshold > 100)
    threshold = 100;

  const bool finished =
      m_hotPos.duration > 0.0 &&
      (m_hotPos.position * 100.0 / m_hotPos.duration) >= threshold;

  xtream::XtreamDatabase::PlaybackPosition p;
  p.mediaKey = m_hotPos.mediaKey;
  p.position = m_hotPos.position;
  p.duration = m_hotPos.duration;
  p.updatedAt = static_cast<int64_t>(std::time(nullptr));
  p.finished = finished;

  if (!db->SavePlaybackPosition(p)) {
    LOG_WARN("MainFrame: failed to save playback position for '%s'",
             m_hotPos.mediaKey.c_str());
    return;
  }

  m_hotPos.lastWriteMs = wxGetUTCTimeMillis();

  if (force) {
    LOG_DEBUG("MainFrame: saved playback pos '%s' (%.2f/%.2f, finished=%d)",
              m_hotPos.mediaKey.c_str(), m_hotPos.position, m_hotPos.duration,
              finished ? 1 : 0);
  }
}

void MainFrame::ClearPlaybackPosition() { m_hotPos = HotPlaybackPos{}; }
