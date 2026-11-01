cmake_minimum_required(VERSION 3.20)
project(xboxmusicradio VERSION 1.0.1 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(platform)   # fh2mod_platform (DWrite.dll) and fh2mod_api

add_library(xboxmusicradio SHARED src/xboxmusicradio.cpp src/library.cpp src/radio.cpp src/spotify.cpp src/apps.cpp src/capture.cpp src/decoder.c)
target_include_directories(xboxmusicradio PRIVATE third_party/dr_libs)
# dr_mp3 / dr_flac / dr_wav: third-party code, built without our warnings.
set_source_files_properties(src/decoder.c PROPERTIES COMPILE_OPTIONS "$<IF:$<BOOL:${MSVC}>,/W0,-w>")
target_link_libraries(xboxmusicradio PRIVATE fh2mod_api ole32 shell32 propsys uuid mmdevapi winhttp runtimeobject)
set_target_properties(xboxmusicradio PROPERTIES OUTPUT_NAME XboxMusicRadio PREFIX "")
target_compile_definitions(xboxmusicradio PRIVATE XBOXMUSICRADIO_VERSION="${PROJECT_VERSION}" UNICODE _UNICODE)

if(MSVC)
    target_compile_options(xboxmusicradio PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(xboxmusicradio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-missing-field-initializers>)
    target_link_options(xboxmusicradio PRIVATE -static)
endif()
