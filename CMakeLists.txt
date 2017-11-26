cmake_minimum_required (VERSION 2.8)

# projectname is the same as the main-executable
project(hipercurl)

add_definitions('-g')
add_definitions('-Wall')

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR} ${CMAKE_MODULE_PATH})

find_package(CURL REQUIRED)
find_package(EV   REQUIRED)

add_executable(${PROJECT_NAME} main.c)
target_link_libraries(${PROJECT_NAME} 
  ${EV_LIBRARIES} 
  ${CURL_LIBRARIES})

set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES ${CMAKE_SOURCE_DIR}/${PROJECT_NAME})
