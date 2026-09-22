##### 一键编译（推荐）
#####   ./build.sh deps        # 安装系统依赖
#####   ./build.sh             # 编译 jemalloc / lua / drogon(PostgreSQL)
#####   ./build.sh drogon      # 只编译 drogon，默认开启 PostgreSQL
#####   WITH_POSTGRESQL=OFF ./build.sh drogon
##### 产物默认安装：
#####   macOS  -> 3rdback/buildout/{lib,include,bin}
#####   Linux  -> /usr/local/{lib,include,bin}（可用 PREFIX=... 覆盖）
##### 
##### 如果要安装（./build.sh 默认编译完就会安装，不必再单独 make install）:
#####   cd 3rdback
#####   chmod +x build.sh
#####   ./build.sh deps                 # 先装 cmake / autoconf / libpq 等系统依赖
#####   ./build.sh                      # 编译并安装
#####                                   # Linux 默认装到 /usr/local
#####                                   # 对 /usr/local 没写权限时会自动 sudo
#####   sudo ldconfig                   # 若已装过库但仍报 liblua.so not found，手动执行一次即可
#####                                   # 新版 build.sh 安装结束后会自动 ldconfig
##### 
##### 示例:
#####   ./build.sh                      # 编译并安装（Linux -> /usr/local；macOS -> 3rdback/buildout）
#####   PREFIX=/opt/mf ./build.sh       # 编译并安装到指定目录
#####   PREFIX=/usr/local ./build.sh    # Linux 显式装到系统目录（与默认相同）
#####   SKIP_INSTALL=1 ./build.sh       # 只编译不安装1

sudo apt install cmake
sudo apt install build-essential gcc

jemalloc
sudo apt install autoconf
git clone https://github.com/jemalloc/jemalloc.git
cd jemalloc
./autogen.sh
./configure --enable-shared --enable-prof --enable-stats
make -j8
make install
--enable-shared: 构建动态库（默认也会构建静态库）
--disable-static: 只构建动态库（不构建静态库）
--enable-prof: 启用内存分析功能
--enable-stats: 启用统计功能
--prefix=/usr/local/lib: 指定安装路径




sudo apt install libjsoncpp-dev
sudo apt install uuid-dev
sudo apt install zlib1g-dev
sudo apt install openssl libssl-dev (可选)
sudo apt install libbrotli-dev

drogon 
unzip drogon-1.9.11
cd drogon-1.9.11
mkdir build
cd build 
如果安装了ssl
cmake -DCMAKE_BUILD_TYPE=Release ..
否则
cmake -DCMAKE_BUILD_TYPE=Release -DTRANTOR_USE_TLS=none ..
make -j8
make install

lua-5.5.1
windows
cmake -B build -S .
cmake --build build --config Release

liunx
sudo apt install libreadline-dev
编译
liunx macos
cd lua-5.5.1
make
make install


nohup ./MFServer > /dev/null 2>&1 &
cmake -DCMAKE_BUILD_TYPE=Debug ..

trantor-1.5.26 编译
cd build

windows 
cmake -DBUILD_SHARED_LIBS=ON -DTRANTOR_USE_TLS=none ..
cmake --build . --config Release


# Windows / MinGW-w64（MSYS2）

Windows 下只支持 MinGW-w64，不支持 MSVC：`winlib/` 里放的是 MSVC 构建的导入库，
C++ ABI 和 MinGW 不兼容，一个都不能用，所以 lua / drogon / trantor 全部从源码重编。

## 环境

必须用 MSYS2 的 **MINGW64** shell。MSYS / UCRT64 / CLANG64 的 ABI 和 runtime 都不一样，
混用会在链接期或运行期炸得莫名其妙，`build-mingw.sh` 开头会卡 `MSYSTEM=MINGW64`。

启动方式：开始菜单里的「MSYS2 MINGW64」，或直接跑 `C:\msys64\mingw64.exe`。
**不能在 CMD / PowerShell 里执行 .sh**，会报「'.' 不是内部或外部命令」。

`.gitattributes` 里强制了 `*.sh text eol=lf`。`core.autocrlf=true` 会把脚本签出成 CRLF，
bash 直接在 `$'{\r'` 之类的地方语法报错。

## 编译第三方依赖

```bash
cd /e/usr/work/server/MFServer/3rdback
./build-mingw.sh deps      # pacman 装系统依赖（工具链 / cmake / ninja / openssl / brotli / libpq 等）
./build-mingw.sh           # 编 lua-5.5.1(ejoy skynet55) + drogon-1.9.11(含 trantor)
```

具体装哪些包见脚本里的 `install_deps`。lua / drogon / trantor 都编成**静态库**装到 `/mingw64`：
MinGW 建 DLL 要额外处理符号导出，静态链省事，代价是 MFServer 得显式链上 drogon 的全部传递依赖
（`CMakeLists.txt` 的 `elseif(WIN32)` 分支里已列全）。

不编 jemalloc：Windows 上没法像 Linux 那样靠符号插入透明替换 malloc。

## 编译主工程

```bash
cd /e/usr/work/server/MFServer
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw -j24
./3rdback/build-mingw.sh dlls
```

产物是 `bin/MFServer.exe`。

`dlls` 这步不能省：openssl / brotli / libpq 这些仍是动态库，而 MSYS2 把 libpq 装在
`/mingw64/opt/pg-16/bin`，这个目录不在 PATH 上，不拷的话哪怕在 MSYS2 shell 里跑也会报
`LIBPQ.dll: cannot open shared object file`。该目标用 `ldd` 解析出全部非系统 DLL 拷进 `bin/`，
Windows 会优先从 exe 所在目录找，之后在纯 Windows 环境（无 MSYS2 PATH）下也能直接跑。
`bin/` 已在 `.gitignore` 里，这些 DLL 不会进版本库。

