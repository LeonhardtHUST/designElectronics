cmake_minimum_required(VERSION 3.20)
project(designElectronics VERSION 0.8.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL3 + SDL3_ttf: use installed packages, or vendor them with
#   cmake -DDE_VENDOR_SDL=ON   (expects external/SDL and external/SDL_ttf checkouts)
option(DE_VENDOR_SDL "Build SDL3/SDL3_ttf from external/ sources" OFF)
if(DE_VENDOR_SDL OR ANDROID OR IOS OR EMSCRIPTEN)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  add_subdirectory(external/SDL EXCLUDE_FROM_ALL)
  set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(external/SDL_ttf EXCLUDE_FROM_ALL)
else()
  find_package(SDL3 REQUIRED CONFIG)
  find_package(SDL3_ttf REQUIRED CONFIG)
endif()

set(DE_SOURCES
  src/main.cpp
  src/app.cpp
  src/canvas.cpp
  src/canvas_anno.cpp
  src/panels.cpp
  src/ui.cpp
  src/gfx.cpp
  src/json.cpp
  src/i18n.cpp
  src/settings.cpp
  src/keys.cpp
  src/panels_settings.cpp
  src/toml.cpp
  src/partlib.cpp
  src/catalog.cpp
  src/packages.cpp
  src/doc.cpp
  src/anno.cpp
  src/router.cpp
  src/tidy.cpp
  src/drc.cpp
  src/sim.cpp
  src/arduino.cpp
  src/export.cpp
  src/spice_tests.cpp
  src/examples.cpp
  src/mcp.cpp
  src/logo.cpp
  src/history.cpp
  src/app_history.cpp
  src/panels_layers.cpp
  src/panels_tools.cpp
  src/colorcode.cpp
  src/web.cpp
  src/code_editor.cpp
  src/panels_code.cpp
  src/upload.cpp
  src/video.cpp
  src/panels_video.cpp
  src/codec_h264.c
  src/codec_mp4.c
  src/net.cpp
  src/agent.cpp
  src/panels_ai.cpp
  src/partdata.cpp
  src/partmodels.cpp
  src/image.cpp
  src/image_impl.cpp
)
if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  list(APPEND DE_SOURCES src/ngspice_backend.cpp src/ngspice_tests.cpp src/spice_xcheck.cpp)
endif()

# Parts library data (parts/*.txt), compiled in.
file(GLOB DE_PART_DATA CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/parts/*.txt)
list(SORT DE_PART_DATA)
set(DE_PART_BLOB ${CMAKE_CURRENT_BINARY_DIR}/partdata_blob.c)
add_custom_command(
  OUTPUT ${DE_PART_BLOB}
  COMMAND ${CMAKE_COMMAND} "-DOUT=${DE_PART_BLOB}" "-DIN=${DE_PART_DATA}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  DEPENDS ${DE_PART_DATA} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  COMMENT "Embedding parts library"
  VERBATIM)
list(APPEND DE_SOURCES ${DE_PART_BLOB})

# Translation packs (i18n/*.json), compiled in for the same reason: the web and
# mobile builds then carry every language without extra packaging. A copy in
# i18n/ next to the executable can still override a pack at runtime.
file(GLOB DE_I18N CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/i18n/*.json)
list(SORT DE_I18N)
set(DE_I18N_BLOB ${CMAKE_CURRENT_BINARY_DIR}/i18n_blob.c)
add_custom_command(
  OUTPUT ${DE_I18N_BLOB}
  COMMAND ${CMAKE_COMMAND} "-DOUT=${DE_I18N_BLOB}" "-DIN=${DE_I18N}" "-DSYMBOL=de_i18n"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  DEPENDS ${DE_I18N} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  COMMENT "Embedding translations"
  VERBATIM)
list(APPEND DE_SOURCES ${DE_I18N_BLOB})

set(DE_NOTICE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
  ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
  ${CMAKE_CURRENT_SOURCE_DIR}/CREDITS.md)
set(DE_LICENSE_BLOB ${CMAKE_CURRENT_BINARY_DIR}/licenses_blob.c)
add_custom_command(
  OUTPUT ${DE_LICENSE_BLOB}
  COMMAND ${CMAKE_COMMAND} "-DOUT=${DE_LICENSE_BLOB}" "-DIN=${DE_NOTICE_FILES}" "-DSYMBOL=de_licenses"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  DEPENDS ${DE_NOTICE_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  COMMENT "Embedding open-source license notices"
  VERBATIM)
list(APPEND DE_SOURCES src/licenses.cpp ${DE_LICENSE_BLOB})

# ---------------------------------------------------------------------------
# WebP decoding. The vendored libwebp (1.5.0, BSD, see third_party/libwebp/COPYING)
# ships as a small static library holding only the decoder. The SSE2/SSE4.1 and NEON
# sources are plain C intrinsics guarded by WEBP_USE_*: each compiles to nothing on the
# other architectures (and on Emscripten), and arm64 (Apple silicon, Android) needs the
# NEON ones because libwebp omits the plain C versions there. The other picture
# formats come from stb_image (PNG/JPEG/GIF/BMP/PSD, third_party/stb) and nanosvg
# (SVG, third_party/nanosvg), both expanded in src/image_impl.cpp.
# ---------------------------------------------------------------------------
set(DE_WEBP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libwebp)
set(DE_WEBP_SOURCES
  ${DE_WEBP_DIR}/src/dec/alpha_dec.c
  ${DE_WEBP_DIR}/src/dec/buffer_dec.c
  ${DE_WEBP_DIR}/src/dec/frame_dec.c
  ${DE_WEBP_DIR}/src/dec/idec_dec.c
  ${DE_WEBP_DIR}/src/dec/io_dec.c
  ${DE_WEBP_DIR}/src/dec/quant_dec.c
  ${DE_WEBP_DIR}/src/dec/tree_dec.c
  ${DE_WEBP_DIR}/src/dec/vp8_dec.c
  ${DE_WEBP_DIR}/src/dec/vp8l_dec.c
  ${DE_WEBP_DIR}/src/dec/webp_dec.c
  ${DE_WEBP_DIR}/src/dsp/alpha_processing.c
  ${DE_WEBP_DIR}/src/dsp/alpha_processing_neon.c
  ${DE_WEBP_DIR}/src/dsp/alpha_processing_sse2.c
  ${DE_WEBP_DIR}/src/dsp/alpha_processing_sse41.c
  ${DE_WEBP_DIR}/src/dsp/cpu.c
  ${DE_WEBP_DIR}/src/dsp/dec.c
  ${DE_WEBP_DIR}/src/dsp/dec_neon.c
  ${DE_WEBP_DIR}/src/dsp/dec_clip_tables.c
  ${DE_WEBP_DIR}/src/dsp/dec_sse2.c
  ${DE_WEBP_DIR}/src/dsp/dec_sse41.c
  ${DE_WEBP_DIR}/src/dsp/filters.c
  ${DE_WEBP_DIR}/src/dsp/filters_neon.c
  ${DE_WEBP_DIR}/src/dsp/filters_sse2.c
  ${DE_WEBP_DIR}/src/dsp/lossless.c
  ${DE_WEBP_DIR}/src/dsp/lossless_neon.c
  ${DE_WEBP_DIR}/src/dsp/lossless_sse2.c
  ${DE_WEBP_DIR}/src/dsp/lossless_sse41.c
  ${DE_WEBP_DIR}/src/dsp/rescaler.c
  ${DE_WEBP_DIR}/src/dsp/rescaler_neon.c
  ${DE_WEBP_DIR}/src/dsp/rescaler_sse2.c
  ${DE_WEBP_DIR}/src/dsp/upsampling.c
  ${DE_WEBP_DIR}/src/dsp/upsampling_neon.c
  ${DE_WEBP_DIR}/src/dsp/upsampling_sse2.c
  ${DE_WEBP_DIR}/src/dsp/upsampling_sse41.c
  ${DE_WEBP_DIR}/src/dsp/yuv.c
  ${DE_WEBP_DIR}/src/dsp/yuv_neon.c
  ${DE_WEBP_DIR}/src/dsp/yuv_sse2.c
  ${DE_WEBP_DIR}/src/dsp/yuv_sse41.c
  ${DE_WEBP_DIR}/src/utils/bit_reader_utils.c
  ${DE_WEBP_DIR}/src/utils/color_cache_utils.c
  ${DE_WEBP_DIR}/src/utils/filters_utils.c
  ${DE_WEBP_DIR}/src/utils/huffman_utils.c
  ${DE_WEBP_DIR}/src/utils/palette.c
  ${DE_WEBP_DIR}/src/utils/quant_levels_dec_utils.c
  ${DE_WEBP_DIR}/src/utils/random_utils.c
  ${DE_WEBP_DIR}/src/utils/rescaler_utils.c
  ${DE_WEBP_DIR}/src/utils/thread_utils.c
  ${DE_WEBP_DIR}/src/utils/utils.c)
add_library(de_webp STATIC ${DE_WEBP_SOURCES})
target_include_directories(de_webp PUBLIC ${DE_WEBP_DIR} ${DE_WEBP_DIR}/src)
if(MSVC)
  target_compile_options(de_webp PRIVATE /W0)
else()
  target_compile_options(de_webp PRIVATE -w)
endif()

if(ANDROID)
  add_library(main SHARED ${DE_SOURCES})
  set(DE_TARGET main)
else()
  add_executable(designElectronics MACOSX_BUNDLE WIN32 ${DE_SOURCES})
  set(DE_TARGET designElectronics)
endif()

target_include_directories(${DE_TARGET} PRIVATE src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg)

# The vendored single-header libraries are third-party code: their warnings are
# not ours to chase.
if(MSVC)
  set_source_files_properties(src/image_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
endif()

# HTTPS for GitHub sync (desktop). Without it, local version history still works.
if(NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    target_link_libraries(${DE_TARGET} PRIVATE CURL::libcurl)
    target_compile_definitions(${DE_TARGET} PRIVATE DE_HAVE_CURL=1)
  else()
    message(STATUS "libcurl not found: GitHub sync disabled (local history still works)")
  endif()
endif()
target_link_libraries(${DE_TARGET} PRIVATE SDL3_ttf::SDL3_ttf SDL3::SDL3 de_webp)
if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  target_link_libraries(${DE_TARGET} PRIVATE ${CMAKE_DL_LIBS})
endif()

# Windows: SDL is linked as a DLL, so copy the runtime next to the executable.
if(WIN32 AND NOT DE_VENDOR_SDL)
  add_custom_command(TARGET ${DE_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:SDL3::SDL3>
      $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
      $<TARGET_FILE_DIR:${DE_TARGET}>
    COMMENT "Copying SDL3 runtime DLLs")
endif()

if(MSVC)
  target_compile_options(${DE_TARGET} PRIVATE /W3 /utf-8 /D_USE_MATH_DEFINES)
else()
  target_compile_options(${DE_TARGET} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Copy fonts next to the binary (desktop) / into the bundle (macOS, iOS).
set(DE_FONTS
  ${CMAKE_SOURCE_DIR}/assets/fonts/SarasaGothicSC-Regular.ttf
  ${CMAKE_SOURCE_DIR}/assets/fonts/JetBrainsMapleMono-Regular.ttf
  ${CMAKE_SOURCE_DIR}/assets/fonts/OFL-SarasaGothic.txt
  ${CMAKE_SOURCE_DIR}/assets/fonts/OFL-JetBrainsMapleMono.txt)

if(APPLE)
  target_sources(${DE_TARGET} PRIVATE ${DE_FONTS})
  set_source_files_properties(${DE_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/assets/fonts)
  target_sources(${DE_TARGET} PRIVATE ${DE_NOTICE_FILES})
  set_source_files_properties(${DE_NOTICE_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  # App icon (regenerate with --export-logo + iconutil, see docs/BUILDING.md)
  target_sources(${DE_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/platform/apple/AppIcon.icns)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/platform/apple/AppIcon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(${DE_TARGET} PROPERTIES
    MACOSX_BUNDLE_ICON_FILE AppIcon
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/platform/apple/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER org.designelectronics.app
    MACOSX_BUNDLE_BUNDLE_NAME designElectronics
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
elseif(EMSCRIPTEN)
  # Web build: designElectronics.js/.wasm/.data, started by docs/app and docs/embed pages.
  # INVOKE_RUN=0: the page writes the design into the virtual FS first, then calls callMain(args).
  target_link_options(${DE_TARGET} PRIVATE
    -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sINVOKE_RUN=0 -sEXIT_RUNTIME=0
    "-sEXPORTED_RUNTIME_METHODS=['FS','ccall','callMain']"
    "-sEXPORTED_FUNCTIONS=['_main','_de_web_opened','_de_web_opened_image']"
    "--preload-file=${CMAKE_SOURCE_DIR}/assets/fonts@/assets/fonts")
elseif(NOT ANDROID)
  add_custom_command(TARGET ${DE_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${DE_TARGET}>/assets/fonts
    COMMAND ${CMAKE_COMMAND} -E copy ${DE_FONTS} $<TARGET_FILE_DIR:${DE_TARGET}>/assets/fonts/)
  install(TARGETS ${DE_TARGET} RUNTIME DESTINATION bin)
  install(FILES ${DE_FONTS} DESTINATION bin/assets/fonts)
  add_custom_command(TARGET ${DE_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DE_NOTICE_FILES} $<TARGET_FILE_DIR:${DE_TARGET}>/)
  install(FILES ${DE_NOTICE_FILES} DESTINATION bin)
  if(UNIX)
    install(FILES platform/linux/designelectronics.desktop DESTINATION share/applications)
    install(FILES platform/linux/designelectronics-mime.xml DESTINATION share/mime/packages)
    install(FILES assets/icon/icon_256.png DESTINATION share/icons/hicolor/256x256/apps RENAME designelectronics.png)
    install(FILES assets/icon/logo.svg DESTINATION share/icons/hicolor/scalable/apps RENAME designelectronics.svg)
  endif()
endif()
