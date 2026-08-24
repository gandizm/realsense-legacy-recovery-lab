# 固件与工具清单

本仓库不分发 Intel 固件或更新器二进制。以下清单用于核对用户自行取得的本地文件。

## R200 DCM 载荷

在本次研究使用的三个本地 DCM 解压目录中，以下载荷逐字节相同：

| 文件 | 文件内版本 | 大小 | SHA-256 |
|---|---:|---:|---|
| `DS4_Firmware.ihex` | `1.0.72.10` | 591,822 | `DD80D1A0044CC92D8FA521F876EBC03A486FA84A1721BE3405B13B190F4FC8CF` |
| `DS4_Firmware_Recovery.ihex` | `2.0.68.8` | 474,513 | `13C7425F70066DAD3B6BF25817F32957D82B391CF4E10F9D4C3C367A3F4B0D2F` |
| `DS4_Firmware.ibin` | `0.0.70.38` | 393,261 | `F98168D3367148C7EE3A5B9F6A214508FE03C9480D356BB66EA2955130B038E9` |

注意：不同工具对 IHEX 的文件大小显示可能包含文本记录长度；运行前以 SHA-256 为准。上表大小应在本地重新核验，本仓库不把它作为授权下载依据。

已检查的本地包名：

```text
intel_rs_dcm_r200_2.1.27.2853
intel_rs_dcm_r200_2.2.98.5272
intel_rs_dcm_r200_2.2.104.3651
```

参考：

- R200 DCM 2.2.98 Release Notes：<https://downloadmirror.intel.com/29921/eng/dcm_releasenotes_r200_v2.2.98.5272.pdf>
- Intel 安全公告 INTEL-SA-00397：<https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00397.html>

## SR300 固件

| 版本 | 常见载荷名 | SHA-256 |
|---:|---|---|
| `3.10.10.0` | `SignedFirmwareImage_1_5.bin` | `72667C2A93E036ED0B573AA4F61880587FEB39DB9FB8A08C303F5F0991BAE884` |
| `3.15.0.0` | `SignedFirmwareImage_1_5.bin` | `F53482FF5612E9E3095D518A638F027A23041E3CFF21A92261C47BFC380CD89A` |
| `3.21.0.0` | `SignedFirmwareImage_1_5.bin` | `AA3FE9D4DB13F79E4D0740A901A85ABB4029386AC2D0BF844E2AF99AB4F717E1` |
| `3.26.1.0` | `SR3XX_FW_Image-3.26.1.0.bin` | `C4AC2144DF13C3A64FCA9D16C175595C903E6E45F02F0F238630A223B07C14D1` |

已知来源：

- SR300 3.26.1.0：<https://librealsense.intel.com/Releases/SR300/FW/SR3XX_FW_Image-3.26.1.0.bin>
- librealsense 相关记录：<https://github.com/realsenseai/librealsense/issues/8083>
- SR300 DCM 3.0.24.59748 存档页：<https://www.touslesdrivers.com/index.php?v_code=48037&v_page=23>
- SR300 DCM 3.1.25.2599 存档页：<https://www.touslesdrivers.com/index.php?v_code=49633&v_page=23>

## 更新器补丁

不要提交修改后的 EXE。由 `tools/make_lr200_updater_full_match.py` 对已核验的官方更新器本地生成，并再次记录 source/output SHA-256。

补丁只允许：

- `PID_0A80` → `PID_0ABF`
- 完整 R200 UVC 节点名 → LR200 节点名
- 文件总长度不变
- 所有替换次数符合预期

固件载荷本身不应由字符串补丁脚本修改。

本次核验过的 x64 文件：

| 文件 | SHA-256 |
|---|---|
| 官方 `FWUpdateR200.exe` | `6D4068BC442400E8F683D98CCEB9222F4B7DF11E5AEF15F367845BBFCFB9B1D9` |
| 脚本生成的 LR200 full-match 版本 | `24F4906DBA8F7294F1DA795E484B6714B733F3CE70DA23384B5E755B7EC5D968` |

如果输出哈希不同，先停止并检查 DCM 版本和替换次数，不要直接运行。
