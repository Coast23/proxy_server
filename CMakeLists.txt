cmake_minimum_required(VERSION 3.12)
project(proxy_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(PLATFORM_LIBS ws2_32 mswsock)
else()
    set(PLATFORM_LIBS pthread)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
endif()

add_executable(proxy_server
    src/main.cpp
    src/session.cpp
    src/io.cpp
)

target_include_directories(proxy_server PRIVATE src)
target_link_libraries(proxy_server PRIVATE ${PLATFORM_LIBS})
