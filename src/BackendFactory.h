#pragma once
#include "IPlayerBackend.h"
#include "MpvBackend.h"

#include <memory>

std::unique_ptr<IPlayerBackend> CreateBackend(wxWindow *parent,
                                              const MpvInitOptions &opts = {});
