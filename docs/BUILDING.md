# 构建 legacy 诊断工具

## 依赖

- librealsense `legacy` 分支：<https://github.com/realsenseai/librealsense/tree/legacy>
- Windows：Visual Studio 2022、CMake、Ninja
- 相机节点使用 Windows UVC 驱动

## 接入源码树

把本仓库 `src/*.cpp` 复制到 librealsense legacy 的 `tools/`，然后做以下三个最小改动。

在 `src/ds-private.h` 的 `read_isp_firmware_version()` 声明后加入：

```cpp
bool read_device_pages(uvc::device & device, uint32_t address,
                       unsigned char * buffer, uint32_t page_count);
```

在 `src/device.h` 的 `rs_device_base` public 区加入：

```cpp
rsimpl::uvc::device & get_uvc_device_for_flash_backup() { return get_device(); }
```

在顶层 `CMakeLists.txt` 的 `target_include_directories(realsense ...)` 后为四个文件各加入：

```cmake
add_executable(<target> tools/<source>.cpp)
target_include_directories(<target> PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(<target> realsense)
```

目标与源文件对应关系：

```text
lr200-flash-backup      -> lr200-flash-backup.cpp
lr200-flash-write-probe -> lr200-flash-write-probe.cpp
rs-color-probe          -> rs-color-probe.cpp
rs-depth-probe2         -> rs-depth-probe2.cpp
```

这些改动只做三件事：

1. 把已有的 `read_device_pages()` 声明暴露给工具。
2. 给内部 `rs_device_base` 增加只供研究工具使用的 UVC accessor。
3. 在 CMake 中注册四个可执行目标。

## Windows 构建示例

```powershell
cmake -S <legacy-source> -B <build-dir> -G Ninja `
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_SHARED_LIBS=OFF `
  -DBUILD_EXAMPLES=OFF `
  -DBUILD_UNIT_TESTS=OFF

cmake --build <build-dir> --target `
  lr200-flash-backup `
  lr200-flash-write-probe `
  rs-color-probe `
  rs-depth-probe2
```

Flash 读写通常需要管理员权限。先运行只读备份；不要把“程序能编译”当作已确认连接的是目标机。
