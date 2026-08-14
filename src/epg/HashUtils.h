#pragma once
#include <string>

/**
 * Детерминированный хеш (CRC32) для сравнения списков каналов.
 * Используется и в EPGDatabase, и в EPGManager.
 */
std::string stable_hash(const std::string &input);
