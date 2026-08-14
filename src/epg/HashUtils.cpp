#include "HashUtils.h"
#include <iomanip>
#include <mutex>
#include <sstream>

static uint32_t crc32_table[256];
static std::once_flag crc32_once;

static void init_crc32_table() {
  std::call_once(crc32_once, []() {
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t crc = i;
      for (uint32_t j = 0; j < 8; ++j) {
        if (crc & 1)
          crc = (crc >> 1) ^ polynomial;
        else
          crc >>= 1;
      }
      crc32_table[i] = crc;
    }
  });
}

std::string stable_hash(const std::string &input) {
  init_crc32_table();
  uint32_t crc = 0xFFFFFFFF;
  for (char c : input) {
    crc = (crc >> 8) ^ crc32_table[(crc ^ static_cast<uint8_t>(c)) & 0xFF];
  }
  crc ^= 0xFFFFFFFF;
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(8) << crc;
  return ss.str();
}
