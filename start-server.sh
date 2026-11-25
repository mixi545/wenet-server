#!/usr/bin/env bash
set -euo pipefail
source_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-$source_root/build}"
server="$build_dir/bin/newtranx-online-websocket-server"
if [[ ! -x "$server" ]]; then
  echo "Build the server first: $source_root/build-cpu.sh" >&2
  exit 1
fi
export LD_LIBRARY_PATH="$build_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$build_dir"
exec "$server" "$@"
