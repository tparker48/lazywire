cmake_minimum_required(VERSION 3.15)
project(lazywire VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies via FetchContent
include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG        v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# libssh (must be installed on system: apt install libssh-dev)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH REQUIRED libssh)

# Source files
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/pane.cpp
    src/local_fs.cpp
    src/remote_fs.cpp
    src/ssh_session.cpp
    src/file_ops.cpp
    src/fuzzy.cpp
)

add_executable(lazywire ${SOURCES})

target_include_directories(lazywire PRIVATE
    include
    ${LIBSSH_INCLUDE_DIRS}
)

target_link_libraries(lazywire PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    ${LIBSSH_LIBRARIES}
)

target_compile_options(lazywire PRIVATE ${LIBSSH_CFLAGS_OTHER})
