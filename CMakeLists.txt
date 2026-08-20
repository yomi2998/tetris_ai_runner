cmake_minimum_required(VERSION 3.50)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

string(TOLOWER "${CMAKE_CXX_COMPILER}" _cxx_compiler_lower)

project(tetris_ai)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-flto)
    add_link_options(-flto)
endif()

add_library(${PROJECT_NAME} SHARED
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
target_include_directories(${PROJECT_NAME} PRIVATE src)
if (_cxx_compiler_lower MATCHES "mingw")
    target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)
endif()


project(tetris_ai_runner)
add_library(${PROJECT_NAME} SHARED
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
target_include_directories(${PROJECT_NAME} PRIVATE src)


project(top_pso)
add_library(${PROJECT_NAME} SHARED
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
    src/rule_srs.cpp
    src/search_tspin.cpp
    src/tetris_core.cpp
    src/ppt_pso.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)


project(match)
add_executable(${PROJECT_NAME}
    src/match.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_simple.cpp
    src/search_tspin.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)
if (_cxx_compiler_lower MATCHES "mingw")
    target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)
endif()


project(tetris_profile)
add_executable(${PROJECT_NAME}
    src/tetris_profile.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)


add_executable(heuristic_features_test
    src/heuristic_features_test.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(heuristic_features_test PRIVATE src)


project(tuner)
add_executable(${PROJECT_NAME}
    src/tuner.cpp
    src/tetris_core.cpp
    src/rule_toj.cpp
    src/search_tspin.cpp
    src/ai_zzz.cpp
    src/integer_utils.cpp
    src/random.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)
