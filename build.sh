#!/usr/bin/env bash
# 维语 ARM64 CPU 编译入口。
#
# aarch64 机器：本机 cmake，不启容器
# 其它架构：Docker / QEMU 编 aarch64
#
# 产物：
#   build/bin/newtranx-online-websocket-server
#   build/lib/libonnxruntime.so
#
# 用法：
#   ./build.sh
#   ./build.sh --plan
#   BUILD_JOBS=16 ./build.sh
#   FORCE_DOCKER=1 ./build.sh
set -euo pipefail

jobs="${BUILD_JOBS:-8}"
src="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
image="${BUILD_IMAGE:-newtranx-arm-cpu-ug:v1}"
network="${BUILD_NETWORK:-host}"
cache="${BUILD_CACHE:-$src/build-cache}"
cmake_target="sherpa-onnx-online-websocket-server"
output_name="newtranx-online-websocket-server"
arch="$(uname -m)"

print_plan() {
  cat <<EOF
维语 CPU 编译计划
================
本机架构: $arch
缓存: $cache
产物:
  build/bin/$output_name
  build/lib/libonnxruntime.so

当前服务走在线 WeNet CTC。
aarch64 上直接 cmake（需 cmake、g++、libssl、libunwind）。
其它架构用 Docker 镜像 $image。
EOF
}

cmake_build() {
  local root="$1"
  local build_dir="$2"
  if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "need aarch64, got $(uname -m)" >&2
    exit 1
  fi
  cmake -S "$root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-flto=${jobs}" \
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
  cmake --build "$build_dir" --target "$cmake_target" --parallel "$jobs"
}

copy_runtime() {
  local built="$cache/bin/$output_name"
  local ort="$cache/_deps/onnxruntime-src/lib/libonnxruntime.so"
  if [[ ! -x "$built" ]]; then
    echo "binary missing: $built" >&2
    exit 1
  fi
  mkdir -p "$src/build/bin" "$src/build/lib"
  cp -a "$built" "$src/build/bin/$output_name"
  if [[ -f "$ort" ]]; then
    cp -a "$ort" "$src/build/lib/libonnxruntime.so"
  fi
  echo "copied runtime files to $src/build"
  ls -l "$src/build/bin/$output_name" "$src/build/lib/libonnxruntime.so"
}

if [[ "${1:-}" == "--plan" || "${1:-}" == "-n" ]]; then
  print_plan
  exit 0
fi

if [[ "${1:-}" == "--inside" ]]; then
  cmake_build "$src" "$cache"
  exit 0
fi

if [[ "${1:-}" != "" ]]; then
  echo "Usage: $0 [--plan]" >&2
  exit 2
fi

mkdir -p "$cache"
print_plan
echo

if [[ "$arch" == "aarch64" && "${FORCE_DOCKER:-0}" != "1" ]]; then
  cmake_build "$src" "$cache"
  copy_runtime
  exit 0
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "docker not found; x86 编译需要 Docker" >&2
  exit 1
fi

echo "image=$image  jobs=$jobs"
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
  /workspace/build.sh --inside

copy_runtime
