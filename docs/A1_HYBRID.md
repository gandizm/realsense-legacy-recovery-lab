# A1 hybrid 到底改了什么

## 操作定义

输入：

- `base`：已经具有有效 R200 admin root、R200 slot trailer 和可运行 R200 固件的 1 MiB 镜像。
- `old_lr200`：FWUpdateLR200 运行前读取的原始 LR200 1 MiB Flash。

变换：

```text
destination: base[0xA1000 + 0x000 : 0xA1000 + 0xFD3]
source:      old [0xA1000 + 0x02D : 0xA1000 + 0x1000]
preserved:   base[0xA1000 + 0xFD3 : 0xA1000 + 0x1000]
```

也就是复制 `0xFD3 = 4051` 字节旧 payload，同时保留 R200 末尾 45 字节 slot trailer。整片镜像只允许 sector `0xA1` 发生变化。

## 实际发生变化的内容

旧 payload 与当时的 R200/供体记录相比，共有 **340 个字节不同**：

| 区域 | A1 相对范围 | 变化字节数 | 含义 |
|---|---:|---:|---|
| Calibration version/counts | `0x000–0x00F` | 0 | 两者都是 calibration v2，计数相同 |
| Left intrinsics | `0x010–0x03B` | 24 | 左目未校正内参 |
| Right intrinsics | `0x03C–0x093` | 26 | 右目未校正内参 |
| Third/RGB intrinsics | `0x094–0x117` | 48 | RGB 原始分辨率内参 |
| Platform intrinsics | `0x118–0x1C7` | 0 | 未改变 |
| LR rectified modes | `0x1C8–0x287` | 34 | 深度双目校正模式 |
| Third/RGB rectified modes | `0x288–0x437` | 42 | RGB 校正模式 |
| Platform modes | `0x438–0x4F7` | 0 | 未改变 |
| `Rleft/Rright/Rthird` | `0x4F8–0x5CF` | 83 | 相机旋转矩阵，其中 Rthird 改 29 字节 |
| `B` | `0x618–0x61F` | 3 | 双目 baseline 参数 |
| `T` | `0x620–0x637` | 10 | Depth/Third 平移关系 |
| `Rworld` | `0x650–0x673` | 25 | 世界坐标旋转 |
| Camera head | `0x800–0xFD2` | 45 | 身份、日期、模块描述等 |
| R200 slot trailer | `0xFD3–0xFFF` | 0 | 刻意保留 |

## Camera head 的关键变化

hybrid 把 Camera head 从已移植的正常 R200/供体记录换成旧 LR200/IBIN 模板记录：

| 字段 | hybrid 前 | hybrid 后 |
|---|---|---|
| Camera head contents version | 12 | 8 |
| Module version | `4.2.5.0` | `255.255.255.255` |
| OEM | `0` | `0xFFFFFFFF` |
| PRQ type | `1` | `255` |
| Emitter type | `2` | `0xFFFFFFFF` |
| Third nominal baseline | 58 mm | 58 mm |
| Third lens type | 10 | 10 |

序列号也会随 Camera head 一起变化，因此 hybrid 后 USB/SDK 显示的序列不能再作为设备物理身份依据。

## 实测效果

- R200 firmware `1.0.72.10` 可以解析 A1 中的 calibration v2。
- Depth 能稳定出帧，热身后有效像素显著增加。
- RGB 仍然 `start()` 成功但收不到第一帧。
- XU status 的未解释低位 `0x08` 没有消失。

结论：A1 同时改变了 Depth 与 RGB 标定，但没有修复颜色传感器启动状态。它不是单独的“RGB 固件”或“序列号补丁”。
