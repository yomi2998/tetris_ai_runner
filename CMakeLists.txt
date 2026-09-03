cmake_minimum_required(VERSION 3.50)

project(tetris_ai LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(fast_reachability INTERFACE)
target_include_directories(fast_reachability INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(fast_reachability INTERFACE cxx_std_23)

enable_testing()

string(TOLOWER "${CMAKE_CXX_COMPILER}" _cxx_compiler_lower)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-flto)
    add_link_options(-flto)
endif()

set(TETRIS_AI_SOURCES
    src/ai.cpp
    src/dllmain.c
    src/search_tspin.cpp
    src/random.cpp
    src/rule_toj.cpp
    src/tetris_core.cpp
    src/ai_zzz.cpp
)

add_library(tetris_ai_objects OBJECT ${TETRIS_AI_SOURCES})
target_include_directories(tetris_ai_objects PRIVATE src)
set_target_properties(tetris_ai_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(tetris_ai SHARED $<TARGET_OBJECTS:tetris_ai_objects>)
target_include_directories(tetris_ai PRIVATE src)
if (_cxx_compiler_lower MATCHES "mingw")
    target_link_options(tetris_ai PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

add_executable(match
    src/match.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/random.cpp
)
target_include_directories(match PRIVATE src)
if (_cxx_compiler_lower MATCHES "mingw")
    target_link_options(match PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

add_executable(tetris_profile
    src/tetris_profile.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/random.cpp
)
target_include_directories(tetris_profile PRIVATE src)

add_executable(tuner
    src/tuner.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/random.cpp
)
target_include_directories(tuner PRIVATE src)

add_executable(migration_fixtures
    src/migration_fixtures.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/random.cpp
)
target_include_directories(migration_fixtures PRIVATE src)

add_executable(spec_oracle
    src/spec_oracle.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/random.cpp
)
target_include_directories(spec_oracle PRIVATE src)

add_executable(migration_compare
    src/migration_compare.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/random.cpp
)
target_include_directories(migration_compare PRIVATE src)

add_executable(tetris_board_tests
    tests/tetris_board_tests.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/random.cpp
)
target_link_libraries(tetris_board_tests PRIVATE fast_reachability)
target_include_directories(tetris_board_tests PRIVATE src tests)
add_test(NAME tetris_board_tests COMMAND tetris_board_tests)
