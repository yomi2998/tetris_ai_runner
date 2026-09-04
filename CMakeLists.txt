cmake_minimum_required(VERSION 3.50)

if (NOT DEFINED CMAKE_CXX_SCAN_FOR_MODULES)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

project(tetris_ai LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TETRIS_SANITIZERS "Instrument the build with ASan and UBSan" OFF)
if (TETRIS_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(fast_reachability INTERFACE)
target_include_directories(fast_reachability INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(fast_reachability INTERFACE cxx_std_23)

enable_testing()

string(TOLOWER "${CMAKE_CXX_COMPILER}" _cxx_compiler_lower)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT TETRIS_SANITIZERS)
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

add_executable(fast_reachability_tests
    tests/fast_reachability_tests.cpp
)
target_link_libraries(fast_reachability_tests PRIVATE fast_reachability)
target_include_directories(fast_reachability_tests PRIVATE src tests src/fast-reachability)
target_compile_definitions(fast_reachability_tests PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:FAST_REACHABILITY_RUN_PERFT>)
add_test(NAME fast_reachability_tests COMMAND fast_reachability_tests)

add_executable(fast_reachability_perft
    tests/fast_reachability_tests.cpp
)
target_link_libraries(fast_reachability_perft PRIVATE fast_reachability)
target_include_directories(fast_reachability_perft PRIVATE src tests src/fast-reachability)
target_compile_definitions(fast_reachability_perft PRIVATE FAST_REACHABILITY_PERFT_ONLY)
target_compile_options(fast_reachability_perft PRIVATE -O3)
add_test(NAME fast_reachability_perft COMMAND fast_reachability_perft)

add_executable(arrival_candidates
    src/arrival_candidates.cpp
)
target_link_libraries(arrival_candidates PRIVATE fast_reachability)
target_include_directories(arrival_candidates PRIVATE src tests src/fast-reachability)
target_compile_options(arrival_candidates PRIVATE -O3 -march=native)

add_executable(bfs_bench
    src/bfs_bench.cpp
)
target_link_libraries(bfs_bench PRIVATE fast_reachability)
target_include_directories(bfs_bench PRIVATE src tests src/fast-reachability)
target_compile_options(bfs_bench PRIVATE -O3 -march=native)

add_executable(raw_bench_current
    tests/raw_bench_main.cpp
)
target_link_libraries(raw_bench_current PRIVATE fast_reachability)
target_include_directories(raw_bench_current PRIVATE src tests src/fast-reachability)
target_compile_options(raw_bench_current PRIVATE -O3 -march=native)

add_executable(raw_bench_frozen
    tests/raw_bench_main.cpp
)
target_include_directories(raw_bench_frozen BEFORE PRIVATE tests/frozen_0c35e13)
target_include_directories(raw_bench_frozen PRIVATE src tests)
target_compile_options(raw_bench_frozen PRIVATE -O3 -march=native)

add_executable(raw_bench_frozen_180fix
    tests/raw_bench_main.cpp
)
target_include_directories(raw_bench_frozen_180fix BEFORE PRIVATE tests/frozen_0c35e13_180fix tests/frozen_0c35e13)
target_include_directories(raw_bench_frozen_180fix PRIVATE src tests)
target_compile_options(raw_bench_frozen_180fix PRIVATE -O3 -march=native)

find_program(GIT_EXECUTABLE git)

function(reach_provenance target detail_name)
    cmake_parse_arguments(PROV "AUTO_GIT" "PREFIX" "SOURCES" ${ARGN})
    set(detail "${PROV_PREFIX}")
    if (PROV_AUTO_GIT AND GIT_EXECUTABLE)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE root_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/fast-reachability
            OUTPUT_VARIABLE kernel_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain -- src tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE root_dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REGEX REPLACE "
" "," root_dirty "${root_dirty}")
        if (root_dirty STREQUAL "")
            set(root_dirty "clean")
        endif()
        string(JOIN " " detail "${detail}" "root=${root_head}" "root_worktree=${root_dirty}" "kernel=${kernel_head}")
    endif()
    foreach (source IN LISTS PROV_SOURCES)
        if (IS_ABSOLUTE "${source}")
            set(path "${source}")
        else()
            set(path "${CMAKE_SOURCE_DIR}/${source}")
        endif()
        set(label "${path}")
        if (path MATCHES "^${CMAKE_SOURCE_DIR}/")
            string(REGEX REPLACE "^${CMAKE_SOURCE_DIR}/" "" label "${path}")
        elseif (path MATCHES "^${REFERENCE_A_ROOT}/")
            string(REGEX REPLACE "^${REFERENCE_A_ROOT}/" "refa_" label "${path}")
        endif()
        string(REPLACE "/" "_" label "${label}")
        file(SHA256 ${path} digest)
        string(APPEND detail " ${label}=${digest}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${path})
    endforeach()
    string(STRIP "${detail}" detail)
    target_compile_definitions(${target} PRIVATE ${detail_name}="${detail}")
endfunction()

reach_provenance(raw_bench_current PRODUCER_DETAIL AUTO_GIT
    SOURCES src/fast-reachability/search.hpp tests/raw_bench_main.cpp)
reach_provenance(raw_bench_frozen PRODUCER_DETAIL AUTO_GIT
    SOURCES tests/frozen_0c35e13/search.hpp tests/raw_bench_main.cpp)
reach_provenance(raw_bench_frozen_180fix PRODUCER_DETAIL AUTO_GIT
    SOURCES tests/frozen_0c35e13_180fix/search.hpp tests/raw_bench_main.cpp)
reach_provenance(arrival_candidates PRODUCER_DETAIL AUTO_GIT
    SOURCES src/fast-reachability/search.hpp src/arrival_candidates.cpp src/candidate_format.h)

add_test(NAME raw_bench_warmup_smoke
    COMMAND $<TARGET_FILE:raw_bench_current> --reps 2 --warmup 3 --producer smoke
)
add_test(NAME raw_bench_warmup_smoke_frozen
    COMMAND $<TARGET_FILE:raw_bench_frozen_180fix> --reps 2 --warmup 3 --producer smoke
)

add_test(NAME raw_parity_frozen_180fix
    COMMAND ${CMAKE_COMMAND}
        -DA=$<TARGET_FILE:raw_bench_current>
        -DB=$<TARGET_FILE:raw_bench_frozen_180fix>
        -DPREFIX=RAWCASE -DMODE=equal
        "-DARGS_A=--reps 0 --warmup 0 --producer parity"
        "-DARGS_B=--reps 0 --warmup 0 --producer parity"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/reach_parity_check.cmake
)
add_test(NAME raw_parity_frozen_180off
    COMMAND ${CMAKE_COMMAND}
        -DA=$<TARGET_FILE:raw_bench_current>
        -DB=$<TARGET_FILE:raw_bench_frozen>
        -DPREFIX=RAWCASE -DMODE=equal
        "-DARGS_A=--reps 0 --warmup 0 --no-180 --producer parity"
        "-DARGS_B=--reps 0 --warmup 0 --no-180 --producer parity"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/reach_parity_check.cmake
)

set(REFERENCE_A_ROOT "/home/icly/Documents/GitHub/tet" CACHE PATH "Pinned Reference A (tet) checkout")
set(REFERENCE_A_PIN_HEAD "e9a90d8")
set(REFERENCE_A_PIN_KERNEL_HEAD "0c35e130abb733caefababe0d08c8faf466740e7")

if (EXISTS ${REFERENCE_A_ROOT}/src/search/reachability_search.cpp)
    set(refa_provenance "refa_root=git-unavailable")
    if (GIT_EXECUTABLE)
        execute_process(COMMAND ${GIT_EXECUTABLE} -C ${REFERENCE_A_ROOT} rev-parse HEAD
            OUTPUT_VARIABLE refa_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} -C ${REFERENCE_A_ROOT}/src/fast-reachability rev-parse HEAD
            OUTPUT_VARIABLE refa_kernel_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} -C ${REFERENCE_A_ROOT} status --porcelain -- src
            OUTPUT_VARIABLE refa_dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REGEX REPLACE "
" "," refa_dirty "${refa_dirty}")
        if (refa_dirty STREQUAL "")
            set(refa_dirty "clean")
        endif()
        string(JOIN " " refa_provenance "refa_root=${refa_head}" "refa_worktree=${refa_dirty}"
            "refa_kernel=${refa_kernel_head}")
        string(LENGTH "${refa_head}" refa_head_len)
        if (NOT "${refa_head_len}" GREATER_EQUAL 7 OR NOT "${refa_head}" MATCHES "^${REFERENCE_A_PIN_HEAD}")
            message(WARNING "reference_a_frozen: ${REFERENCE_A_ROOT} HEAD is ${refa_head}, expected ${REFERENCE_A_PIN_HEAD}")
        endif()
        if (NOT refa_kernel_head STREQUAL REFERENCE_A_PIN_KERNEL_HEAD)
            message(WARNING "reference_a_frozen: Reference A kernel is ${refa_kernel_head}, expected ${REFERENCE_A_PIN_KERNEL_HEAD}")
        endif()
        if (NOT refa_dirty STREQUAL "clean")
            message(WARNING "reference_a_frozen: Reference A src/ worktree is not clean: ${refa_dirty}")
        endif()
    endif()

    add_library(reference_a_search STATIC ${REFERENCE_A_ROOT}/src/search/reachability_search.cpp)
    target_include_directories(reference_a_search PUBLIC ${REFERENCE_A_ROOT}/src)
    target_compile_options(reference_a_search PRIVATE -O3 -march=native)
    target_compile_definitions(reference_a_search PRIVATE NDEBUG)

    add_executable(reference_a_frozen
        tests/reference_a_frozen_main.cpp
    )
    target_link_libraries(reference_a_frozen PRIVATE reference_a_search)
    target_include_directories(reference_a_frozen BEFORE PRIVATE ${REFERENCE_A_ROOT}/src)
    target_include_directories(reference_a_frozen PRIVATE tests src)
    target_compile_options(reference_a_frozen PRIVATE -O3 -march=native)
    target_compile_definitions(reference_a_frozen PRIVATE NDEBUG)
    reach_provenance(reference_a_frozen PRODUCER_DETAIL AUTO_GIT PREFIX "${refa_provenance}"
        SOURCES
            ${REFERENCE_A_ROOT}/src/search/reachability_search.cpp
            ${REFERENCE_A_ROOT}/src/fast-reachability/search.hpp
            tests/reference_a_frozen_main.cpp
            src/candidate_format.h)

    add_test(NAME t_contract_reference_a_180off
        COMMAND ${CMAKE_COMMAND}
            -DA=$<TARGET_FILE:arrival_candidates>
            -DB=$<TARGET_FILE:reference_a_frozen>
            -DPREFIX=CASE -DMODE=equal
            "-DARGS_A=--reps 0 --warmup 0 --no-180"
            "-DARGS_B=--reps 0 --warmup 0 --no-180"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/reach_parity_check.cmake
    )
    find_program(PYTHON3_EXECUTABLE python3)
    if (PYTHON3_EXECUTABLE)
        add_test(NAME t_contract_reference_a_production
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_candidate_inclusion.py
                $<TARGET_FILE:arrival_candidates> $<TARGET_FILE:reference_a_frozen>
        )
    else()
        message(STATUS "t_contract_reference_a_production: skipped, python3 not found")
    endif()
else()
    message(STATUS "reference_a_frozen: not configured, set -DREFERENCE_A_ROOT to a pinned tet checkout")
endif()
