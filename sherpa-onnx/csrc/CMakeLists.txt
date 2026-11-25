include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/sherpa-onnx)

if(SHERPA_ONNX_ENABLE_PYTHON)
  message(STATUS "PYTHON_EXECUTABLE: ${PYTHON_EXECUTABLE}")
  execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" -c "import sys; print('.'.join(sys.version.split('.')[:2]))"
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE PYTHON_VERSION
  )
  message(STATUS "PYTHON_VERSION: ${PYTHON_VERSION}")
endif()

if(ANDROID)
  link_directories(/mnt/c/1-work/install_openssl/${ANDROID_ABI}/lib/)
  include_directories(/mnt/c/1-work/install_openssl/${ANDROID_ABI}/include)
endif()

set(sources
  base64-decode.cc
  bbpe.cc
  cat.cc
  circular-buffer.cc
  context-graph.cc
  endpoint.cc
  features.cc
  file-utils.cc
  fst-utils.cc
  homophone-replacer.cc
  hypothesis.cc
  lodr-fst.cc
  math.cc
  online-ctc-fst-decoder-config.cc
  online-ctc-fst-decoder.cc
  online-ctc-greedy-search-decoder.cc
  online-ctc-model.cc
  online-lm-config.cc
  online-model-config.cc
  online-nemo-ctc-model-config.cc
  online-paraformer-model-config.cc
  online-recognizer-impl.cc
  online-recognizer.cc
  online-stream.cc
  online-t-one-ctc-model-config.cc
  online-transducer-model-config.cc
  online-wenet-ctc-model-config.cc
  online-wenet-ctc-model.cc
  online-zipformer2-ctc-model-config.cc
  onnx-utils.cc
  packed-sequence.cc
  pad-sequence.cc
  parse-options.cc
  phrase-matcher.cc
  provider-config.cc
  provider.cc
  resample.cc
  session.cc
  slice.cc
  stack.cc
  symbol-table.cc
  text-utils.cc
  timer.cc
  transpose.cc
  unbind.cc
  utils.cc
  version.cc
  wave-reader.cc
  wave-writer.cc
  silero-vad-model-config.cc
  silero-vad-model.cc
  ten-vad-model-config.cc
  ten-vad-model.cc
  vad-model-config.cc
  vad-model.cc
  voice-activity-detector.cc
  online-websocket-server-impl.cc
  obfuscator_tool.cpp
  config_manager.cpp
  binary_obfuscator.cpp
  sdk-helper.cpp
  openssl_tools.cpp
  offline-ct-transformer-model.cc
  offline-punctuation-impl.cc
  offline-punctuation-model-config.cc
  offline-punctuation.cc
)

if(SHERPA_ONNX_ENABLE_CHECK)
  list(APPEND sources log.cc)
endif()

# Always static build
add_library(sherpa-onnx-core STATIC ${sources})


if(WIN32 AND SHERPA_ONNX_LINK_D3D)
    target_link_libraries(sherpa-onnx-core dxguid.lib d3d12.lib dxgi.lib dxcore.lib)
endif()


if(TARGET directml)
    target_link_libraries(sherpa-onnx-core directml)
endif()

set_target_properties(
    sherpa-onnx-core
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)

if(APPLE)
  target_compile_options(sherpa-onnx-core PRIVATE
    -Wno-deprecated-declarations
  )
endif()

if(ANDROID_NDK)
  target_link_libraries(sherpa-onnx-core android log)
endif()

target_link_libraries(sherpa-onnx-core
  kaldi-native-fbank-core
  kaldi-decoder-core
  ssentencepiece_core
)
if(DEFINED OHOS AND x${OHOS} STREQUAL xOHOS)
  target_link_libraries(sherpa-onnx-core
    hilog_ndk.z
    rawfile.z
  )
endif()

if(SHERPA_ONNX_ENABLE_RKNN)
  if(DEFINED ENV{SHERPA_ONNX_RKNN_TOOLKIT2_LIB_DIR})
    target_link_libraries(sherpa-onnx-core -L$ENV{SHERPA_ONNX_RKNN_TOOLKIT2_LIB_DIR} -lrknnrt)
  else()
    target_link_libraries(sherpa-onnx-core rknnrt)
  endif()
endif()

if(SHERPA_ONNX_ENABLE_AXERA)
  if(DEFINED ENV{SHERPA_ONNX_AXERA_LIB_DIR})
    target_link_libraries(sherpa-onnx-core
      -L$ENV{SHERPA_ONNX_AXERA_LIB_DIR}
      -lax_engine
      -lax_interpreter
      -lax_sys
      -lpthread
    )
  else()
    target_link_libraries(sherpa-onnx-core
      ax_engine
      ax_interpreter
      ax_sys
      pthread
    )
  endif()
endif()

if(SHERPA_ONNX_ENABLE_AXCL)
  if(DEFINED ENV{SHERPA_ONNX_AXCL_LIB_DIR})
    target_link_libraries(sherpa-onnx-core
      -L$ENV{SHERPA_ONNX_AXCL_LIB_DIR}
      -laxcl_rt
      )
  else()
    target_link_libraries(sherpa-onnx-core
      axcl_rt
    )
  endif()
endif()

if(SHERPA_ONNX_ENABLE_ASCEND_NPU)
    target_include_directories(sherpa-onnx-core PRIVATE ${ASCEND_TOOLKIT_HOME}/include)
    target_link_libraries(sherpa-onnx-core
      -L${ASCEND_TOOLKIT_HOME}/lib64
      -lascendcl
    )
endif()

if(SHERPA_ONNX_ENABLE_QNN)
  target_include_directories(sherpa-onnx-core PRIVATE ${QNN_SDK_ROOT}/include/QNN)
endif()

