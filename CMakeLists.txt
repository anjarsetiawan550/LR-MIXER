cmake_minimum_required(VERSION 3.22)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: no VC++ redistributable needed
project(LRProAudioDevice VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# ---- ASIO SDK (Steinberg) : -DASIO_SDK_DIR=<folder yang berisi common/ host/ ...>  atau taruh di ThirdParty/asiosdk
set(ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK")
if(NOT ASIO_SDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asiosdk/common/iasiodrv.h")
  set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asiosdk")
endif()

juce_add_plugin(LRProAudioDevice
  COMPANY_NAME "LR PRO"
  PLUGIN_MANUFACTURER_CODE LRpr
  PLUGIN_CODE Lrad
  FORMATS VST3 Standalone
  PRODUCT_NAME "LR PRO AUDIO DEVICE"
  BUNDLE_ID com.lrpro.audiodevice
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx Dynamics EQ Analyzer
  COPY_PLUGIN_AFTER_BUILD FALSE
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon/app_icon_256.png")

file(GLOB ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png")
juce_add_binary_data(LRAssets SOURCES ${ASSET_FILES})
set_target_properties(LRAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(LRProAudioDevice PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)

target_compile_definitions(LRProAudioDevice PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_WASAPI=1 JUCE_WASAPI_EXCLUSIVE=1      # fallback low-latency tanpa ASIO (exclusive mode)
  JUCE_DIRECTSOUND=0)

if(ASIO_SDK_DIR)
  message(STATUS "ASIO SDK ditemukan: ${ASIO_SDK_DIR}  -> ASIO aktif")
  target_compile_definitions(LRProAudioDevice PUBLIC JUCE_ASIO=1)
  target_include_directories(LRProAudioDevice PUBLIC "${ASIO_SDK_DIR}/common" "${ASIO_SDK_DIR}/host" "${ASIO_SDK_DIR}/host/pc")
else()
  message(WARNING "ASIO SDK tidak ditemukan -> build tanpa ASIO (hanya WASAPI/DirectSound)")
endif()

target_link_libraries(LRProAudioDevice
  PRIVATE LRAssets juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

if(MSVC)
  target_compile_options(LRProAudioDevice PRIVATE /O2 /Ob2 /MP)
endif()
