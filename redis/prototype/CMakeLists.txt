cmake_minimum_required(VERSION 3.16)
project(redis_study LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(study-store
  src/main.cpp
  src/common.cpp
  src/config.cpp
  src/protocol.cpp
  src/store.cpp
  src/server.cpp
  src/server_network.cpp
  src/server_commands.cpp
)
target_compile_options(study-store PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
option(SANITIZE "Enable address and undefined-behavior sanitizers" OFF)
if(SANITIZE)
  target_compile_options(study-store PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(study-store PRIVATE -fsanitize=address,undefined)
endif()
enable_testing()
add_executable(store-expiration-tests tests/store_expiration.cpp src/store.cpp)
target_include_directories(store-expiration-tests PRIVATE src)
target_compile_options(store-expiration-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
if(SANITIZE)
  target_compile_options(store-expiration-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(store-expiration-tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME store-expiration COMMAND store-expiration-tests)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME integration COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py $<TARGET_FILE:study-store>)
set_tests_properties(integration PROPERTIES TIMEOUT 60)
