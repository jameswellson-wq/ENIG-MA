// logger.h — spdlog 日志系统初始化封装
// ============================================================
// 输出目标：
//   Sink 1: logs/enigma_hwid.log（滚动文件，5MB x 3个）
//   Sink 2: Visual Studio 输出窗口（调试模式可见）
//
// 使用示例：
//   spdlog::info("采集完成，CRC={:08X}", crc);
//   spdlog::debug("偏移量={}", offset);
//   spdlog::warn("CreateFile 失败，code={}", GetLastError());
//   spdlog::error("CRC 校验不通过");
// ============================================================
#pragma once

// ── 安装方式（二选一）───────────────────────────────────────
// 方式 A（推荐）：vcpkg
//   vcpkg install spdlog:x86-windows
//   vcpkg integrate install
//   → VS 自动找到头文件，无需手动配置
//
// 方式 B：手动 Header-only
//   从 https://github.com/gabime/spdlog 下载
//   将 include/spdlog/ 复制到 EnigmaHardwareID/include/spdlog/
//   vcxproj 已将 $(ProjectDir)include 加入 AdditionalIncludeDirectories
// ────────────────────────────────────────────────────────────

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <memory>
#include <vector>
#include <windows.h>   // CreateDirectoryA

namespace EnigmaLog {

/// 在 InitInstance() 开头调用，初始化全局默认 logger
inline void Init()
{
    try {
        // 确保 logs/ 目录存在
        ::CreateDirectoryA("logs", NULL);

        std::vector<spdlog::sink_ptr> sinks;

        // Sink 1：滚动文件日志（5MB 单文件上限，最多保留 3 个）
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/enigma_hwid.log",
            5ULL * 1024 * 1024,   // 5 MB
            3                      // 保留 3 个轮转文件
        );
        file_sink->set_level(spdlog::level::debug);
        sinks.push_back(file_sink);

        // Sink 2：Visual Studio 输出窗口（调试运行时可见）
        auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        msvc_sink->set_level(spdlog::level::debug);
        sinks.push_back(msvc_sink);

        // 组合 logger → 设为全局默认（spdlog::info/debug/warn/error 直接使用）
        auto logger = std::make_shared<spdlog::logger>(
            "enigma", sinks.begin(), sinks.end()
        );
        logger->set_level(spdlog::level::debug);
        // 格式：[时间戳]  [级别]  消息
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%-5l%$] %v");
        // warn 及以上级别立即刷盘（不丢失错误日志）
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(logger);
        spdlog::info("=========================================");
        spdlog::info("EnigmaHardwareID  日志系统初始化完成");
        spdlog::info("日志文件: logs/enigma_hwid.log");
        spdlog::info("=========================================");

    } catch (const spdlog::spdlog_ex& ex) {
        // 降级：无法写文件时至少输出到 VS 调试窗口
        ::OutputDebugStringA("[EnigmaLog] spdlog 初始化失败: ");
        ::OutputDebugStringA(ex.what());
        ::OutputDebugStringA("\n");
    }
}

/// 在 ExitInstance() 或程序退出前调用，刷盘并释放资源
inline void Shutdown()
{
    spdlog::info("=========================================");
    spdlog::info("EnigmaHardwareID  正在关闭");
    spdlog::info("=========================================");
    spdlog::shutdown();
}

} // namespace EnigmaLog
