#include "BackendFactory.h"
#include "LogControl.h"
#include "MpvBackend.h"

std::unique_ptr<IPlayerBackend> CreateBackend() {
  LOG_DEBUG("BackendFactory: using internal mpv backend");
  return std::make_unique<MpvBackend>();
}
