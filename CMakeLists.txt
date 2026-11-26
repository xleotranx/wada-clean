cmake_minimum_required(VERSION 3.16)
project(wada_clean C)

# Shipped binaries must run on a machine with nothing but Windows installed, so
# link the MinGW runtime (libwinpthread, libgcc, UCRT) statically. Without this a
# MinGW toolchain produces an exe that fails to start with STATUS_DLL_NOT_FOUND
# on stock Windows (missing libwinpthread-1.dll).
if(MINGW)
    add_link_options(-static -static-libgcc)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(WADA_C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/wada-c" CACHE PATH "Path to wada-c")
get_filename_component(WADA_C_ABS "${WADA_C_DIR}" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(WADA_BUILD_APPS OFF CACHE BOOL "Build Wada apps and examples" FORCE)
set(WADA_BUILD_TESTS OFF CACHE BOOL "Build Wada tests" FORCE)
add_subdirectory("${WADA_C_ABS}" "${CMAKE_BINARY_DIR}/wada-c")

# Pure C99 engine (zero OS headers).
add_library(wc_engine STATIC
    src/engine/wc_safety.c
    src/engine/wc_rules.c
    src/engine/wc_history.c
    src/engine/wc_quarantine.c
    src/engine/wc_deep.c
    src/engine/wc_leftovers.c
    src/engine/wc_sha256.c
    src/engine/wc_worker.c
    src/engine/wc_disk.c
    src/engine/wc_exclusions.c
    src/engine/wc_updater.c
    src/engine/wc_cookies.c
    src/engine/wc_cookie_job.c
    src/engine/wc_list_job.c
)
target_include_directories(wc_engine PUBLIC src/engine src/platform)
target_link_libraries(wc_engine PUBLIC wada_core wc_platform)
if(MSVC)
    target_compile_options(wc_engine PRIVATE /W4)
else()
    target_compile_options(wc_engine PRIVATE -Wall -Wextra)
endif()

# Platform backend.
if(WIN32)
    add_library(wc_platform STATIC
        src/platform/wc_cookies_sql.c
        src/platform/win32/wc_win_fs.c
        src/platform/win32/wc_win_proc.c
        src/platform/win32/wc_win_startup.c
        src/platform/win32/wc_win_apps.c
        src/platform/win32/wc_win_updater.c
        src/platform/win32/wc_win_sched.c
        src/platform/win32/wc_win_dialog.c
        src/platform/win32/wc_win_cookies.c
        src/platform/win32/wc_win_deep.c
    )
    target_include_directories(wc_platform PUBLIC src/platform src/engine "${WADA_C_ABS}/vendor/sqlite")
    target_link_libraries(wc_platform PUBLIC wc_engine wada_sqlite shell32 advapi32 user32 ole32 uuid)
    if(MSVC)
        target_compile_options(wc_platform PRIVATE /W4)
    else()
        target_compile_options(wc_platform PRIVATE -Wall -Wextra)
    endif()
elseif(APPLE)
    find_package(Threads REQUIRED)
    add_library(wc_platform STATIC
        src/platform/wc_cookies_sql.c
        src/platform/macos/wc_mac_fs.c
        src/platform/macos/wc_mac_proc.c
        src/platform/macos/wc_mac_startup.c
        src/platform/macos/wc_mac_apps.c
        src/platform/macos/wc_mac_updater.c
        src/platform/macos/wc_mac_sched.c
        src/platform/macos/wc_mac_cookies.c
        src/platform/macos/wc_mac_deep.c
    )
    target_include_directories(wc_platform PUBLIC src/platform src/engine "${WADA_C_ABS}/vendor/sqlite")
    target_link_libraries(wc_platform PUBLIC wc_engine wada_sqlite Threads::Threads)
    target_compile_options(wc_platform PRIVATE -Wall -Wextra)
else()
    message(FATAL_ERROR "Supported platforms are WIN32 and APPLE")
endif()

# Shipped GUI application. Step 2 of the One Door plan: one home screen that
# answers "how much can I get back?", and one Tools door for everything else.
add_executable(wada_clean
    src/main.c
    src/ui/wc_product.c
    src/ui/wc_view_home.c
    src/ui/wc_view_tools.c
    src/ui/wc_view_apps.c
    src/ui/wc_view_disk.c
)
target_include_directories(wada_clean PRIVATE src/engine src/platform src/ui)
target_link_libraries(wada_clean PRIVATE wc_engine wc_platform wada_shell)
if(MSVC)
    target_compile_options(wada_clean PRIVATE /W4)
else()
    target_compile_options(wada_clean PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    set_target_properties(wada_clean PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Isolated fixture unit tests.
enable_testing()

if(WIN32)
add_executable(wada_clean_tests
    tests/test_phase1.c
)
target_include_directories(wada_clean_tests PRIVATE src/engine src/platform "${WADA_C_ABS}/src/providers/sqlite")
target_link_libraries(wada_clean_tests PRIVATE wc_engine wc_platform wada_sqlite)
if(MSVC)
    target_compile_options(wada_clean_tests PRIVATE /W4)
else()
    target_compile_options(wada_clean_tests PRIVATE -Wall -Wextra)
endif()

add_executable(wada_phase2_tests
    tests/test_phase2.c
)
target_include_directories(wada_phase2_tests PRIVATE src/engine src/platform)
target_link_libraries(wada_phase2_tests PRIVATE wc_engine wc_platform wada_core)
if(MSVC)
    target_compile_options(wada_phase2_tests PRIVATE /W4)
else()
    target_compile_options(wada_phase2_tests PRIVATE -Wall -Wextra)
endif()

add_executable(wada_phase3_tests
    tests/test_phase3.c
)
target_include_directories(wada_phase3_tests PRIVATE src/engine src/platform "${WADA_C_ABS}/src/providers/sqlite")
target_link_libraries(wada_phase3_tests PRIVATE wc_engine wc_platform wada_sqlite)
if(MSVC)
    target_compile_options(wada_phase3_tests PRIVATE /W4)
else()
    target_compile_options(wada_phase3_tests PRIVATE -Wall -Wextra)
endif()

add_executable(wada_phase4_tests
    tests/test_phase4.c
)
target_include_directories(wada_phase4_tests PRIVATE src/engine src/platform "${WADA_C_ABS}/src/providers/sqlite" "${WADA_C_ABS}/vendor/sqlite")
target_link_libraries(wada_phase4_tests PRIVATE wc_engine wc_platform wada_sqlite)
if(MSVC)
    target_compile_options(wada_phase4_tests PRIVATE /W4)
else()
    target_compile_options(wada_phase4_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME phase1 COMMAND wada_clean_tests)
add_test(NAME phase2 COMMAND wada_phase2_tests)
add_test(NAME phase3 COMMAND wada_phase3_tests)
add_test(NAME phase4 COMMAND wada_phase4_tests)

add_executable(wada_wave1_tests
    tests/test_wave1.c
)
target_include_directories(wada_wave1_tests PRIVATE src/engine src/platform "${WADA_C_ABS}/src/providers/sqlite")
target_link_libraries(wada_wave1_tests PRIVATE wc_engine wc_platform wada_sqlite)
if(MSVC)
    target_compile_options(wada_wave1_tests PRIVATE /W4)
else()
    target_compile_options(wada_wave1_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME wave1 COMMAND wada_wave1_tests)

add_executable(wada_wave3_tests
    tests/test_wave3.c
)
target_include_directories(wada_wave3_tests PRIVATE src/engine src/platform)
target_link_libraries(wada_wave3_tests PRIVATE wc_engine wc_platform wada_core)
if(MSVC)
    target_compile_options(wada_wave3_tests PRIVATE /W4)
else()
    target_compile_options(wada_wave3_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME wave3 COMMAND wada_wave3_tests)
endif()

add_executable(wada_wave2_tests
    tests/test_wave2.c
)
target_include_directories(wada_wave2_tests PRIVATE src/engine src/platform)
target_link_libraries(wada_wave2_tests PRIVATE wc_engine wc_platform wada_core)
if(MSVC)
    target_compile_options(wada_wave2_tests PRIVATE /W4)
else()
    target_compile_options(wada_wave2_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME wave2 COMMAND wada_wave2_tests)

add_executable(wada_phase5_tests
    tests/test_phase5.c
)
target_include_directories(wada_phase5_tests PRIVATE src/engine src/platform)
target_link_libraries(wada_phase5_tests PRIVATE wc_engine wc_platform wada_core)
if(MSVC)
    target_compile_options(wada_phase5_tests PRIVATE /W4)
else()
    target_compile_options(wada_phase5_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME phase5 COMMAND wada_phase5_tests)
