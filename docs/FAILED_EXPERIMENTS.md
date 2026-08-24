# 已验证失败或禁止盲目重复的试验

## LR200 / R200

- 把 `DS4_Firmware_Recovery.ihex 2.0.68.8` 当普通主固件：设备回退到备用 LR200 运行态，所有流不可用。Recovery image 不是正常使用固件。
- 用普通 Flash opcode 写 `0x10000` 或 `0x50000` 固件区：设备返回拒绝状态，内容不变。
- 未知 opcode `0x11` 的错误长度探测：曾导致固件崩溃并从另一 bank 启动。禁止重复。
- 整个旧 A1 payload：Depth 改善，RGB 不修复。
- 整个旧 A3 payload：RGB 不修复，status 从 `0x08` 增加为 `0x18`，并影响 Depth；已逐字节回滚。
- 仅写 RGB stream intent：XU 请求成功，但固件不进入 RGB streaming 状态。

## F250 / SR300

- 只换新版 Viewer/SDK：不能生成缺失的 SR300 标定表。
- 在 `3.10/3.15` 上忽略 `Params table id not valid (-17)`：可以绕过部分主机侧查询，但 Depth/IR 仍无帧。
- 把“F200 recovery 枚举”误认为已经写入 F200 固件：PID 变化只证明 recovery 路径，不能证明主 Flash 内容。

## 通用

- 不要在没有写前双备份和精确 expected-current 镜像时运行写工具。
- 不要把供体序列号/相机头记录当作目标机永久身份。
- 不要使用 `git` 仓库中的脚本替代对本地官方文件 SHA-256 的核验。
