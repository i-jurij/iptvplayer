#include "BackendFactory.h"
#include "MpvBackend.h"

std::unique_ptr<IPlayerBackend> CreateBackend(wxWindow *parent,
                                              const MpvInitOptions &opts) {
  return std::make_unique<MpvBackend>(parent, opts);
}
