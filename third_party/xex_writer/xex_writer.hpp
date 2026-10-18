#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <map>

namespace xexlib {

/// Пишет XEX2-файл из ELF32 (big-endian) бинаря.
///
/// @param elf           Массив байт с ELF-файлом (PowerPC, BE).
/// @param importLibs    Карта: имя библиотеки → список импортируемых функций.
/// @param exportLibs    Карта: имя библиотеки → список экспортируемых функций.
/// @param xex_out       Результирующий массив байт для XEX.
/// @param error         Строка ошибки в случае сбоя.
/// @return              true при успехе, false — при ошибке.
bool write_xex_from_elf(const std::vector<uint8_t>& elf,
                        const std::map<std::string, std::vector<std::string>>& importLibs,
                        const std::map<std::string, std::vector<std::string>>& exportLibs,
                        std::vector<uint8_t>& xex_out,
                        std::string& error);

} // namespace xexlib
