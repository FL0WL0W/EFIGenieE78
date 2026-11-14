cmake_minimum_required(VERSION 4.0)

project ( EFIGenieE78 C CXX ASM )

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBRARIES_DIR	    ${CMAKE_SOURCE_DIR}/libs)

include(FetchContent)
include(cmake/Runtime.cmake)
link_libraries(e200_runtime)

FetchContent_Declare(
    EFIGenie
    GIT_REPOSITORY https://github.com/FL0WL0W/EFIGenie
    GIT_TAG        main
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
    SOURCE_DIR  ${LIBRARIES_DIR}/EFIGenie
)

FetchContent_MakeAvailable(EFIGenie)
FetchContent_GetProperties(EmbeddedIOServices)
set(EmbeddedIOServicesHardwareAbstraction_Path "${embeddedioservices_SOURCE_DIR}/HardwareAbstractions/MPC5xxx")

include_directories(include)
include_directories(${EmbeddedIOServicesHardwareAbstraction_Path}/include)

file(GLOB_RECURSE USER_CSOURCES src/*.c)
file(GLOB_RECURSE USER_CSOURCES_Templates src/*_template.c)
IF(USER_CSOURCES_Templates)
        list(REMOVE_ITEM USER_CSOURCES ${USER_CSOURCES_Templates})
ENDIF()
file(GLOB_RECURSE USER_CXXSOURCES src/*.cpp)
file(GLOB_RECURSE USER_CXXSOURCES_Templates src/*_template.cpp)
IF(USER_CXXSOURCES_Templates)
        list(REMOVE_ITEM USER_CXXSOURCES ${USER_CXXSOURCES_Templates})
ENDIF()
file(GLOB_RECURSE EmbeddedIOService_SOURCES ${EmbeddedIOServicesHardwareAbstraction_Path}/src/*.cpp)
file(GLOB_RECURSE EmbeddedIOService_SOURCES_Templates ${EmbeddedIOServicesHardwareAbstraction_Path}/src/*_template.cpp)
IF(EmbeddedIOService_SOURCES_Templates)
        list(REMOVE_ITEM EmbeddedIOService_SOURCES ${EmbeddedIOService_SOURCES_Templates})
ENDIF()

set(MPC5566_STARTUP_SOURCE "${CMAKE_SOURCE_DIR}/startup_mpc5566_flash.S")
set(MPC5566_INTERRUPT_SOURCE "${CMAKE_SOURCE_DIR}/interrupt_mpc5566.S")
set(MPC5566_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/MPC5566_FLASH.ld")
set(MPC5566_IMAGE_BASENAME "EFIGenie-E78")
set(MPC5566_BOOTLOADER_IMAGE
    "${CMAKE_SOURCE_DIR}/bootloader.bin"
    CACHE FILEPATH "Unmodified stock bootloader used as the patch input")

set(BOOTLOADER_HEADER_PATCH_SOURCE
    ${CMAKE_SOURCE_DIR}/tools/bootloader_application_header_patch.S)
set(BOOTLOADER_HEADER_PATCH_SCRIPT
    ${CMAKE_SOURCE_DIR}/tools/patch_bootloader_application_header.sh)
set(BOOTLOADER_HEADER_PATCH_BINARY
    ${CMAKE_BINARY_DIR}/bootloader_application_header_patch.bin)
set(PATCHED_BOOTLOADER_IMAGE
    ${CMAKE_CURRENT_BINARY_DIR}/${MPC5566_IMAGE_BASENAME}-Bootloader.bin)

find_program(BASH_EXECUTABLE bash REQUIRED)
add_library(bootloader_header_patch OBJECT ${BOOTLOADER_HEADER_PATCH_SOURCE})
add_custom_command(
    OUTPUT ${PATCHED_BOOTLOADER_IMAGE}
    COMMAND ${CMAKE_OBJCOPY} -O binary -j .text
        $<TARGET_OBJECTS:bootloader_header_patch>
        ${BOOTLOADER_HEADER_PATCH_BINARY}
    COMMAND ${BASH_EXECUTABLE}
        ${BOOTLOADER_HEADER_PATCH_SCRIPT}
        ${MPC5566_BOOTLOADER_IMAGE}
        ${BOOTLOADER_HEADER_PATCH_BINARY}
        ${PATCHED_BOOTLOADER_IMAGE}
    DEPENDS
        bootloader_header_patch
        $<TARGET_OBJECTS:bootloader_header_patch>
        ${MPC5566_BOOTLOADER_IMAGE}
        ${BOOTLOADER_HEADER_PATCH_SOURCE}
        ${BOOTLOADER_HEADER_PATCH_SCRIPT}
    COMMENT "Patching the stock E78 bootloader for the compact application header")
add_custom_target(patched_bootloader ALL DEPENDS ${PATCHED_BOOTLOADER_IMAGE})

set(SRC_FILES ${USER_CSOURCES} ${USER_CXXSOURCES} ${EmbeddedIOService_SOURCES}
    ${MPC5566_STARTUP_SOURCE}
    ${MPC5566_INTERRUPT_SOURCE})
add_executable(firmware.elf ${SRC_FILES})
add_dependencies(firmware.elf patched_bootloader)
set_property(TARGET firmware.elf PROPERTY LINK_DEPENDS ${MPC5566_LINKER_SCRIPT})

target_compile_definitions(firmware.elf PRIVATE MPC5566)
target_link_libraries(firmware.elf EFIGenie)
target_link_options(firmware.elf PRIVATE
    "LINKER:--wrap=malloc"
    "LINKER:--wrap=free"
    "LINKER:--wrap=calloc"
    "LINKER:--wrap=realloc")

set(HEX_FILE ${CMAKE_CURRENT_BINARY_DIR}/${MPC5566_IMAGE_BASENAME}.hex)
set(BIN_FILE ${CMAKE_CURRENT_BINARY_DIR}/${MPC5566_IMAGE_BASENAME}.bin)
add_custom_command(TARGET firmware.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex $<TARGET_FILE:firmware.elf> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} ARGS -O binary --gap-fill 0xFF $<TARGET_FILE:firmware.elf> ${BIN_FILE}
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware.elf>
        BYPRODUCTS ${HEX_FILE} ${BIN_FILE}
        VERBATIM)

set(FULL_FLASH_IMAGE_SCRIPT ${CMAKE_SOURCE_DIR}/tools/build_full_flash_image.sh)
set_property(TARGET firmware.elf APPEND PROPERTY LINK_DEPENDS
    ${PATCHED_BOOTLOADER_IMAGE}
    ${FULL_FLASH_IMAGE_SCRIPT})
set(FULL_FLASH_BIN_FILE
    ${CMAKE_CURRENT_BINARY_DIR}/${MPC5566_IMAGE_BASENAME}-Full.bin)
add_custom_command(TARGET firmware.elf POST_BUILD
    COMMAND ${BASH_EXECUTABLE}
        ${FULL_FLASH_IMAGE_SCRIPT}
        ${PATCHED_BOOTLOADER_IMAGE}
        ${BIN_FILE}
        ${FULL_FLASH_BIN_FILE}
        0x80000
        0x300000
    COMMENT "Building complete 3 MiB flash image ${FULL_FLASH_BIN_FILE}")