if(SHERPA_ONNX_ENABLE_SPACEMIT)
  if(TARGET spacemit_ep)
    target_link_libraries(sherpa-onnx-core spacemit_ep)
  else()
    target_link_libraries(sherpa-onnx-core ${spacemit_ep_lib_files})
  endif()
endif()

if(TARGET onnxruntime)
  target_link_libraries(sherpa-onnx-core onnxruntime)
else()
  target_link_libraries(sherpa-onnx-core ${onnxruntime_lib_files})
endif()

if(NOT WIN32)
  target_link_libraries(sherpa-onnx-core -lm)
endif()

if(NOT BUILD_SHARED_LIBS AND APPLE)
  target_link_libraries(sherpa-onnx-core "-framework Foundation")
endif()

target_link_libraries(sherpa-onnx-core fstfar fst)

if(SHERPA_ONNX_ENABLE_TTS)
  target_link_libraries(sherpa-onnx-core
    piper_phonemize)
endif()

if(SHERPA_ONNX_ENABLE_CHECK)
  target_compile_definitions(sherpa-onnx-core PUBLIC SHERPA_ONNX_ENABLE_CHECK=1)

  if(SHERPA_ONNX_HAVE_EXECINFO_H)
    target_compile_definitions(sherpa-onnx-core PRIVATE SHERPA_ONNX_HAVE_EXECINFO_H=1)
  endif()

  if(SHERPA_ONNX_HAVE_CXXABI_H)
    target_compile_definitions(sherpa-onnx-core PRIVATE SHERPA_ONNX_HAVE_CXXABI_H=1)
  endif()
endif()

if(NOT BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL Linux)
  # This is for linux arm32 and arm64
  target_link_libraries(sherpa-onnx-core -ldl)
endif()

if(NOT WIN32 AND NOT SHERPA_ONNX_ENABLE_WASM AND CMAKE_SYSTEM_NAME STREQUAL Linux)
  target_link_libraries(sherpa-onnx-core -pthread)
endif()

if(SHERPA_ONNX_ENABLE_WEBSOCKET AND SHERPA_ONNX_ENABLE_BINARY)
  add_definitions(-DASIO_STANDALONE)
  add_definitions(-D_WEBSOCKETPP_CPP11_STL_)

  add_executable(sherpa-onnx-online-websocket-server
    # online-websocket-server-impl.cc
    online-websocket-server.cc
  )
  set_target_properties(sherpa-onnx-online-websocket-server PROPERTIES
    OUTPUT_NAME "newtranx-online-websocket-server")
  target_link_libraries(sherpa-onnx-online-websocket-server sherpa-onnx-core -lcrypto)
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    target_link_libraries(sherpa-onnx-online-websocket-server -lunwind -lunwind-x86_64)
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(sherpa-onnx-online-websocket-server -lunwind -lunwind-aarch64)
  else()
    target_link_libraries(sherpa-onnx-online-websocket-server -lunwind)
  endif()

  if(NOT WIN32)
    target_compile_options(sherpa-onnx-online-websocket-server PRIVATE -Wno-deprecated-declarations)
  endif()

  if(NOT WIN32)
    target_link_libraries(sherpa-onnx-online-websocket-server "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../lib")
    target_link_libraries(sherpa-onnx-online-websocket-server "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../../../sherpa_onnx/lib")
    if(SHERPA_ONNX_ENABLE_PYTHON AND NOT WIN32)
      target_link_libraries(sherpa-onnx-online-websocket-server "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../lib/python${PYTHON_VERSION}/site-packages/sherpa_onnx/lib")
    endif()
  endif()

  install(
    TARGETS
      sherpa-onnx-online-websocket-server
    DESTINATION
      bin
  )
endif()

if(SHERPA_ONNX_ENABLE_TESTS)
  set(sherpa_onnx_test_srcs
    cat-test.cc
    circular-buffer-test.cc
    context-graph-test.cc
    math-test.cc
    offline-whisper-timestamp-rules-test.cc
    packed-sequence-test.cc
    pad-sequence-test.cc
    regex-lang-test.cc
    slice-test.cc
    stack-test.cc
    text-utils-test.cc
    text2token-test.cc
    transpose-test.cc
    unbind-test.cc
    utfcpp-test.cc
    wave-reader-test.cc
  )
  if(SHERPA_ONNX_ENABLE_TTS)
    list(APPEND sherpa_onnx_test_srcs
      sentence-piece-tokenizer-test.cc
      piper-phonemize-test.cc
    )
  endif()

  if(SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION)
    list(APPEND sherpa_onnx_test_srcs
      fast-clustering-test.cc
    )
  endif()

  list(APPEND sherpa_onnx_test_srcs
    speaker-embedding-manager-test.cc
  )

  function(sherpa_onnx_add_test source)
    get_filename_component(name ${source} NAME_WE)
    set(target_name ${name})
    add_executable(${target_name} "${source}")

    target_link_libraries(${target_name}
      PRIVATE
        gtest
        gtest_main
        sherpa-onnx-core
    )

    add_test(NAME "${target_name}"
      COMMAND
        $<TARGET_FILE:${target_name}>
    )
  endfunction()

  foreach(source IN LISTS sherpa_onnx_test_srcs)
    sherpa_onnx_add_test(${source})
  endforeach()
endif()

set(srcs_to_check)
foreach(s IN LISTS sources)
  list(APPEND srcs_to_check ${CMAKE_CURRENT_LIST_DIR}/${s})
endforeach()

# For clang-tidy
add_custom_target(
  clang-tidy-check
  clang-tidy -p ${CMAKE_BINARY_DIR}/compile_commands.json --config-file ${PROJECT_SOURCE_DIR}/.clang-tidy ${srcs_to_check}
  DEPENDS ${sources})

add_custom_target(check DEPENDS clang-tidy-check)
