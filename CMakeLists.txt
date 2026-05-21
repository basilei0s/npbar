cmake_minimum_required(VERSION 3.20)
project(npbar LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "npbar is Windows-only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_executable(npbar
    src/main.cpp
    src/cli_options.cpp
    src/media_session_reader.cpp
    src/bar_renderer.cpp
    src/progress_bar.cpp
    src/colors.cpp
    src/text_util.cpp
    src/console_util.cpp
)

target_include_directories(npbar PRIVATE src)

if(MSVC)
    # MSVC build path: rely on the C++/WinRT projection that the
    # Microsoft.Windows.CppWinRT NuGet package (or Windows SDK) provides.
    # Users on MSVC may rewrite media_session_reader.cpp to use C++/WinRT
    # if they prefer; the raw-ABI version compiles unchanged.
    target_compile_options(npbar PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_definitions(npbar PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN NOMINMAX
    )
    set_property(TARGET npbar PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    # MinGW build path: use MinGW's own WinRT template headers
    # (<windows.foundation.h> + <windows.foundation.collections.h>) and
    # our hand-declared GSMTC interfaces in src/gsmtc_abi.h.
    target_compile_options(npbar PRIVATE
        -Wall -Wextra -Wno-unknown-pragmas
        -fms-extensions
        -municode
        $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables -flto>
    )
    target_compile_definitions(npbar PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
    # Default: single-file static build (~265 KB).
    # Set NPBAR_STATIC_STDLIB=OFF for a ~70 KB exe that requires
    # libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll alongside.
    option(NPBAR_STATIC_STDLIB "Statically link libstdc++ and libgcc into npbar.exe" ON)
    if(NPBAR_STATIC_STDLIB)
        target_link_options(npbar PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_link_options(npbar PRIVATE -municode)
    target_link_options(npbar PRIVATE
        "$<$<CONFIG:Release>:-Os>"
        "$<$<CONFIG:Release>:-Wl,-s>"
        "$<$<CONFIG:Release>:-Wl,--gc-sections>"
        "$<$<CONFIG:Release>:-flto>"
    )
endif()

target_link_libraries(npbar PRIVATE
    runtimeobject  # RoInitialize, RoGetActivationFactory, WindowsCreateString
)

set_target_properties(npbar PROPERTIES
    OUTPUT_NAME "npbar"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
