cmake_minimum_required(VERSION 3.25)
project(iOSAudioBridge VERSION 0.1.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows x64 is required")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/W4 /permissive- /utf-8 /EHsc)
set(CORE "${CMAKE_CURRENT_SOURCE_DIR}/src/AudioCore/src")
set(USB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb-win32")
add_library(AudioCoreImpl STATIC
  ${CORE}/Protocol/QuickTimePacket.cpp ${CORE}/Protocol/QuickTimeSession.cpp ${CORE}/Protocol/Plist.cpp
  ${CORE}/Media/CoreMedia.cpp ${CORE}/Transport/Socket.cpp ${CORE}/Transport/UsbMuxClient.cpp
  ${CORE}/Transport/LibUsb0Transport.cpp ${CORE}/Device/AppleUsbDiscovery.cpp
  ${CORE}/Logging.cpp ${CORE}/Device/DeviceCatalog.cpp
  ${CORE}/Device/UsbDeviceRepair.cpp ${CORE}/Device/UsbRepairClient.cpp
  ${CORE}/Wasapi/WasapiOutput.cpp ${CORE}/Capture/UsbAudioSource.cpp ${CORE}/Engine/AudioEngine.cpp)
target_include_directories(AudioCoreImpl PUBLIC src/AudioCore/include ${CORE} "${USB}/include")
target_link_libraries(AudioCoreImpl PUBLIC ws2_32 bcrypt setupapi cfgmgr32 advapi32 ole32 avrt propsys shell32
  "${USB}/lib/x64/libusb0-dynamic.lib")
add_library(AudioCore SHARED ${CORE}/AudioCore.cpp)
target_compile_definitions(AudioCore PRIVATE AUDIOCORE_EXPORTS)
target_link_libraries(AudioCore PRIVATE AudioCoreImpl delayimp)
target_link_options(AudioCore PRIVATE /DELAYLOAD:libusb0.dll)
add_executable(AudioBridge.UsbRestore src/AudioBridge.UsbRestore/main.cpp)
target_include_directories(AudioBridge.UsbRestore PRIVATE ${CORE} "${USB}/include")
target_link_libraries(AudioBridge.UsbRestore PRIVATE cfgmgr32 "${USB}/lib/x64/libusb0-dynamic.lib")
add_executable(AudioBridge.UsbRepair src/AudioBridge.UsbRepair/main.cpp
  ${CORE}/Device/UsbDeviceRepair.cpp ${CORE}/Device/AppleUsbDiscovery.cpp)
target_include_directories(AudioBridge.UsbRepair PRIVATE ${CORE})
target_link_libraries(AudioBridge.UsbRepair PRIVATE setupapi cfgmgr32 advapi32)
add_executable(AudioProbe tools/AudioProbe/main.cpp)
target_link_libraries(AudioProbe PRIVATE AudioCore AudioCoreImpl)
target_include_directories(AudioProbe PRIVATE src/AudioCore/include)
add_executable(AudioCore.Tests tests/AudioCoreTests.cpp)
target_link_libraries(AudioCore.Tests PRIVATE AudioCoreImpl AudioCore)
enable_testing()
add_test(NAME AudioCore.Tests COMMAND AudioCore.Tests)
set_tests_properties(AudioCore.Tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_executable(PcmProcessing.Tests tests/PcmProcessingTests.cpp)
target_include_directories(PcmProcessing.Tests PRIVATE "${CORE}")
add_test(NAME PcmProcessing.Tests COMMAND PcmProcessing.Tests)
configure_file("${USB}/bin/x64/libusb0.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/libusb0.dll" COPYONLY)

option(AB_BUILD_ZL "Build the original ZL editor with the USB audio engine" OFF)
if(AB_BUILD_ZL)
  enable_language(C)
  set(ZL_JUCE_FORMATS Standalone CACHE STRING "" FORCE)
  set(ZL_JUCE_COPY_PLUGIN OFF CACHE BOOL "" FORCE)
  set(ZL_HWY_STATIC_TARGET SSE2 CACHE STRING "" FORCE)
  add_subdirectory(third_party/ZLCompressor)
  target_compile_definitions(SharedCode INTERFACE ZL_USB_BRIDGE=1)
  target_compile_definitions(ZLCompressor_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
  target_sources(ZLCompressor_Standalone PRIVATE
    src/ZLUsbAudioBridge/BridgeApplication.cpp src/ZLUsbAudioBridge/ZlProcessorAdapter.cpp src/ZLUsbAudioBridge/SelfTests.cpp)
  target_include_directories(ZLCompressor_Standalone PRIVATE third_party/ZLCompressor/source src/ZLUsbAudioBridge)
  target_link_libraries(ZLCompressor_Standalone PRIVATE AudioCoreImpl)
  set_target_properties(ZLCompressor_Standalone PROPERTIES OUTPUT_NAME ZLUsbAudioBridge)
  add_custom_target(ZLBridge.Runtime ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${USB}/bin/x64/libusb0.dll" "$<TARGET_FILE_DIR:ZLCompressor_Standalone>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:AudioBridge.UsbRestore>" "$<TARGET_FILE_DIR:ZLCompressor_Standalone>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:AudioBridge.UsbRepair>" "$<TARGET_FILE_DIR:ZLCompressor_Standalone>"
    DEPENDS ZLCompressor_Standalone AudioBridge.UsbRestore AudioBridge.UsbRepair)
  add_test(NAME ZLBridge.Dsp COMMAND ZLCompressor_Standalone --self-test --result "${CMAKE_BINARY_DIR}/zl-self-test.json")
  set_tests_properties(ZLBridge.Dsp PROPERTIES TIMEOUT 60)
endif()
