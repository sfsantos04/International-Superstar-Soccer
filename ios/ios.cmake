# ios/ios.cmake - included at the end of CMakeLists.txt when the target is iOS.
#
# Two things change versus the desktop builds:
#   * ISSDNative/main.c is compiled with -Dmain=issd_main, so the binary does
#     not define a C main(); UIKit owns the process entry and SDL hands it to
#     SDL_main, which lives in ios/main_ios.m.
#   * libSDL2main.a must be linked, otherwise UIApplicationMain is never called
#     and SDL_Init(SDL_INIT_VIDEO) hangs with no window.

set(ISSD_MAIN_C "${CMAKE_CURRENT_SOURCE_DIR}/ISSDNative/main.c")
set(ISSD_MAIN_IOS "${CMAKE_CURRENT_SOURCE_DIR}/ios/main_ios.m")

set_source_files_properties("${ISSD_MAIN_C}" PROPERTIES
    COMPILE_DEFINITIONS "main=issd_main")

target_sources(ISSDNative PRIVATE "${ISSD_MAIN_IOS}")
set_source_files_properties("${ISSD_MAIN_IOS}" PROPERTIES
    COMPILE_FLAGS "-fobjc-arc -fobjc-abi-version=2")

# Static SDL2 needs its entry shim as a separate archive.
if(EXISTS "${SDL2_LIBRARY_DIR}/libSDL2main.a")
    target_link_libraries(ISSDNative PRIVATE "${SDL2_LIBRARY_DIR}/libSDL2main.a")
elseif(EXISTS "${CMAKE_PREFIX_PATH}/lib/libSDL2main.a")
    target_link_libraries(ISSDNative PRIVATE "${CMAKE_PREFIX_PATH}/lib/libSDL2main.a")
else()
    message(WARNING "libSDL2main.a nao encontrado - o app vai subir sem entry point")
endif()

target_link_libraries(ISSDNative PRIVATE
    "-framework Foundation"
    "-framework UIKit"
    "-framework QuartzCore"
    "-framework CoreGraphics"
    "-framework OpenGLES"
    "-framework AVFoundation"
    "-framework AudioToolbox"
    "-framework CoreAudio"
    "-framework CoreVideo"
    "-framework CoreMotion"
    "-framework GameController"
    "-framework CoreHaptics"
    "-framework Metal"
    "-framework CoreBluetooth"
    "iconv"
    "z"
)

set_target_properties(ISSDNative PROPERTIES
    MACOSX_BUNDLE FALSE
    SUFFIX "")
