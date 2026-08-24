# RealSense Legacy Recovery Lab

这是一份基于真实设备实验整理的 Intel RealSense 旧型号恢复与跨型号固件研究记录，覆盖：

- F250 → SR300：成功进入 F200 recovery 身份并转换为 SR300；RGB 可用，Depth/IR 仍受标定表布局限制。
- LR200 → R200：成功更新到 R200 `1.0.72.10`，通过 R200 admin/NV 布局重建恢复 Depth；RGB 端点仍不送帧。

这里记录的是**有效操作、必要工具、失败路径和回滚证据**，不是无风险刷机教程。跨型号刷写可能永久损坏设备。

## 最重要的结论

1. R200 的 `DS4_Firmware_Recovery.ihex 2.0.68.8` 是更新/异常路径使用的备用运行镜像，不是硬件 ROM bootloader。
2. LR200 没有实体 recovery 按键；进入更新流程由软件完成。
3. LR200 与 R200 的 non-firmware admin 记录语义相近，但 LR200 每个 4 KiB slot 带 `0x2D` 字节前缀；直接按相同地址复制会错位。
4. R200 admin root 位于 `0xA0000`，校准记录通常由 root index 0 指向 `0xA1000`。
5. Flash 命令包必须按 XU 描述符发送完整 `0x100` 字节；`0xEC` 只是反汇编中局部初始化长度。
6. 已验证的 NV 擦写顺序是 `0x1C pre → 0x1B erase → 0x1C post → 0x19 pages`，且每个 sector 必须立即回读校验。
7. 把旧 LR200 A1 记录转换成 R200 布局后，Depth 明显改善，但 RGB 仍无帧；普通 RGB 内外参不是唯一原因。
8. A3 单 sector 候选会让状态位从 `0x08` 增加为 `0x18`，且影响 Depth，已经判定失败并完整回滚。

## 当前状态

| 路线 | 枚举 | RGB | Depth/IR | 状态 |
|---|---|---|---|---|
| F250 → SR300 | SR300 节点正常 | 可出帧 | 无帧 | SR300 固件读取不到 F250 原标定表 |
| LR200 → R200 | R200 三节点正常 | start 成功但零帧 | Depth 可出帧 | RGB 固件状态长期带未解释 bit `0x08` |

## 文档导航

- [A1 hybrid 到底改了什么](docs/A1_HYBRID.md)
- [LR200 → R200 有效流程与回滚](docs/LR200_TO_R200.md)
- [F250 → SR300 有效流程与剩余问题](docs/F250_TO_SR300.md)
- [固件与工具清单](firmware/FIRMWARE_MANIFEST.md)
- [失败试验与禁止重复事项](docs/FAILED_EXPERIMENTS.md)
- [构建 legacy 诊断工具](docs/BUILDING.md)

## 工具

- `src/lr200-flash-backup.cpp`：只读 1 MiB SPI Flash 备份。
- `src/lr200-flash-write-probe.cpp`：带多重确认、范围限制和逐 sector 校验的研究工具。
- `src/rs-color-probe.cpp`：统一测试 R200 RGB profile、XU status 和 intent。
- `src/rs-depth-probe2.cpp`：Depth/IR/Emitter 最小探针。
- `tools/make_lr200_updater_full_match.py`：把官方更新器的 R200 PID/节点匹配字符串改为 LR200，文件长度及后续偏移不变。
- `tools/compare_nv_layouts.py`：离线比较 LR200 `+0x2D` payload 与 R200 slot。
- `tools/build_r200_slot_hybrid.py`：生成只改变一个 admin sector 的候选整片镜像；不接触设备。

这些 C++ 工具需要在 librealsense `legacy` 源码树中构建。请先阅读代码中的安全门槛；不要删除确认变量、设备型号检查或写入范围检查。

## 最低安全纪律

1. 一次只连接一台设备，并用 PID、USB 序列和全 Flash 哈希三重确认身份。
2. 写前读取两份完整 Flash，确认两份 SHA-256 和逐字节比较都一致。
3. 正常供体只读，禁止运行任何更新、标定或写 Flash 命令。
4. 每次只改变一个可解释的 sector；写后立即回读，并预先准备反向镜像。
5. 不发送未经验证的 `peek/poke`，尤其不要发送 opcode `0x11/0x12`。
6. 不把原始 `.bin` 直接交给只接受 Intel IHEX/IBIN 的更新器。

## 固件为什么不放进仓库

Intel DCM、固件 IHEX/IBIN、Viewer、更新器以及设备 Flash/标定备份未提交：

- Intel 二进制的再分发许可不明确；
- Flash/标定包含设备身份和个体光学参数；
- 修改后的更新器应由脚本从已核验的官方文件本地生成。

`firmware/FIRMWARE_MANIFEST.md` 提供版本、大小、SHA-256、已知来源和本地提取位置。

## 上游与参考

- librealsense legacy：<https://github.com/realsenseai/librealsense/tree/legacy>
- librealsense v2.29.0：<https://github.com/realsenseai/librealsense/releases/tag/v2.29.0>
- librealsense v2.54.2：<https://github.com/realsenseai/librealsense/releases/tag/v2.54.2>
- R200 Product Datasheet：<https://www.intel.com/content/dam/develop/external/us/en/documents/realsense-camera-r200-product-datasheet.pdf>

## 许可

`src/` 中派生自 librealsense legacy 的代码遵循上游 Apache-2.0 许可。Intel 固件与工具不属于本仓库许可范围，且未随仓库发布。
