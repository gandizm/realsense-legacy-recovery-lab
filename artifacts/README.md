# 已发布的修改版产物

这些文件是本次实验实际构建或生成并使用过的产物。仓库现在是 Public，下载和运行前必须阅读根目录 README 及 `docs/FAILED_EXPERIMENTS.md`。

## librealsense 诊断工具

| 文件 | 用途 | 风险 |
|---|---|---|
| `lr200-flash-backup.exe` | 只读读取 1 MiB Flash | 只读，但必须核对设备身份 |
| `lr200-flash-write-probe.exe` | NV 擦写/回读探针 | **危险**；默认不写，写入需 commit 和环境变量双重确认 |
| `rs-color-probe.exe` | RGB UVC/XU 流探针 | 只开流，不写 Flash |
| `rs-depth-probe2.exe` | Depth/IR/Emitter 探针 | 只开流，可临时改 emitter 控件 |

这些 EXE 从本仓库 `src/` 的源码构建，源码和构建方法同时发布。它们不是官方 Intel 更新器。

## 更新器修改版

| 文件 | 生成/修改目的 | 运行前提 |
|---|---|---|
| `FWUpdateLR200-full-match.exe` | 将官方 R200 更新器的 PID/完整 UVC 节点匹配改为 LR200 | 只在目标 LR200 上使用；先双备份 |
| `FWUpdateLR20032-PID-only.exe` | x86 PID-only 旧版本 | 不推荐，节点名匹配不完整 |
| `FWUpdateR200-allow-pre-safe.exe` | 放宽主固件最低版本门槛，供 2.0.68.8 recovery 试验 | **失败实验产物，不要重复刷** |
| `FWUpdateR200-recovery-only.exe` | recovery-only 研究版 | **危险/历史实验** |
| `FWUpdateR200-recovery-only-forced.exe` | 强制 recovery-only 研究版 | **危险/历史实验** |

这些文件基于 Intel DCM 中的官方更新器修改而来，固件载荷仍由更新器外部读取。它们不是 Intel 官方发布版本；如果你的法律/分发环境不允许公开再分发 Intel 更新器，请删除 `artifacts/FWUpdate*.exe`，只保留生成脚本和哈希。
## 完整性

`SHA256SUMS.txt` 是提交时生成的 SHA-256 清单。重新构建时，输出哈希必须与预期不同/相同的原因可解释；不要把哈希不匹配的 EXE 用于设备。
