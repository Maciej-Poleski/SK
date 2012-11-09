cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(http-server)

set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.51.0 REQUIRED date_time system filesystem)

include_directories(${Boost_INCLUDE_DIRS})

add_executable(serv main.cxx)

set_target_properties(serv PROPERTIES COMPILE_FLAGS "-std=c++0x")

target_link_libraries(serv ${Boost_LIBRARIES})

install(TARGETS serv RUNTIME DESTINATION bin)
