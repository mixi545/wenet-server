# wenet-server

维语 ARM CPU 在线 ASR 服务：WeNet CTC + WebSocket。

本目录已从完整 sherpa-onnx 树精简，只保留编译 `newtranx-online-websocket-server` 所需源码。

## 编译

在宿主机执行（Docker + QEMU aarch64）：

```bash
./build-cpu.sh --plan
./build-cpu.sh
```

产物：

- `build/bin/newtranx-online-websocket-server`
- `build/lib/libonnxruntime.so`

CMake 缓存在 `build-cache/`，不进 git。

## 运行

```bash
docker compose up -d
```

默认端口 **6002**。模型、授权、日志通过 volume 挂载，见 `docker-compose.yaml`。
