cmake_minimum_required(VERSION 3.15)

if(APPLE)
    # Build Universal 2 binary (Apple Silicon arm64 + Intel x86_64) by default for macOS Resolume
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version" FORCE)
    endif()
endif()

project(ResolumeBrowserSource VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

set(FFGL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl")

# Gather FFGL SDK core + ffglex sources
set(FFGL_SDK_SOURCES
    "${FFGL_SDK_DIR}/source/lib/ffgl/FFGL.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffgl/FFGLLog.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffgl/FFGLPluginInfo.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffgl/FFGLPluginInfoData.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffgl/FFGLPluginManager.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffgl/FFGLPluginSDK.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffgl/FFGLThumbnailInfo.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLFBO.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScopedBufferBinding.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScopedFBOBinding.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScopedRenderBufferBinding.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScopedSamplerActivation.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScopedShaderBinding.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScopedTextureBinding.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScopedVAOBinding.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLScreenQuad.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLShader.cpp"
    "${FFGL_SDK_DIR}/source/lib/ffglex/FFGLUtilities.cpp"
)

set(PLUGIN_SOURCES
    src/FFGLBrowserSource.h
    src/FFGLBrowserSource.cpp
    src/IBrowserBackend.h
)

if(WIN32)
    list(APPEND PLUGIN_SOURCES
        src/BrowserBackendWin.h
        src/BrowserBackendWin.cpp
        "${FFGL_SDK_DIR}/build/windows/FFGLPlugins.def"
    )
elseif(APPLE)
    list(APPEND PLUGIN_SOURCES
        src/BrowserBackendMac.h
        src/BrowserBackendMac.mm
    )
endif()

add_library(BrowserSource MODULE
    ${FFGL_SDK_SOURCES}
    ${PLUGIN_SOURCES}
)

target_include_directories(BrowserSource PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${FFGL_SDK_DIR}/source/lib"
)

if(WIN32)
    set(GLEW_DIR "${FFGL_SDK_DIR}/deps/glew-2.1.0")
    set(WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/webview2/build/native")

    target_include_directories(BrowserSource PRIVATE
        "${GLEW_DIR}/include"
        "${WEBVIEW2_DIR}/include"
    )

    target_compile_definitions(BrowserSource PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        GLEW_STATIC
        _CRT_SECURE_NO_WARNINGS
    )

    target_link_libraries(BrowserSource PRIVATE
        "${GLEW_DIR}/lib/Release/x64/glew32s.lib"
        "${WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
        opengl32
        gdi32
        user32
        ole32
        oleaut32
        shlwapi
        version
        advapi32
        shell32
        windowscodecs
        winmm
    )

    add_executable(test_host tests/test_host.cpp)
    target_include_directories(test_host PRIVATE
        "${FFGL_SDK_DIR}/source/lib"
        "${GLEW_DIR}/include"
    )
    target_compile_definitions(test_host PRIVATE WIN32 GLEW_STATIC)
    target_link_libraries(test_host PRIVATE
        "${GLEW_DIR}/lib/Release/x64/glew32s.lib"
        opengl32
        gdi32
        user32
    )
elseif(APPLE)
    target_compile_definitions(BrowserSource PRIVATE
        TARGET_OS_MAC=1
        GL_SILENCE_DEPRECATION=1
    )

    set_target_properties(BrowserSource PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "BrowserSource"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.resolume.ffgl.browsersource"
    )

    target_link_libraries(BrowserSource PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework WebKit"
        "-framework CoreGraphics"
        "-framework QuartzCore"
        "-framework Carbon"
    )

    # Ad-hoc code sign the bundle automatically (required on Apple Silicon / macOS Gatekeeper)
    add_custom_command(TARGET BrowserSource POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:BrowserSource>"
        COMMENT "Ad-hoc code signing BrowserSource.bundle for macOS Resolume"
    )

    add_executable(test_host tests/test_host_mac.mm)
    target_include_directories(test_host PRIVATE
        "${FFGL_SDK_DIR}/source/lib"
    )
    target_compile_definitions(test_host PRIVATE
        TARGET_OS_MAC=1
        GL_SILENCE_DEPRECATION=1
    )
    target_link_libraries(test_host PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework CoreFoundation"
    )
endif()
