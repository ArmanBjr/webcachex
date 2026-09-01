cmake_minimum_required(VERSION 3.16)
project(WebCacheX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Good warnings for coursework builds
add_compile_options(-Wall -Wextra -Wpedantic)

# =========================
# web_server
# =========================
add_executable(web_server_bin
    web_server/src/main.cpp
)

target_include_directories(web_server_bin PRIVATE
    web_server/include
)

# pthread for Linux/WSL
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(web_server_bin PRIVATE Threads::Threads)

# =========================
# cache_server
# =========================
add_executable(cache_server_bin
    cache_server/src/main.cpp
    cache_server/src/logger.cpp
)

target_include_directories(cache_server_bin PRIVATE
    cache_server/include
)

target_link_libraries(cache_server_bin PRIVATE Threads::Threads)

# =========================
# Output binaries into build/ (optional but nice)
# =========================
set_target_properties(web_server_bin cache_server_bin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
