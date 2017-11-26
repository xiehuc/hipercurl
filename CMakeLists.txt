cmake_minimum_required (VERSION 2.8)

# projectname is the same as the main-executable
project(hipercurl)

set(CMAKE_EXPORT_COMPILE_COMMANDS, 1)
add_definitions('-g')
add_definitions('-Wall')

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR} ${CMAKE_MODULE_PATH})

find_package(CURL REQUIRED)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
find_package(EV   REQUIRED)

include_directories(curl/src/ ./ ${CURL_INCLUDE_DIRS} ${EV_INCLUDE_DIRS})

add_executable(${PROJECT_NAME} main.c hiperfifo.c)
target_link_libraries(${PROJECT_NAME} 
  ${EV_LIBRARIES} 
  ${CURL_LIBRARIES})

set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES ${CMAKE_SOURCE_DIR}/${PROJECT_NAME})
