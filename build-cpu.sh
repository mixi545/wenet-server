#!/usr/bin/env bash
# 维语 ARM64 CPU 编译入口。在宿主机执行。
#
# 产物（真正需要的）：
#   newtranx-online-websocket-server
#   以及它依赖的 sherpa-onnx-core / ONNX Runtime / fbank / fst
#
# 编译缓存：
#   ug-cpu/build 已被精简，只剩运行文件，不能当 CMake 目录。
#   默认把缓存挂到本目录下的 build-cache/
#   编完后把运行文件拷回：
#     ug-cpu/build/bin/newtranx-online-websocket-server
#     ug-cpu/build/lib/libonnxruntime.so
#
# 用法：
#   ./build-cpu.sh            # 编译
#   ./build-cpu.sh --plan     # 只打印会编什么，不编译
#   BUILD_JOBS=16 ./build-cpu.sh
#
# 环境变量：
#   BUILD_JOBS     并行线程，默认 8
#   BUILD_CACHE    CMake 缓存目录，默认 $src/build-cache
#   BUILD_IMAGE    默认 newtranx-arm-cpu-ug:v1
#   BUILD_NETWORK  默认 host；缓存齐全时可设 none
set -euo pipefail

jobs="${BUILD_JOBS:-8}"
src="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
image="${BUILD_IMAGE:-newtranx-arm-cpu-ug:v1}"
network="${BUILD_NETWORK:-host}"
default_cache="$src/build-cache"
cache="${BUILD_CACHE:-$default_cache}"
cmake_target="sherpa-onnx-online-websocket-server"
output_name="newtranx-online-websocket-server"

print_plan() {
  cat <<'EOF'
维语 CPU 编译计划
================

【最终产物，服务要用】
  - C++ WebSocket 服务 newtranx-online-websocket-server
  - 核心库 sherpa-onnx-core
  - ONNX Runtime（CPU）
  - kaldi-native-fbank、kissfft、OpenFST、kaldifst、kaldi-decoder、ssentencepiece
  - OpenSSL（授权）、libunwind（崩溃栈）

  当前 6002 走在线 WeNet CTC。

【已经关掉，不会编】
  - GPU / RKNN
  - TTS 语音合成
  - 说话人分离 / 说话人向量
  - Python 扩展、C API、JNI、测试、PortAudio、WASM
  - 麦克风 / alsa 命令行工具
  - 其它命令行二进制
    ENABLE_BINARY 必须开，否则 WebSocket 服务目标不会生成；
    实际只 build 这一个 target，旁边那些可执行文件不会被编。

【不会做】
  - 不提交镜像
  - 不重启 6002
EOF
}

if [[ "${1:-}" == "--plan" || "${1:-}" == "-n" ]]; then
  print_plan
  echo
  echo "cache=$cache"
  exit 0
fi

if [[ "${1:-}" == "--inside" ]]; then
  cd /workspace
  if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "container is not aarch64: $(uname -m)" >&2
    exit 1
  fi
  build_dir="/workspace/build-cache"
  cmake -S /workspace -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-flto=${BUILD_JOBS:-8}" \
    -DBUILD_SHARED_LIBS=ON \
    -DSHERPA_ONNX_ENABLE_PYTHON=OFF \
    -DSHERPA_ONNX_ENABLE_BINARY=ON \
    -DSHERPA_ONNX_ENABLE_WEBSOCKET=ON \
    -DSHERPA_ONNX_ENABLE_GPU=OFF \
    -DSHERPA_ONNX_ENABLE_TTS=OFF \
    -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF \
    -DSHERPA_ONNX_ENABLE_TESTS=OFF \
    -DSHERPA_ONNX_ENABLE_C_API=OFF \
    -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF \
    -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
    -DSHERPA_ONNX_ENABLE_JNI=OFF \
    -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=OFF
  cmake --build "$build_dir" --target "$cmake_target" --parallel "${BUILD_JOBS:-8}"
  echo
  echo "done: $build_dir/bin/$output_name"
  ls -l "$build_dir/bin/$output_name"
  exit 0
fi

if [[ "${1:-}" != "" ]]; then
  echo "Usage: $0 [--plan]" >&2
  exit 2
fi

if [[ ! -f "$cache/CMakeCache.txt" ]]; then
  echo "CMake cache not found: $cache" >&2
  echo "Set BUILD_CACHE to a full build directory. Do not use the trimmed ug-cpu/build." >&2
  exit 1
fi

print_plan
echo
echo "image=$image  jobs=$jobs  cache=$cache"
echo

if ! command -v docker >/dev/null 2>&1; then
  echo "docker not found" >&2
  exit 1
fi

docker run --rm \
  --platform linux/arm64 \
  --entrypoint /bin/bash \
  --user "$(id -u):$(id -g)" \
  --network "$network" \
  --mount "type=bind,source=$src,target=/workspace" \
  --mount "type=bind,source=$cache,target=/workspace/build-cache" \
  --workdir /workspace \
  -e HOME=/tmp \
  -e "BUILD_JOBS=$jobs" \
  "$image" \
  /workspace/build-cpu.sh --inside

built="$cache/bin/$output_name"
if [[ ! -x "$built" ]]; then
  echo "build finished but binary missing: $built" >&2
  exit 1
fi

mkdir -p "$src/build/bin" "$src/build/lib"
cp -a "$built" "$src/build/bin/$output_name"
ort="$cache/_deps/onnxruntime-src/lib/libonnxruntime.so"
if [[ -f "$ort" ]]; then
  cp -a "$ort" "$src/build/lib/libonnxruntime.so"
fi
echo "copied runtime files to $src/build"
ls -l "$src/build/bin/$output_name" "$src/build/lib/libonnxruntime.so"
