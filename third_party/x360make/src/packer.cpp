
#include "packer.h"
#include <cstdlib>

void PackXex(const std::wstring& elf, const std::wstring& xex) {
    std::wstring cmd = L"xextool.exe -c " + elf + L" -o " + xex;
    _wsystem(cmd.c_str());
}
