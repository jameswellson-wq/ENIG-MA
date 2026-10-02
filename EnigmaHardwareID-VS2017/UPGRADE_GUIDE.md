# EnigmaHardwareID — VC6 → VS2017+ 迁移指南

> **原始版本**：Visual C++ 6.0（1998）  
> **目标版本**：Visual Studio 2017 / 2019 / 2022（Win32 / x86）  
> **新增功能**：spdlog 结构化日志（滚动文件 + VS 输出窗口）

---

## 一、快速开始（5 步完成）

### 步骤 1：安装 vcpkg 并集成到 Visual Studio

```powershell
# 在任意目录（推荐 C:\vcpkg）
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# 全局集成到 Visual Studio（只需执行一次）
C:\vcpkg\vcpkg integrate install
```

### 步骤 2：安装 spdlog（x86-windows）

```powershell
# 项目使用 vcpkg.json 清单模式，无需手动指定版本
C:\vcpkg\vcpkg install --triplet x86-windows
```

> **提示**：如果 VS 已集成 vcpkg，打开解决方案后会自动安装 `vcpkg.json` 中声明的依赖。

### 步骤 3：用 Visual Studio 打开解决方案

```
双击：EnigmaHardwareID.sln
```

首次打开时，VS 可能提示"重定目标项目"，选择最新已安装的 Windows SDK 版本即可。

### 步骤 4：选择配置并生成

- 工具栏选 `Debug | Win32` 或 `Release | Win32`
- 菜单 → **生成** → **生成解决方案**（`Ctrl+Shift+B`）
- 输出文件位于：`bin\Debug\EnigmaHardwareID.exe` 或 `bin\Release\`

### 步骤 5：运行程序

```
bin\Debug\EnigmaHardwareID.exe
```

日志文件将自动生成在工作目录的 `logs\enigma_hwid.log`（滚动，5MB × 3 个备份）。

---

## 二、代码变更清单

| 编号 | 类别 | 原始（VC6）| 现代化（VS2017+）|
|------|------|-----------|-----------------|
| M01 | 类型定义 | 手动 `typedef int8_t`、`uint8_t` | `#include <cstdint>` |
| M02 | 内存管理 | `std::auto_ptr<>` | `std::unique_ptr<>` |
| M03 | 循环作用域 | `for (int i=0; ...)` 变量泄漏到外层 | 标准 C++11 限定域 |
| M04 | 字符串操作 | `strcat()` 裸字符数组 | `std::string` 拼接 |
| M05 | **日志** | `printf()` / 无日志 | **spdlog 结构化日志** |
| M06 | 格式化 | `sprintf`、`strcpy` | `sprintf_s`、`strcpy_s` |
| M07 | 数组初始化 | `for` 逐字节清零 | `memset()` |
| M08 | 堆内存 | `malloc` + 裸指针 | `std::unique_ptr<BYTE[]>` |
| M09 | 重复逻辑 | 函数内重复代码块 | C++11 Lambda |
| ASM | 内联汇编 | `_asm { ... }` | **保持不变**（VS2017 x86 支持）|

---

## 三、spdlog 日志点说明

```
程序启动
│
├─ EnigmaHardwareID.cpp::InitInstance()
│   └─ [INFO]  "EnigmaHardwareID 启动"
│
├─ GetHDDString()
│   ├─ [DEBUG] "开始获取硬盘序列号..."
│   ├─ [WARN]  "CreateFile 失败，无法打开存储设备"（如失败）
│   └─ [INFO]  "硬盘序列号获取成功: {序列号}"
│
├─ GetCPUVendorID()
│   ├─ [DEBUG] "开始获取 CPU 厂商 ID..."
│   └─ [INFO]  "CPU 厂商 ID: {id}"
│
├─ GetMotherboard()
│   ├─ [DEBUG] "开始获取主板信息..."
│   └─ [INFO]  "主板信息: {info}"
│
├─ GetWindowSerial()
│   ├─ [DEBUG] "开始读取 Windows 序列号..."
│   └─ [INFO]  "Windows 序列号: {serial}"
│
├─ OnGetbutton()            ← 点击"Get"按钮
│   ├─ [INFO]  "=== 开始采集硬件指纹 ==="
│   ├─ [DEBUG] "CPU CRC32: 0xXXXXXXXX" ×N
│   └─ [INFO]  "硬件指纹采集完成，共 N 个组件"
│
├─ OnGenhwid()              ← 点击"Gen HWID"按钮
│   ├─ [WARN]  "RC4 密钥为空"（如空）
│   └─ [INFO]  "=== HWID 生成完成: '...'"
│
└─ OnDecodehwid()           ← 点击"Decode HWID"按钮
    ├─ [WARN]  各种解码失败原因
    └─ [INFO]  "=== HWID 解码完成 ==="
```

