-- luamake 构建工具，定义了一个项目配置来生成名为 "ltask" 的动态库（lua_dll 表示 Lua 动态链接库）
local lm = require "luamake"

-- 定义一个名为 "ltask" 的 Lua 动态库目标，其中 { ... } 内包含具体的构建配置
lm:lua_dll "ltask" {

    -- 指定 C 语言标准为 C11。
    c = "c11",
    -- 指定源文件目录为 "src/"，其中所有 .c 文件将被编译进项目。
    sources = "src/*.c",

    defines = {
        -- "DEBUGLOG"：定义了一个预处理宏 DEBUGLOG（被注释掉，不启用）
        --"DEBUGLOG",
        -- 在 debug 模式下定义 DEBUGTHREADNAME，用于调试线程名称（适用于条件编译）。
        lm.mode=="debug" and "DEBUGTHREADNAME",
    },

    -- 针对 Windows 系统的特定配置：
    windows = {
        -- 定义了 _WIN32_WINNT，设置为 Windows 7 (0x0601)，用于启用特定的 Windows API 功能
        defines = {
            "_WIN32_WINNT=0x0601"
        },
        -- 链接 ws2_32（Windows 套接字库）和 winmm（Windows 多媒体库）库。
        links = {
            "ws2_32",
            "winmm",
        }
    },

    -- 针对 Microsoft Visual C++ (MSVC) 编译器的配置：
    msvc = {
        -- 启用 C11 的原子操作支持。
        flags = {
            "/experimental:c11atomics"
        },
        -- 导出 luaopen_ltask_bootstrap 符号，供 Lua 加载时使用。
        ldflags = {
            "-export:luaopen_ltask_bootstrap",
        },
    },

    -- 针对 GCC 编译器的配置：
    gcc = {
        -- 链接 POSIX 线程库 pthread。
        links = "pthread",
        -- 设置默认可见性为 default，确保符号在共享库中公开。
        visibility = "default",
        -- 定义 _XOPEN_SOURCE=600，启用 POSIX 标准功能（如线程和信号处理）。
        defines = "_XOPEN_SOURCE=600",
    }
}
