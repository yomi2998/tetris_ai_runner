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
target_include_directories(match PRIVATE src third_party/nlohmann/include)
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
    src/tournament/bracket.cpp
    src/tournament/bracket_test.cpp
)
target_include_directories(tournament_bracket_test PRIVATE src)

add_executable(tournament_scheduler_test
    src/tournament/scheduler.cpp
    src/tournament/scheduler_test.cpp
)
target_include_directories(tournament_scheduler_test PRIVATE src)
target_link_libraries(tournament_scheduler_test PRIVATE Threads::Threads)

add_executable(tournament_rating_test
    src/tournament/rating.cpp
    src/tournament/rating_test.cpp
)
target_include_directories(tournament_rating_test PRIVATE src)

add_executable(tournament_checkpoint_test
    src/tournament/checkpoint.cpp
    src/tournament/checkpoint_test.cpp
)
target_include_directories(tournament_checkpoint_test PRIVATE src third_party/nlohmann/include)

add_executable(tournament_cmaes_test
    src/tournament/cmaes.cpp
    src/tournament/cmaes_test.cpp
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
    src/tuning/domain_test.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tuning_domain_test PRIVATE src)

add_executable(tuning_match_test
    src/tuning/match_test.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tuning_match_test PRIVATE src)
target_link_libraries(tuning_match_test PRIVATE Threads::Threads)

add_executable(tournament_promotion_test
    src/tournament/promotion_test.cpp
)
target_include_directories(tournament_promotion_test PRIVATE src)
target_link_libraries(tournament_promotion_test PRIVATE Threads::Threads)

add_executable(tournament_ordinal_test
    src/tournament/ordinal_test.cpp
)
target_include_directories(tournament_ordinal_test PRIVATE src)

add_executable(tournament_bytes_test
    src/tournament/bytes_test.cpp
)
target_include_directories(tournament_bytes_test PRIVATE src)

add_executable(tournament_runner_test
    src/tournament/bracket.cpp
    src/tournament/runner.cpp
    src/tournament/scheduler.cpp
    src/tournament/runner_test.cpp
)
target_include_directories(tournament_runner_test PRIVATE src)
target_link_libraries(tournament_runner_test PRIVATE Threads::Threads)

add_executable(tuning_engine_match_test
    src/tuning/engine_match_test.cpp
    src/tournament/scheduler.cpp
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
    src/tournament/tuner.cpp
    src/tournament/audit.cpp
    src/tournament/ban_file.cpp
    src/tournament/bracket.cpp
    src/tournament/scheduler.cpp
    src/tournament/rating.cpp
    src/tournament/checkpoint.cpp
    src/tournament/cmaes.cpp
    src/tournament/journal.cpp
    src/tournament/repair.cpp
    src/tournament/runner.cpp
    src/tournament/wire.cpp
    src/tournament/registry.cpp
    $<TARGET_OBJECTS:tournament_toj_objects>
)
target_include_directories(tournament_tuner PRIVATE src third_party/c-cmaes/src third_party/nlohmann/include)
target_link_libraries(tournament_tuner PRIVATE c_cmaes Threads::Threads)

find_package(OpenSSL QUIET)

if (OPENSSL_FOUND)
    target_compile_definitions(tournament_tuner PRIVATE TUNER_HAS_REMOTE=1)
    target_sources(tournament_tuner PRIVATE
        src/tournament/net_transport.cpp
        src/tournament/remote_backend.cpp
        src/tournament/wire_crypto.cpp
    )
    target_link_libraries(tournament_tuner PRIVATE OpenSSL::Crypto OpenSSL::SSL)
endif()

add_executable(tournament_journal_test
    src/tournament/journal.cpp
    src/tournament/wire.cpp
    src/tournament/checkpoint.cpp
    src/tournament/journal_test.cpp
)
target_include_directories(tournament_journal_test PRIVATE src third_party/nlohmann/include)

