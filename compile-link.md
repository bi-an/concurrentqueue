## 运行时 so 的加载顺序

| 优先级 |           来源           |                           说明                           |
|:------:|:------------------------:|:--------------------------------------------------------:|
| 🔺 1    | LD_PRELOAD 环境变量      | 强制优先加载指定库，最高优先级，可用于函数替换           |
| 🔺 2    | rpath（编译时指定）      | 使用 -Wl,-rpath=... 编译时嵌入的路径，仅作用于当前程序（已废弃：因为优先级过高，且影响间接依赖项。推荐使用runpath，没有这两个缺点。）   |
| 🔺 3    | LD_LIBRARY_PATH 环境变量 | 运行时设置的库搜索路径，影响当前 shell 会话              |
| 🔺 4    | runpath（编译时指定）    | 使用 -Wl,-rpath 设置的路径，但优先级低于 LD_LIBRARY_PATH |
| 🔺 5    | /etc/ld.so.cache         | 由 ldconfig 生成的缓存，包含 /etc/ld.so.conf 中的路径    |
| 🔺 6    | 默认系统路径             | /lib, /usr/lib, /lib64, /usr/lib64 等标准目录            |

### 动态链接器（Dynamic Linker）

以上的 so 的加载顺序，是由 **动态链接器（Dynamic Linker）** 完成的。

每个 ELF 可执行文件在头部指定了它的动态链接器路径：

```bash
$ readelf -l your_program | grep interpreter
[Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
```

这个链接器负责在程序启动时加载所需的动态库，包括 libc.so.6（glibc 的主库）。

### ldd 检查

`ldd` (List Dynamic Dependencies) 是一个 shell 脚本，用于查看一个可执行文件或共享库所依赖的动态链接库的命令。

它的思路是：模拟 运行时链接器 的行为。

1. 设置环境变量来触发动态链接器 (ld-linux.so) 输出依赖信息，但是不真正执行程序。如：

   - `LD_TRACE_LOADED_OBJECTS=1`
   - `LD_WARN`, `LD_BIND_NOW`, `LD_VERBOSE` 等

2. 调用动态链接器查找 so .

局限性：

- 使用 `dlopen()` 动态加载库，`ldd` 是无法检测到的

### 补充工具

- `readelf -d binary | grep -i rpath`：查看 ELF 文件中的 RPATH 或 RUNPATH
- `ldconfig -p`：查看系统缓存中有哪些库
- `strace -e openat ./your_program`：查看运行时实际打开了哪些库文件
- `LD_DEBUG=libs ./your_program`：调试动态库加载过程


## 编译时 so 的查找顺序

GCC 查找 glibc 的路径顺序：

🧠 编译阶段（查找头文件）

| 优先级 |           路径来源          |             示例路径            |                说明               |
|:------:|:---------------------------:|:-------------------------------:|:---------------------------------:|
| 1️⃣      | 显式指定 -I 参数            | gcc -I/custom/include           | 用户手动指定，优先级最高          |
| 2️⃣      | -isystem 指定系统头文件路径 | gcc -isystem /custom/sysinclude | 优先级高于默认路径但低于 -I       |
| 3️⃣      | 环境变量 CPATH              | /opt/common/include             | 适用于所有语言，优先于默认路径    |
| 4️⃣      | 环境变量 C_INCLUDE_PATH     | /opt/glibc-2.28/include         | 仅对 C 文件有效，优先于默认路径   |
| 5️⃣      | 环境变量 CPLUS_INCLUDE_PATH | /opt/cpp/include                | 仅对 C++ 文件有效，优先于默认路径 |
| 6️⃣      | GCC 默认系统路径            | /usr/include                    | 最后兜底路径                      |
| 7️⃣      | 内核头文件路径（特殊场景）  | /usr/src/linux/include          | 编译内核模块或驱动时使用          |

