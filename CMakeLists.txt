cmake_minimum_required(VERSION 3.5) # FetchContent is available in 3.11+
project(findestory C)

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(GRAPHICS GRAPHICS_API_OPENGL_21)
# set(CMAKE_C_STANDARD 99)
# set(CMAKE_C_STANDARD_REQUIRED ON)

if (UNIX)
    add_compile_options(-Wall -Wno-unknown-pragmas -Wno-type-limits -std=gnu99)
endif (UNIX)

if(CMAKE_COMPILER_IS_GNUCXX)
    add_compile_options(-Wextra -Wpedantic)
endif (CMAKE_COMPILER_IS_GNUCXX)

# Dependencies
set(RAYLIB_VERSION 5.5)
find_package(raylib ${RAYLIB_VERSION} QUIET) # QUIET or REQUIRED
if (NOT raylib_FOUND) # If there's none, fetch and build raylib
  include(FetchContent)
  FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
  )
  FetchContent_GetProperties(raylib)
  if (NOT raylib_POPULATED) # Have we downloaded raylib yet?
    set(FETCHCONTENT_QUIET NO)
    FetchContent_MakeAvailable(raylib)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE) # don't build the supplied examples
  endif()
endif()

# Our Project

file(GLOB SOURCES
  *.h
  *.c
)

ADD_EXECUTABLE(${PROJECT_NAME}
${SOURCES}
)


target_link_libraries(${PROJECT_NAME} raylib rt pthread m)

# set RAYLIB_INCLUDE_PATH from command line
target_include_directories(${PROJECT_NAME} PUBLIC "${RAYLIB_INCLUDE_PATH}")

if(WIN32)
    target_link_libraries(${PROJECT_NAME} wsock32 ws2_32 opengl32 gdi32 winmm)
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC NBN_DEBUG)
target_compile_definitions(${PROJECT_NAME} PUBLIC NBN_RAYLIB_SERVER)

# compile with C WebRTC driver
if (WEBRTC_C_DRIVER)
  # can't compile WebRTC native driver with emscripten
  if (EMSCRIPTEN)
    message(SEND_ERROR "Can't compile WebRTC native driver with emscripten")
  endif (EMSCRIPTEN)

  message("Compiling with C WebRTC driver")

  target_compile_definitions(${PROJECT_NAME} PUBLIC SOAK_WEBRTC_C_DRIVER)

  target_link_libraries(${PROJECT_NAME} ${LIBFACILIO_LIBRARY_PATH})
  target_link_libraries(${PROJECT_NAME} ${LIBCRYPTO_LIBRARY_PATH})
  target_link_libraries(${PROJECT_NAME} ${LIBSSL_LIBRARY_PATH})
  target_link_libraries(${PROJECT_NAME} ${LIBDATACHANNEL_LIBRARY_PATH})

  target_include_directories(${PROJECT_NAME} PUBLIC "${LIBFACILIO_INCLUDE_PATH}")
  target_include_directories(${PROJECT_NAME} PUBLIC "${OPENSSL_INCLUDE_PATH}")
  target_include_directories(${PROJECT_NAME} PUBLIC "${LIBDATACHANNEL_INCLUDE_PATH}")

  if (USE_HTTPS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC NBN_HTTPS_SERVER_NAME="localhost")
    target_compile_definitions(${PROJECT_NAME} PUBLIC NBN_HTTPS_KEY_PEM="localhost.key")
    target_compile_definitions(${PROJECT_NAME} PUBLIC NBN_HTTPS_CERT_PEM="localhost.crt")
  endif (USE_HTTPS)
endif (WEBRTC_C_DRIVER)

unset(WEBRTC_C_DRIVER)

# Use HTTPS (for WebRTC drivers)
if (USE_HTTPS)
  message("Compiling with HTTPS enabled")

  target_compile_definitions(${PROJECT_NAME} PUBLIC NBN_USE_HTTPS)
endif (USE_HTTPS)

unset(USE_HTTPS)

if (EMSCRIPTEN)
    set(ASYNCIFY_IMPORTS "[\"__js_game_server_start\", \"__js_game_client_start\", \"__js_game_client_close\"]")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_MEMORY=30MB -s EXIT_RUNTIME=1 -s ASSERTIONS=1 -s ASYNCIFY -s ASYNCIFY_IMPORTS=\"${ASYNCIFY_IMPORTS}\"")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --js-library ${CMAKE_CURRENT_SOURCE_DIR}/../../net_drivers/webrtc/js/api.js")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_GLFW=3 --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND browserify ${CMAKE_CURRENT_SOURCE_DIR}/../../net_drivers/webrtc/js/nbnet.js -o nbnet_bundle.js)
endif()

add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_BINARY_DIR}/src
)

if (APPLE)
  target_link_libraries(${PROJECT_NAME} "-framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -framework CoreVideo")
endif (APPLE)