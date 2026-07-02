cmake_minimum_required(VERSION 3.15)
project(MockCQGServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# FetchContent: QuickFIX + nlohmann/json
# ---------------------------------------------------------------------------
include(FetchContent)

# QuickFIX — disable optional dependencies and examples
set(HAVE_SSL OFF CACHE BOOL "" FORCE)
set(HAVE_MYSQL OFF CACHE BOOL "" FORCE)
set(HAVE_POSTGRESQL OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    quickfix
    GIT_REPOSITORY https://github.com/quickfix/quickfix.git
    GIT_TAG        master
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# Fetch CURL
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_4_0
)

FetchContent_MakeAvailable(quickfix nlohmann_json curl)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/MockCQGApplication.cpp
    src/OrderManager.cpp
    src/MatchingEngine.cpp
    src/PriceFetcher.cpp
    src/TradeLogger.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(${PROJECT_NAME} PRIVATE
    quickfix
    libcurl
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Post-build: copy FIX42.xml data dictionary and server.cfg into build dir
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/spec"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${quickfix_SOURCE_DIR}/spec/FIX42.xml"
        "${CMAKE_BINARY_DIR}/spec/FIX42.xml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/server.cfg"
        "${CMAKE_BINARY_DIR}/server.cfg"
    COMMENT "Copying FIX42.xml data dictionary and server.cfg to build directory"
)
