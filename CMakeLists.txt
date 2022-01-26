cmake_minimum_required( VERSION 3.14 )

cmake_policy(SET CMP0092 NEW) # default warning /W3 is disabled as we are using /W4

project(hana23 VERSION 0.1 LANGUAGES CXX)

include(CTest)

add_subdirectory(include)

if(BUILD_TESTING)
    add_executable(hana-test test.cpp)
    target_link_libraries(hana-test PRIVATE hana23)
endif()