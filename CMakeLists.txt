# =============================================================================
# psp360Phoenix — эмулятор PSP, работающий на Xbox 360
#
# Две цели сборки:
#   psp360_core   — статическая библиотека с ядром эмуляции (CPU, память,
#                   интерпретатор, VFPU, загрузчик). Не зависит от платформы.
#   psp360_host   — хостовый раннер для отладки и запуска тестов.
#   psp360_tests  — модульные тесты (GoogleTest).
#
# Сборка для хоста (MSVC):
#   cmake -S . -B build
#   cmake --build build --config Debug
#   ctest --test-dir build -C Debug --output-on-failure
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(psp360Phoenix CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- ядро
add_library(psp360_core STATIC
    core/cpu_state.cpp
    core/memory.cpp
    core/decoder.cpp
    core/logger.cpp
    interp/interpreter.cpp
    interp/ops_arith.cpp
    interp/ops_logic.cpp
    interp/ops_shift.cpp
    interp/ops_branch.cpp
    interp/ops_mem.cpp
    interp/ops_cop.cpp
    vfpu/vfpu_handler.cpp
    Loader/loader.cpp
)

target_include_directories(psp360_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------- хост-раннер
add_executable(psp360_host host/main.cpp)
target_link_libraries(psp360_host PRIVATE psp360_core)

# ---------------------------------------------------------------- тесты
option(PSP360_BUILD_TESTS "Собирать модульные тесты" ON)

if(PSP360_BUILD_TESTS)
    find_package(GTest QUIET)

    if(NOT GTest_FOUND)
        # Ищем локальную сборку GoogleTest (D:\googletest\build\lib)
        set(_gtest_hint "D:/googletest/build")
        if(EXISTS "${_gtest_hint}/lib/Debug/gtest.lib")
            message(STATUS "Используется локальная сборка GoogleTest: ${_gtest_hint}")
            add_library(gtest_local STATIC IMPORTED)
            set_target_properties(gtest_local PROPERTIES
                IMPORTED_LOCATION "${_gtest_hint}/lib/Debug/gtest.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${_gtest_hint}/../../googletest/include")
            add_library(gtest_local_main STATIC IMPORTED)
            set_target_properties(gtest_local_main PROPERTIES
                IMPORTED_LOCATION "${_gtest_hint}/lib/Debug/gtest_main.lib")
            set(GTest_FOUND TRUE)
            set(PSP360_GTEST_LIBS gtest_local_main gtest_local)
        else()
            message(STATUS "GoogleTest не найден — тесты отключены")
        endif()
    else()
        set(PSP360_GTEST_LIBS GTest::gtest_main GTest::gtest)
    endif()

    if(GTest_FOUND)
        enable_testing()
        add_executable(psp360_tests
            tests/test_cpu.cpp
            tests/test_memory.cpp
            tests/test_interp.cpp
            tests/test_vfpu.cpp
            tests/test_loader.cpp
        )
        target_link_libraries(psp360_tests PRIVATE psp360_core ${PSP360_GTEST_LIBS})
        add_test(NAME psp360_tests COMMAND psp360_tests)
    endif()
endif()

# ---------------------------------------------------------------- установка
install(TARGETS psp360_core ARCHIVE DESTINATION lib)
install(TARGETS psp360_host   RUNTIME DESTINATION bin)