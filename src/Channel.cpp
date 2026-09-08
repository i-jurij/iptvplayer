#include "Channel.h"
#include "HashUtils.h"

void Channel::ensureUniqueId() {
  if (!m_uniqueId.empty())
    return;
  if (!m_tvgId.empty()) {
    m_uniqueId = m_tvgId;
    return;
  }
  // стабильный хэш от name + url
  m_uniqueId = stable_hash(m_name + "|" + m_url);
}