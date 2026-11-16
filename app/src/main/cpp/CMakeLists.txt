cmake_minimum_required(VERSION 3.22)
project(pocketchroma CXX)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core)
add_library(pocketchroma SHARED
    jni_bridge.cpp
    ${CORE}/axc51.cpp
    ${CORE}/spi_flash.cpp
    ${CORE}/system.cpp)
target_include_directories(pocketchroma PRIVATE ${CORE})
target_compile_options(pocketchroma PRIVATE -O3 -fno-exceptions -Wno-unused-but-set-variable)
target_link_libraries(pocketchroma android jnigraphics log)
