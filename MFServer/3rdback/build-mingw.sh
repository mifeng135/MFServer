#!/usr/bin/env bash
# 在 MSYS2 MINGW64 环境下编译 MFServer 的第三方依赖：lua-5.5.1 / drogon-1.9.11(含 trantor)
#
# 用法:
#   ./build-mingw.sh            # 编译全部
#   ./build-mingw.sh deps       # pacman 安装系统依赖
#   ./build-mingw.sh lua        # 只编 lua
#   ./build-mingw.sh drogon     # 只编 drogon
#
# 环境变量:
#   PREFIX                  安装前缀，默认 $MINGW_PREFIX(即 /mingw64)
#   BUILD_TYPE=Release
#   JOBS
#   WITH_POSTGRESQL=ON      drogon 是否启用 PostgreSQL
#   WITH_BROTLI=ON
#   WITH_YAML=OFF           drogon 的 yaml 配置，需要 yaml-cpp，默认关掉
#   SKIP_INSTALL=0          1 则只编译不安装
#
# 与 build.sh 的差别:
#   - 不编 jemalloc：Windows 上没法像 Linux 那样靠符号插入透明替换 malloc
#   - lua 和 drogon 都装静态库：MinGW 建 DLL 要额外处理符号导出，静态链省事，
#     代价是 MFServer 必须显式链上 drogon 的全部传递依赖（CMakeLists 里已列全）

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"

log()  { printf '\n\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "未找到命令: $1"
}

# 必须是 MINGW64 环境：MSYS / UCRT64 / CLANG64 的 ABI 和 runtime 都不一样，
# 混用会在链接期或运行期炸得莫名其妙。
check_env() {
    [[ "${MSYSTEM:-}" == "MINGW64" ]] \
        || die "请在 \"MSYS2 MINGW64\" shell 里运行（当前 MSYSTEM=${MSYSTEM:-未设置}）"
    [[ -n "${MINGW_PREFIX:-}" ]] || die "MINGW_PREFIX 未设置，环境不完整"
}

check_env

PREFIX="${PREFIX:-$MINGW_PREFIX}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"
WITH_POSTGRESQL="${WITH_POSTGRESQL:-ON}"
WITH_BROTLI="${WITH_BROTLI:-ON}"
WITH_YAML="${WITH_YAML:-OFF}"
SKIP_INSTALL="${SKIP_INSTALL:-0}"

run_install() {
    if [[ "$SKIP_INSTALL" == "1" ]]; then
        warn "SKIP_INSTALL=1，跳过安装"
        return 0
    fi
    "$@"
}

# zip 里可能有未标 UTF-8 的中文文件名，unzip 会报错，优先用 python3
extract_zip() {
    local zip_path="$1" dest_dir="$2"
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$zip_path" "$dest_dir" <<'PY'
import sys, zipfile
zip_path, dest_dir = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path) as zf:
    zf.extractall(dest_dir)
PY
        return 0
    fi
    need_cmd unzip
    unzip -q "$zip_path" -d "$dest_dir"
}

ensure_src() {
    local zip_name="$1" dir_name="$2"
    local dest="$ROOT/$dir_name"
    if [[ -f "$dest/CMakeLists.txt" ]]; then
        return 0
    fi
    [[ -f "$ROOT/$zip_name" ]] || die "缺少源码: $ROOT/$zip_name"
    if [[ -d "$dest" ]]; then
        warn "源码目录不完整，重新解压 $zip_name"
        rm -rf "$dest"
    fi
    log "解压 $zip_name"
    extract_zip "$ROOT/$zip_name" "$ROOT"
    [[ -d "$dest" ]] || die "解压后未找到目录: $dest"
}

# ---------- 系统依赖 ----------

install_deps() {
    log "安装 MSYS2 依赖"
    need_cmd pacman
    pacman -S --needed --noconfirm \
        mingw-w64-x86_64-toolchain \
        mingw-w64-x86_64-cmake \
        mingw-w64-x86_64-ninja \
        mingw-w64-x86_64-jsoncpp \
        mingw-w64-x86_64-openssl \
        mingw-w64-x86_64-zlib \
        mingw-w64-x86_64-brotli \
        mingw-w64-x86_64-c-ares \
        mingw-w64-x86_64-postgresql-16 \
        unzip
}

# ---------- lua ----------

# MFServer 用的是 ejoy/lua 的 skynet55 分支，MSYS2 仓库里那个 lua 包不能替代。
build_lua() {
    ensure_src "lua-5.5.1.zip" "lua-5.5.1"
    need_cmd cmake
    local src="$ROOT/lua-5.5.1" build="$ROOT/lua-5.5.1/build"
    log "编译 lua-5.5.1 (MinGW)"

    cmake -S "$src" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$build" -j"$JOBS" --target lua_static

    run_install mkdir -p "$PREFIX/lib" "$PREFIX/include"
    run_install cp -f "$build/liblua.a" "$PREFIX/lib/"
    run_install cp -f "$src/lua.h" "$src/luaconf.h" "$src/lauxlib.h" "$src/lualib.h" "$PREFIX/include/"
    log "liblua.a -> $PREFIX/lib"
}

# ---------- drogon + trantor ----------

