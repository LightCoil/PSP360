cmake_minimum_required(VERSION 3.14)
project(XEX2Writer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Реальная реализация.
#
# Канонический файл - xex_writer.cpp в корне этого каталога. Файлы
# src/xex_writer.cpp и include/xexwriter/xex_writer.hpp - заглушки,
# они намеренно НЕ подключаются к сборке (см. комментарии в них).
# ---------------------------------------------------------------------------
add_library(xexwriter STATIC
    xex_writer.cpp
    xex_writer.hpp
)
target_include_directories(xexwriter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(xexwriter PRIVATE /W4)
else()
    target_compile_options(xexwriter PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Тесты структуры выходного XEX2.
# Проверяют файл по раскладке из документации Free60, а не повторным
# разбором собственного вывода.
# ---------------------------------------------------------------------------
add_executable(xexwriter_tests tests/test_xex.cpp)
target_link_libraries(xexwriter_tests PRIVATE xexwriter)

enable_testing()
add_test(NAME xexwriter_tests COMMAND xexwriter_tests)

# ---------------------------------------------------------------------------
# Пример использования.
# ---------------------------------------------------------------------------
add_executable(xexwriter_example examples/example_basic.cpp)
target_link_libraries(xexwriter_example PRIVATE xexwriter)