---

## 四、日志文件位置与轮转规则

```
logs\
└─ enigma_hwid.log        ← 当前日志
└─ enigma_hwid.1.log      ← 前一个（满 5 MB 时轮转）
└─ enigma_hwid.2.log      ← 最多保留 3 个
```

**日志格式**：
```
[2026-10-02 05:30:12.345] [info ] EnigmaHardwareID 启动
[2026-10-02 05:30:12.567] [debug] 开始获取硬盘序列号...
[2026-10-02 05:30:12.890] [info ] 硬盘序列号获取成功: DEADBEEF12345678
```

**Release 模式**：仅记录 `info` 及以上级别，`debug` 日志被编译器完全优化去除（零开销）。

---

## 五、spdlog Header-Only 模式（不使用 vcpkg 时）

如果不想用 vcpkg，可直接解压 spdlog 源码：

```powershell
# 下载最新版本
Invoke-WebRequest -Uri https://github.com/gabime/spdlog/archive/refs/tags/v1.12.0.zip `
                  -OutFile spdlog.zip
Expand-Archive spdlog.zip -DestinationPath .\third_party\
```

然后在 `.vcxproj` 中修改 include 路径：

```xml
<AdditionalIncludeDirectories>
  $(ProjectDir);
  $(ProjectDir)..\third_party\spdlog-1.12.0\include;
  %(AdditionalIncludeDirectories)
</AdditionalIncludeDirectories>
```

---

## 六、已知限制

| 项目 | 状态 |
|------|------|
| x86 内联汇编（CPUID）| ✅ 正常（VS2017 x86 完整支持） |
| x64 构建 | ❌ `_asm` 块在 x64 不支持，需改用 `__cpuid` 内置函数 |
| Unicode 字符集 | ⚠️ 代码使用 MBCS，切换 Unicode 需修改字符串字面量 |
| 异步 spdlog | 可选：在 `logger.h` 中将 `mt` 系列 sink 改为 `async_logger` |

---

## 七、文件结构

```
EnigmaHardwareID-VS2017/
├── EnigmaHardwareID.sln              ← VS 解决方案（双击打开）
├── UPGRADE_GUIDE.md                  ← 本文件
└── EnigmaHardwareID/
    ├── EnigmaHardwareID.vcxproj      ← VS2017+ 项目文件（★ 新增）
    ├── EnigmaHardwareID.vcxproj.filters ← 项目资源管理器分类（★ 新增）
    ├── vcpkg.json                    ← spdlog 依赖声明（★ 新增）
    ├── logger.h                      ← spdlog 初始化封装（★ 新增）
    ├── StdAfx.h / StdAfx.cpp         ← 预编译头（已现代化）
    ├── EnigmaHardwareID.h / .cpp     ← 应用入口（已添加 spdlog 初始化）
    ├── EnigmaHardwareIDDlg.h / .cpp  ← 主对话框（已全面重构）
    ├── config.h                      ← RSA Demo 密钥（不变）
    ├── resource.h                    ← 资源 ID（不变）
    ├── EnigmaHardwareID.rc           ← 对话框资源（不变）
    └── res/                          ← 图标等资源（不变）
```