if (OPENSSL_FOUND)
    add_executable(tournament_wire_test
        src/tournament/wire.cpp
        src/tournament/wire_crypto.cpp
        src/tournament/wire_test.cpp
    )
    target_include_directories(tournament_wire_test PRIVATE src)
    target_link_libraries(tournament_wire_test PRIVATE OpenSSL::Crypto)

    add_executable(tournament_registry_test
        src/tournament/registry.cpp
        src/tournament/registry_test.cpp
    )
    target_include_directories(tournament_registry_test PRIVATE src)
    target_link_libraries(tournament_registry_test PRIVATE Threads::Threads)

    add_executable(tournament_ban_file_test
        src/tournament/ban_file.cpp
        src/tournament/ban_file_test.cpp
    )
    target_include_directories(tournament_ban_file_test PRIVATE src)

    add_executable(tournament_loopback_test
        src/tournament/loopback.cpp
        src/tournament/loopback_test.cpp
    )
    target_include_directories(tournament_loopback_test PRIVATE src)
    target_link_libraries(tournament_loopback_test PRIVATE Threads::Threads)

    add_executable(tournament_audit_repair_test
        src/tournament/audit.cpp
        src/tournament/repair.cpp
        src/tournament/wire.cpp
        src/tournament/bracket.cpp
        src/tournament/runner.cpp
        src/tournament/scheduler.cpp
        src/tournament/audit_test.cpp
        src/tournament/repair_test.cpp
    )
    target_include_directories(tournament_audit_repair_test PRIVATE src)
    target_link_libraries(tournament_audit_repair_test PRIVATE Threads::Threads)

    add_executable(repro_probe
        src/repro_probe.cpp
        src/tournament/wire.cpp
        src/tournament/scheduler.cpp
        $<TARGET_OBJECTS:tournament_toj_objects>
    )
    target_include_directories(repro_probe PRIVATE src)
    target_link_libraries(repro_probe PRIVATE Threads::Threads)

    add_executable(repro_probe_test
        src/repro_probe_test.cpp
        src/tournament/wire.cpp
        src/tournament/scheduler.cpp
        $<TARGET_OBJECTS:tournament_toj_objects>
    )
    target_include_directories(repro_probe_test PRIVATE src)
    target_link_libraries(repro_probe_test PRIVATE Threads::Threads)

    add_executable(tournament_wire_frame_test
        src/tournament/wire.cpp
        src/tournament/wire_crypto.cpp
        src/tournament/wire_frame_test.cpp
    )
    target_include_directories(tournament_wire_frame_test PRIVATE src)
    target_link_libraries(tournament_wire_frame_test PRIVATE OpenSSL::Crypto)

    add_executable(tournament_net_transport_test
        src/tournament/net_transport.cpp
        src/tournament/net_transport_test.cpp
        src/tournament/remote_backend.cpp
        src/tournament/wire.cpp
        src/tournament/wire_crypto.cpp
        src/tournament/registry.cpp
        src/tournament/bracket.cpp
        src/tournament/runner.cpp
        src/tournament/scheduler.cpp
    )
    target_include_directories(tournament_net_transport_test PRIVATE src)
    target_link_libraries(tournament_net_transport_test PRIVATE Threads::Threads OpenSSL::Crypto OpenSSL::SSL)

    add_executable(remote_client
        src/remote_client.cpp
        src/tournament/net_transport.cpp
        src/tournament/registry.cpp
        src/tournament/wire.cpp
        src/tournament/wire_crypto.cpp
        src/tournament/scheduler.cpp
        $<TARGET_OBJECTS:tournament_toj_objects>
    )
    target_include_directories(remote_client PRIVATE src)
    target_link_libraries(remote_client PRIVATE Threads::Threads OpenSSL::Crypto OpenSSL::SSL)

    add_executable(tournament_host
        src/tournament_host.cpp
        src/tournament/ban_file.cpp
        src/tournament/net_transport.cpp
        src/tournament/registry.cpp
        src/tournament/remote_backend.cpp
        src/tournament/wire.cpp
        src/tournament/wire_crypto.cpp
        src/tournament/audit.cpp
        src/tournament/repair.cpp
        src/tournament/bracket.cpp
        src/tournament/runner.cpp
        src/tournament/scheduler.cpp
        $<TARGET_OBJECTS:tournament_toj_objects>
    )
    target_include_directories(tournament_host PRIVATE src)
    target_link_libraries(tournament_host PRIVATE Threads::Threads OpenSSL::Crypto OpenSSL::SSL)

    add_executable(tournament_remote_backend_test
        src/tournament/remote_backend.cpp
        src/tournament/wire.cpp
        src/tournament/wire_crypto.cpp
        src/tournament/loopback.cpp
        src/tournament/registry.cpp
        src/tournament/audit.cpp
        src/tournament/repair.cpp
        src/tournament/bracket.cpp
        src/tournament/runner.cpp
        src/tournament/scheduler.cpp
        $<TARGET_OBJECTS:tournament_toj_objects>
        src/tournament/remote_backend_test.cpp
    )
    target_include_directories(tournament_remote_backend_test PRIVATE src)
    target_link_libraries(tournament_remote_backend_test PRIVATE Threads::Threads OpenSSL::Crypto)
endif()

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
add_test(NAME tournament_journal_test COMMAND tournament_journal_test)

if (OPENSSL_FOUND)
    add_test(NAME tournament_wire_test COMMAND tournament_wire_test)
    add_test(NAME tournament_registry_test COMMAND tournament_registry_test)
    add_test(NAME tournament_ban_file_test COMMAND tournament_ban_file_test)
    add_test(NAME tournament_loopback_test COMMAND tournament_loopback_test)
    add_test(NAME tournament_audit_repair_test COMMAND tournament_audit_repair_test)
    add_test(NAME repro_probe_test COMMAND repro_probe_test)
    add_test(NAME tournament_remote_backend_test COMMAND tournament_remote_backend_test)
    add_test(NAME tournament_wire_frame_test COMMAND tournament_wire_frame_test)
    add_test(NAME tournament_net_transport_test COMMAND tournament_net_transport_test)
endif()
