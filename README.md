# wenet-server

维语 ARM CPU 流式语音识别服务。模型是在线 WeNet CTC，对外提供 WebSocket，默认端口 **6002**。

## 产物

```text
build/bin/newtranx-online-websocket-server
build/lib/libonnxruntime.so
```

## 编译

在 x86 宿主机上用 Docker / QEMU 编 aarch64：

```bash
./build-cpu.sh --plan
./build-cpu.sh
```

缓存目录 `build-cache/`，运行文件在 `build/`，都不进 git。

## 运行

```bash
docker compose up -d
```

Compose 会挂载：

- 模型：`model.int8.onnx.obfus`、`tokens.txt.obfus`
- 授权：`license.txt`
- 日志：`./logs/ug.log`

识别参数：`--provider cpu`，`--num-threads=5`，`--num-work-threads=10`。

本地直接起进程：

```bash
./start-server.sh --port=6002
```
