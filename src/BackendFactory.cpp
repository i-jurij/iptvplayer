#include "BackendFactory.h"
#include "MpvBackend.h"

// BackendFactory.h или BackendFactory.cpp
std::unique_ptr<IPlayerBackend> CreateBackend(wxWindow *parentWindow) {
  return std::make_unique<MpvBackend>(parentWindow);
}
