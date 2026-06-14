#pragma once
#include "IPlayerBackend.h"
#include <memory>

std::unique_ptr<IPlayerBackend> CreateBackend();
