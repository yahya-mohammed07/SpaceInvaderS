cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(space_invaders LANGUAGES CXX)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

find_package(GLEW REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(${PROJECT_NAME}
  main.cpp
)

target_link_libraries(space_invaders PRIVATE GLEW::GLEW)
target_link_libraries(space_invaders PRIVATE fmt::fmt)
target_link_libraries(space_invaders PRIVATE OpenGL::GL)
target_link_libraries(space_invaders PRIVATE glfw)