add_library(hana23 INTERFACE)

target_sources(hana23 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/hana23/move_only_function.hpp)

target_compile_features(hana23 INTERFACE cxx_std_20)
target_include_directories(hana23 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(hana23)