cmake_minimum_required(VERSION 3.10)
project(WebOsClient)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required libraries
find_package(Threads REQUIRED)

# Include directories for external libraries
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/lib)
include_directories(${PROJECT_SOURCE_DIR}/src)
# Add the main executable
add_executable(webos_client src/webos_client.cpp)

# Add JSON library (assuming you're using nlohmann/json)
# If nlohmann/json is installed via a package manager like vcpkg or system-wide, use this:
find_package(nlohmann_json REQUIRED)
target_link_libraries(webos_client PRIVATE nlohmann_json::nlohmann_json)

# Alternatively, if you are including the nlohmann/json.hpp directly:
# target_include_directories(webos_client PRIVATE ${PROJECT_SOURCE_DIR}/path/to/json)

# Add websocketpp
# If websocketpp is installed via a package manager like vcpkg or system-wide, use this:
find_package(websocketpp CONFIG REQUIRED)
target_link_libraries(webos_client PRIVATE websocketpp::websocketpp)

# Alternatively, if websocketpp is included directly in the project:
# target_include_directories(webos_client PRIVATE ${PROJECT_SOURCE_DIR}/path/to/websocketpp)

# Add Base64 library (cppcodec)
# If you have it as a header-only library:
target_include_directories(webos_client PRIVATE ${PROJECT_SOURCE_DIR}/lib/cppcodec)

# Link Boost Asio (if required for websocketpp)
# This assumes Boost is installed and `BOOST_ROOT` is set.
find_package(Boost REQUIRED COMPONENTS system)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    target_link_libraries(webos_client PRIVATE ${Boost_LIBRARIES})
endif()

# Link Threads
target_link_libraries(webos_client PRIVATE Threads::Threads)