# MSYS2 没有 drogon / trantor 的包（打包 PR msys2/MINGW-packages#20553 已关闭），
# 只能从源码编；trantor 就在 drogon 的源码树里，一起出来。
detect_libpq() {
    [[ "$WITH_POSTGRESQL" == ON ]] || return 0
    local d
    for d in "$MINGW_PREFIX"/opt/pg-*; do
        if [[ -f "$d/include/libpq-fe.h" ]]; then
            PG_PREFIX="$d"
            log "使用 libpq: $d"
            return 0
        fi
    done
    if [[ -f "$MINGW_PREFIX/include/libpq-fe.h" ]]; then
        PG_PREFIX="$MINGW_PREFIX"
        return 0
    fi
    die "未找到 libpq，请执行: pacman -S mingw-w64-x86_64-postgresql-16"
}

build_drogon() {
    ensure_src "drogon-1.9.11.zip" "drogon-1.9.11"
    need_cmd cmake
    local src="$ROOT/drogon-1.9.11" build="$ROOT/drogon-1.9.11/build"
    [[ -f "$src/trantor/CMakeLists.txt" ]] || die "drogon 源码里缺 trantor 子目录"

    PG_PREFIX=""
    detect_libpq

    local cmake_args=(
        -G Ninja
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_CTL=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_MYSQL=OFF
        -DBUILD_SQLITE=OFF
        -DBUILD_REDIS=OFF
        -DBUILD_BROTLI="$WITH_BROTLI"
        -DBUILD_YAML_CONFIG="$WITH_YAML"
    )
    if [[ "$WITH_POSTGRESQL" == ON ]]; then
        cmake_args+=(-DBUILD_ORM=ON -DBUILD_POSTGRESQL=ON "-DCMAKE_PREFIX_PATH=$PG_PREFIX")
    else
        cmake_args+=(-DBUILD_ORM=OFF -DBUILD_POSTGRESQL=OFF)
    fi

    log "编译 drogon-1.9.11 (PostgreSQL=$WITH_POSTGRESQL Brotli=$WITH_BROTLI)"
    cmake -S "$src" -B "$build" "${cmake_args[@]}"

    # cmake 找不到 libpq 时不会报错，只是悄悄把 ORM 关掉，这里必须卡一道
    if [[ "$WITH_POSTGRESQL" == ON ]]; then
        grep -qE 'USE_POSTGRESQL[[:space:]]+1' "$build/drogon/config.h" \
            || die "cmake 未启用 PostgreSQL，检查上面日志里的 libpq 探测结果"
        log "已确认 drogon 启用 PostgreSQL"
    fi

    cmake --build "$build" -j"$JOBS"
    run_install cmake --install "$build"
}

# ---------- 运行期 DLL ----------

# lua / drogon / trantor 是静态链进去的，但 openssl、brotli、libpq 这些仍是动态库。
# libpq 尤其麻烦：MSYS2 把它装在 /mingw64/opt/pg-XX/bin，该目录不在 PATH 上，
# 于是哪怕在 MSYS2 shell 里也会报找不到 LIBPQ.dll。把非系统 DLL 收进 bin/，
# Windows 会优先从 exe 所在目录找，exe 就能脱离 MSYS2 环境直接跑。
copy_dlls() {
    local bindir exe
    bindir="$(cd "$ROOT/.." && pwd)/bin"
    exe="$bindir/MFServer.exe"
    [[ -f "$exe" ]] || die "未找到 $exe，请先编译主工程"
    need_cmd ldd

    PG_PREFIX=""
    detect_libpq
    if [[ -n "$PG_PREFIX" ]]; then
        PATH="$PG_PREFIX/bin:$PATH"
    fi

    local n=0 miss=0 name arrow src
    while read -r name arrow src _; do
        [[ "$arrow" == "=>" ]] || continue
        if [[ "$src" != /* ]]; then
            warn "未解析到 $name"
            miss=1
            continue
        fi
        # 系统 DLL 不拷，拷出来反而可能和系统版本打架
        [[ "${src,,}" == /c/windows/* ]] && continue
        cp -u "$src" "$bindir/"
        n=$((n + 1))
    done < <(ldd "$exe")

    [[ "$miss" == 0 ]] || die "有 DLL 未解析到，先确认依赖已装全"
    log "已复制 $n 个 DLL 到 $bindir"
}

usage() {
    cat <<EOF
用法: $0 [目标]

目标:
  all         编译全部（默认）
  deps        pacman 安装系统依赖
  lua         编译 lua-5.5.1（ejoy skynet55 分支）
  drogon      编译 drogon-1.9.11 + trantor
  dlls        把运行期 DLL 收集到 bin/（需先编好主工程）

环境变量:
  PREFIX=$PREFIX
  BUILD_TYPE=$BUILD_TYPE
  JOBS=$JOBS
  WITH_POSTGRESQL=$WITH_POSTGRESQL
  WITH_BROTLI=$WITH_BROTLI
  WITH_YAML=$WITH_YAML
  SKIP_INSTALL=$SKIP_INSTALL

编完之后回项目根目录:
  cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build-mingw -j$JOBS
  ./3rdback/build-mingw.sh dlls
EOF
}

main() {
    case "${1:-all}" in
        -h|--help|help) usage ;;
        deps)
            install_deps
            ;;
        lua)
            build_lua
            log "完成。产物目录: $PREFIX"
            ;;
        drogon)
            build_drogon
            log "完成。产物目录: $PREFIX"
            ;;
        dlls)
            copy_dlls
            ;;
        all)
            build_lua
            build_drogon
            log "全部完成。产物目录: $PREFIX"
            ;;
        *) usage; die "未知目标: $1" ;;
    esac
}

main "$@"