- `-I` 和 `-isystem` 都是命令行参数，但 `-isystem` 会将路径标记为“系统路径”，避免某些警告。
- 环境变量如 `CPATH` 和 `C_INCLUDE_PATH` 是在没有显式参数时的补充手段。
- 默认路径 `/usr/include` 是 GCC 安装时配置的，可以通过 `gcc -xc -E -v -` 查看完整搜索路径。


🔗 链接阶段（查找库文件）

| 优先级 |               来源类型              | 是否可覆盖 |              说明              |
|:------:|:-----------------------------------:|:----------:|:------------------------------:|
| 1️⃣      | 显式命令行参数 -L                   | ✅          | 最优先，用户指定的库路径       |
| 2️⃣      | -rpath-link 参数                    | ✅          | 链接器查找间接依赖库时使用     |
| 3️⃣      | 环境变量 LIBRARY_PATH               | ✅          | 编译时使用，优先于默认路径     |
| 4️⃣      | --sysroot 指定根路径                | ✅          | 用于交叉编译，影响所有路径解析 |
| 5️⃣      | specs 文件配置                      | ✅          | GCC 内部配置，可自定义行为     |
| 6️⃣      | 默认系统路径 /lib, /usr/lib, /lib64 | ❌          | 最后兜底路径                   |


### 编译时链接器（ld）

编译时链接器（ld） 和 运行时动态链接器 不是同一个东西：

|       特性       | 编译时链接器 (`ld`) | 运行时动态链接器 (`ld-linux.so`) |
|:----------------:|:-----------------:|:------------------------------:|
| 执行时机         | 编译阶段          | 程序启动时                     |
| 作用             | 生成可执行文件    | 加载 .so 库并绑定符号          |
| 由谁调用         | 编译器（如 gcc）  | 操作系统加载器                 |
| 是否参与运行过程 | ❌ 不参与          | ✅ 参与                         |

## glibc

glibc 是特殊的 so ，它封装了系统调用，所以 Linux 系统本身也依赖它。

C 语言允许编译时链接和运行时链接分开，所以常常遇到这样奇怪的问题：

- 在本机编译、链接；
- 在本机不能运行：

```Code
./myapp: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.33' not found (required by ./myapp)
```

这是因为 gcc 在编译时指定的 glibc 和系统运行时的 glibc 不一样：

```bash
$ strings /lib/x86_64-linux-gnu/libc.so.6 | grep GLIBC_
GLIBC_2.2.5
...
GLIBC_2.31

$ strings ./myapp | grep GLIBCXX_
GLIBC_2.3
...
GLIBCXX_2.33
```

`/lib/x86_64-linux-gnu/libc.so.6` 中确实没有 `GLIBC_2.33` 的符号。

如果你的系统中有多个 glibc ，那么这种问题可以通过上面“运行时 so 的加载顺序”，使用环境变量调整。

最好的方式，是在编译时就确认 GCC 的 glibc 和系统运行时一致。

### 检查当前 GCC 使用的 glibc 路径

```bash
gcc -print-file-name=libc.so
g++ -print-file-name=libstdc++.so
gcc -print-search-dirs
```

### 确认当前系统的 glibc 和 libstdc++ 版本

✅ 查看 glibc 版本（即 libc.so）
```bash
ldd --version
# 或者
getconf GNU_LIBC_VERSION
```

✅ 查看 libstdc++ 版本（C++ 标准库）

```bash
strings /usr/lib*/libstdc++.so.6 | grep GLIBCXX
```

你会看到类似：

```Code
GLIBCXX_3.4.21
GLIBCXX_3.4.26
```

这些是你系统支持的 C++ ABI 版本。


### 系统默认路径

由 /etc/ld.so.conf 和 ldconfig 决定

通常包括：

- /lib64/
- /usr/lib64/
- /lib/x86_64-linux-gnu/（Debian/Ubuntu）

你可以运行：

```bash
ldconfig -p | grep libc.so.6
```
来查看系统当前可用的 libc.so.6 版本和路径。