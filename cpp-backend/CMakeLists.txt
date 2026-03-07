cmake_minimum_required(VERSION 3.10)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(websocketpp REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(CURL REQUIRED)

include_directories(
    ${Boost_INCLUDE_DIRS}
)

add_executable(chat_server
    main.cpp
)

target_link_libraries(chat_server
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    jsoncpp_lib
    pthread
    ${CURL_LIBRARIES}
)

target_include_directories(chat_server PRIVATE
    ${websocketpp_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)
