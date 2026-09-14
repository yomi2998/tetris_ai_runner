cmake_minimum_required(VERSION 3.50)

project(tetris_ai LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

string(TOLOWER "${CMAKE_CXX_COMPILER}" _cxx_compiler_lower)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-flto)
    add_link_options(-flto)
endif()

set(TETRIS_AI_SOURCES
    src/ai.cpp
    src/ai_ax.cpp
    src/ai_farter.cpp
    src/ai_misaka.cpp
    src/ai_tag.cpp
    src/dllmain.c
    src/integer_utils.cpp
    src/rule_asrs.cpp
    src/search_path.cpp
    src/search_simple.cpp
    src/search_simulate.cpp
    src/search_tag.cpp
    src/search_tspin.cpp
    src/random.cpp
    src/rule_c2.cpp
    src/rule_qq.cpp
    src/rule_srsx.cpp
    src/rule_st.cpp
    src/rule_tag.cpp
    src/rule_toj.cpp
    src/tetris_core.cpp
    src/ai_zzz.cpp
    src/search_cautious.cpp
    src/rule_srs.cpp
)

add_library(tetris_ai_objects OBJECT ${TETRIS_AI_SOURCES})
target_include_directories(tetris_ai_objects PRIVATE src)
set_target_properties(tetris_ai_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(tetris_ai_shared name)
    add_library(${name} SHARED $<TARGET_OBJECTS:tetris_ai_objects>)
    target_include_directories(${name} PRIVATE src)
    if (_cxx_compiler_lower MATCHES "mingw")
        target_link_options(${name} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()
endfunction()

tetris_ai_shared(tetris_ai)
tetris_ai_shared(tetris_ai_runner)

add_library(top_pso SHARED
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
    src/rule_srs.cpp
    src/search_tspin.cpp
    src/tetris_core.cpp
    src/ppt_pso.cpp
)
target_include_directories(top_pso PRIVATE src)

add_executable(match
    src/match.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_simple.cpp
    src/search_tspin.cpp
    src/integer_utils.cpp
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
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(tetris_profile PRIVATE src)

add_executable(tuner
    src/tuner.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(tuner PRIVATE src)

add_executable(movegen_probe
    src/movegen_probe.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(movegen_probe PRIVATE src)

add_executable(attach_test
    src/attach_test.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(attach_test PRIVATE src)

find_package(Threads REQUIRED)

add_library(c_cmaes STATIC
    third_party/c-cmaes/src/cmaes.c
)
target_include_directories(c_cmaes PUBLIC third_party/c-cmaes/src)
set_target_properties(c_cmaes PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
if (UNIX)
    target_link_libraries(c_cmaes PUBLIC m)
endif()

add_executable(tournament_bracket_test
    src/tournament_bracket.cpp
    src/tournament_bracket_test.cpp
)
target_include_directories(tournament_bracket_test PRIVATE src)

add_executable(tournament_scheduler_test
    src/tournament_scheduler.cpp
    src/tournament_scheduler_test.cpp
)
target_include_directories(tournament_scheduler_test PRIVATE src)
target_link_libraries(tournament_scheduler_test PRIVATE Threads::Threads)

add_executable(tournament_rating_test
    src/tournament_rating.cpp
    src/tournament_rating_test.cpp
)
target_include_directories(tournament_rating_test PRIVATE src)

add_executable(tournament_checkpoint_test
    src/tournament_checkpoint.cpp
    src/tournament_checkpoint_test.cpp
)
target_include_directories(tournament_checkpoint_test PRIVATE src)

add_executable(tournament_cmaes_test
    src/tournament_cmaes.cpp
    src/tournament_cmaes_test.cpp
)
target_include_directories(tournament_cmaes_test PRIVATE src)
target_link_libraries(tournament_cmaes_test PRIVATE c_cmaes)

add_library(tournament_toj_objects OBJECT
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(tournament_toj_objects PRIVATE src)

add_executable(tuning_domain_test
    src/tuning_domain_test.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tuning_domain_test PRIVATE src)

add_executable(tuning_match_test
    src/tuning_match_test.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tuning_match_test PRIVATE src)
target_link_libraries(tuning_match_test PRIVATE Threads::Threads)

add_executable(tournament_promotion_test
    src/tournament_promotion_test.cpp
)
target_include_directories(tournament_promotion_test PRIVATE src)
target_link_libraries(tournament_promotion_test PRIVATE Threads::Threads)

add_executable(tournament_ordinal_test
    src/tournament_ordinal_test.cpp
)
target_include_directories(tournament_ordinal_test PRIVATE src)

add_executable(tournament_bytes_test
    src/tournament_bytes_test.cpp
)
target_include_directories(tournament_bytes_test PRIVATE src)

add_executable(tournament_runner_test
    src/tournament_bracket.cpp
    src/tournament_runner.cpp
    src/tournament_scheduler.cpp
    src/tournament_runner_test.cpp
)
target_include_directories(tournament_runner_test PRIVATE src)
target_link_libraries(tournament_runner_test PRIVATE Threads::Threads)

add_executable(tuning_engine_match_test
    src/tuning_engine_match_test.cpp
    src/tournament_scheduler.cpp
    src/rule_srs.cpp
    src/search_simple.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tuning_engine_match_test PRIVATE src)
target_link_libraries(tuning_engine_match_test PRIVATE Threads::Threads)

add_executable(tetris_memory_budget_test
    src/tetris_memory_budget_test.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tetris_memory_budget_test PRIVATE src)

add_executable(tournament_tuner
    src/tournament_tuner.cpp
    src/tournament_bracket.cpp
    src/tournament_scheduler.cpp
    src/tournament_rating.cpp
    src/tournament_checkpoint.cpp
    src/tournament_cmaes.cpp
    src/tournament_runner.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tournament_tuner PRIVATE src third_party/c-cmaes/src)
target_link_libraries(tournament_tuner PRIVATE c_cmaes Threads::Threads)

enable_testing()

add_test(NAME attach_test COMMAND attach_test)
add_test(NAME tetris_profile_cross_check COMMAND tetris_profile --moves 5 --iters 100 --seed 1 --maxdepth 6 --cross-check)
add_test(NAME tournament_bracket_test COMMAND tournament_bracket_test)
add_test(NAME tournament_scheduler_test COMMAND tournament_scheduler_test)
add_test(NAME tournament_rating_test COMMAND tournament_rating_test)
add_test(NAME tournament_checkpoint_test COMMAND tournament_checkpoint_test)
add_test(NAME tournament_cmaes_test COMMAND tournament_cmaes_test)
add_test(NAME tuning_domain_test COMMAND tuning_domain_test)
add_test(NAME tuning_match_test COMMAND tuning_match_test)
add_test(NAME tournament_promotion_test COMMAND tournament_promotion_test)
add_test(NAME tournament_ordinal_test COMMAND tournament_ordinal_test)
add_test(NAME tournament_bytes_test COMMAND tournament_bytes_test)
add_test(NAME tournament_runner_test COMMAND tournament_runner_test)
add_test(NAME tuning_engine_match_test COMMAND tuning_engine_match_test)
add_test(NAME tetris_memory_budget_test COMMAND tetris_memory_budget_test)
