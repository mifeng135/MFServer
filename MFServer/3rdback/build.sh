#!/usr/bin/env bash
# 编译 3rdback 依赖：jemalloc / lua / drogon(PostgreSQL)
#
# 用法:
#   ./build.sh              # 编译全部
#   ./build.sh deps         # 安装系统依赖
#   ./build.sh drogon       # 只编译 drogon（默认开启 PostgreSQL）
#   ./build.sh jemalloc|lua
#
# 环境变量:
#   PREFIX                  安装前缀
#                           macOS 默认 3rdback/buildout
#                           Linux 默认 /usr/local
#   BUILD_TYPE=Release
#   JOBS=8
#   WITH_POSTGRESQL=ON      drogon 是否启用 PostgreSQL
#   WITH_SSL=auto           auto|openssl|none
#                           macOS/Xcode 默认 none，Linux 默认 auto
#   WITH_BROTLI=OFF         macOS/Xcode 默认 OFF，Linux 默认 ON
#   SKIP_INSTALL=0          1 则只编译不安装

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OS="$(uname -s)"

if [[ "$OS" == Darwin ]]; then
    PREFIX="${PREFIX:-$ROOT/buildout}"
else
    PREFIX="${PREFIX:-/usr/local}"
fi
BUILD_TYPE="${BUILD_TYPE:-Release}"
WITH_POSTGRESQL="${WITH_POSTGRESQL:-ON}"
SKIP_INSTALL="${SKIP_INSTALL:-0}"

# Xcode 工程不链 OpenSSL / Brotli
if [[ "$OS" == Darwin ]]; then
    WITH_SSL="${WITH_SSL:-none}"
    WITH_BROTLI="${WITH_BROTLI:-OFF}"
else
    WITH_SSL="${WITH_SSL:-auto}"
    WITH_BROTLI="${WITH_BROTLI:-ON}"
fi

case "$OS" in
    Darwin) JOBS="${JOBS:-$(sysctl -n hw.ncpu)}" ;;
    *)      JOBS="${JOBS:-$(nproc)}" ;;
esac

# Linux 默认动态库，macOS 默认静态库（jemalloc 只编 shared，不编 .a）
if [[ "$OS" == Darwin ]]; then
    SHARED_LIBS="${SHARED_LIBS:-OFF}"
else
    SHARED_LIBS="${SHARED_LIBS:-ON}"
fi

log()  { printf '\n\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "未找到命令: $1"
}

run_install() {
    if [[ "$SKIP_INSTALL" == "1" ]]; then
        warn "SKIP_INSTALL=1，跳过安装"
        return 0
    fi
    if [[ -w "$PREFIX" ]] || mkdir -p "$PREFIX" 2>/dev/null; then
        "$@"
    else
        sudo "$@"
    fi
}

# macOS 自带 unzip 遇到未标 UTF-8 的中文文件名会报 Illegal byte sequence。
extract_zip() {
    local zip_path="$1"
    local dest_dir="$2"
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$zip_path" "$dest_dir" <<'PY'
import os, stat, sys, zipfile
zip_path, dest_dir = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path) as zf:
    for info in zf.infolist():
        dest = zf.extract(info, dest_dir)
        if info.is_dir():
            continue
        perm = (info.external_attr >> 16) & 0o777
        if dest.endswith(".sh") or dest.endswith(".py"):
            perm = perm or 0o755
            perm |= stat.S_IXUSR
        if perm:
            os.chmod(dest, perm)
PY
        return 0
    fi
    if [[ "$OS" == Darwin ]] && command -v ditto >/dev/null 2>&1; then
        ditto -x -k "$zip_path" "$dest_dir"
        return 0
    fi
    LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 unzip -q "$zip_path" -d "$dest_dir"
}

ensure_src() {
    local zip_name="$1"
    local dir_name="$2"
    local dest="$ROOT/$dir_name"
    if [[ -f "$dest/CMakeLists.txt" || -f "$dest/configure" || -f "$dest/autogen.sh" ]]; then
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

# Homebrew 的 brew --prefix 可能因联网失败而报错，优先用本地路径判断。
libpq_candidates() {
    local p
    if [[ "$OS" == Darwin ]]; then
        printf '%s\n' /opt/homebrew/opt/libpq /usr/local/opt/libpq
        if command -v brew >/dev/null 2>&1; then
            p="$(brew --prefix libpq 2>/dev/null || true)"
            [[ -n "$p" ]] && printf '%s\n' "$p"
        fi
    fi
    if command -v pg_config >/dev/null 2>&1; then
        p="$(pg_config --includedir 2>/dev/null || true)"
        if [[ -n "$p" ]]; then
            printf '%s\n' "$(dirname "$p")"
        fi
    fi
}

CMAKE_PREFIX_PATHS=()

add_cmake_prefix() {
    local p="$1"
    local x
    for x in "${CMAKE_PREFIX_PATHS[@]+"${CMAKE_PREFIX_PATHS[@]}"}"; do
        [[ "$x" == "$p" ]] && return 0
    done
    CMAKE_PREFIX_PATHS+=("$p")
}

apply_cmake_prefixes() {
    if [[ ${#CMAKE_PREFIX_PATHS[@]} -eq 0 ]]; then
        return 0
    fi
    local joined
    joined="$(IFS=';'; echo "${CMAKE_PREFIX_PATHS[*]}")"
    cmake_args+=("-DCMAKE_PREFIX_PATH=$joined")
}

detect_libpq() {
    if [[ "$WITH_POSTGRESQL" != "ON" ]]; then
        return 0
    fi

    local p
    while IFS= read -r p; do
        [[ -n "$p" ]] || continue
        if [[ -f "$p/include/libpq-fe.h" || -f "$p/include/postgresql/libpq-fe.h" ]]; then
            add_cmake_prefix "$p"
            log "使用 libpq: $p"
            return 0
        fi
    done < <(libpq_candidates)

    if [[ -f /usr/include/postgresql/libpq-fe.h ]] \
        || [[ -f /usr/include/libpq-fe.h ]]; then
        return 0
    fi

    if [[ "$OS" == Darwin ]]; then
        die "未找到 libpq。请先执行: brew install libpq"
    fi
    die "未找到 libpq。请先执行: sudo apt install libpq-dev"
}

brew_pkg() {
    HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALLED_DEPENDENTS_CHECK=1 \
        brew install "$@"
}

jsoncpp_candidates() {
    local p
    printf '%s\n' "$PREFIX"
    if [[ "$OS" == Darwin ]]; then
        printf '%s\n' /opt/homebrew/opt/jsoncpp /usr/local/opt/jsoncpp /opt/homebrew /usr/local
        if command -v brew >/dev/null 2>&1; then
            p="$(HOMEBREW_NO_AUTO_UPDATE=1 brew --prefix jsoncpp 2>/dev/null || true)"
            [[ -n "$p" ]] && printf '%s\n' "$p"
            p="$(HOMEBREW_NO_AUTO_UPDATE=1 brew --prefix 2>/dev/null || true)"
            [[ -n "$p" ]] && printf '%s\n' "$p"
        fi
    fi
}

has_jsoncpp() {
    local p="$1"
    [[ -f "$p/include/json/json.h" || -f "$p/include/jsoncpp/json/json.h" ]]
}

# 优先用 3rdback/jsoncpp-1.9.6.tar.gz，不走 brew（sequoia 会报 unknown or unsupported macOS version）。
build_jsoncpp_from_source() {
    local ver="1.9.6"
    local src="$ROOT/jsoncpp-$ver"
    local tarball="$ROOT/jsoncpp-$ver.tar.gz"
    local url="https://github.com/open-source-parsers/jsoncpp/archive/$ver.tar.gz"

    if [[ ! -d "$src" ]]; then
        if [[ ! -f "$tarball" ]]; then
            need_cmd curl
            log "下载 jsoncpp $ver"
            curl -L --fail --retry 3 -o "$tarball" "$url"
        else
            log "使用本地源码包 $tarball"
        fi
        tar -xzf "$tarball" -C "$ROOT"
    fi
    [[ -d "$src" ]] || die "jsoncpp 源码解压失败"

    log "源码编译 jsoncpp -> $PREFIX"
    cmake -S "$src" -B "$src/build" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS="$SHARED_LIBS" \
        -DJSONCPP_WITH_TESTS=OFF \
        -DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF \
        -DJSONCPP_WITH_EXAMPLE=OFF
    cmake --build "$src/build" --config "$BUILD_TYPE" -j"$JOBS"
    run_install cmake --install "$src/build" --config "$BUILD_TYPE"

    has_jsoncpp "$PREFIX" || die "jsoncpp 安装后仍未在 $PREFIX 找到头文件"
    add_cmake_prefix "$PREFIX"
    log "使用 jsoncpp: $PREFIX"
}

detect_jsoncpp() {
    local p
    while IFS= read -r p; do
        [[ -n "$p" ]] || continue
        if has_jsoncpp "$p"; then
            add_cmake_prefix "$p"
            log "使用 jsoncpp: $p"
            return 0
        fi
    done < <(jsoncpp_candidates)

    if [[ -f /usr/include/jsoncpp/json/json.h || -f /usr/include/json/json.h ]]; then
        return 0
    fi

    build_jsoncpp_from_source
}

tls_cmake_args() {
    local mode="$WITH_SSL"
    if [[ "$mode" == auto && "$OS" == Darwin ]]; then
        mode=none
    fi
    case "$mode" in
        none|OFF|off)
            printf '%s' '-DTRANTOR_USE_TLS=none'
            ;;
        openssl)
            printf '%s' '-DTRANTOR_USE_TLS=openssl'
            ;;
        auto)
            ;;
        *)
            die "WITH_SSL 只能是 auto|openssl|none"
            ;;
    esac
}

# ---------- 系统依赖 ----------

install_deps() {
    log "安装系统依赖 ($OS)"
    if [[ "$OS" == Darwin ]]; then
        need_cmd brew
        brew_pkg cmake autoconf libpq || warn "brew 安装部分依赖失败，可稍后由源码补齐 jsoncpp"
        return 0
    fi
    need_cmd apt-get
    sudo apt-get update
    sudo apt-get install -y \
        cmake build-essential gcc g++ autoconf unzip \
        libjsoncpp-dev uuid-dev zlib1g-dev \
        openssl libssl-dev libbrotli-dev \
        libpq-dev libreadline-dev
}

# ---------- jemalloc ----------

build_jemalloc() {
    ensure_src "jemalloc-5.3.0.zip" "jemalloc-5.3.0"
    need_cmd autoconf
    local src="$ROOT/jemalloc-5.3.0"
    log "编译 jemalloc -> $PREFIX"

    (
        cd "$src"
        # zip 解压常丢掉 +x；configure 会直接执行 *.sh 生成 jemalloc.h
        find . -name '*.sh' -exec chmod +x {} +
        chmod +x configure 2>/dev/null || true
        # 上次失败会留下 0 字节头文件，必须删掉再重新 configure
        rm -f include/jemalloc/jemalloc.h \
              include/jemalloc/jemalloc_rename.h \
              include/jemalloc/jemalloc_mangle.h \
              include/jemalloc/jemalloc_mangle_jet.h
        if [[ ! -f configure ]]; then
            bash autogen.sh
        fi
        sh configure \
            --prefix="$PREFIX" \
            --enable-shared \
            --disable-static \
            --enable-prof \
            --enable-stats
        [[ -s include/jemalloc/jemalloc.h ]] || die "jemalloc.h 生成失败，请检查 *.sh 是否可执行"
        make -j"$JOBS"
        run_install make install
    )
}

# ---------- lua ----------

build_lua() {
    ensure_src "lua-5.5.1.zip" "lua-5.5.1"
    need_cmd cmake
    local src="$ROOT/lua-5.5.1"
    local build="$src/build"
    log "编译 lua-5.5.1"

    cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$build" --config "$BUILD_TYPE" -j"$JOBS"

    run_install mkdir -p "$PREFIX/lib" "$PREFIX/include" "$PREFIX/bin"
    if [[ "$SHARED_LIBS" == ON ]]; then
        if [[ "$OS" == Darwin ]]; then
            run_install cp -f "$build/liblua.dylib" "$PREFIX/lib/"
        else
            run_install cp -f "$build/liblua.so" "$PREFIX/lib/"
        fi
    fi
    [[ -f "$build/liblua.a" ]] && run_install cp -f "$build/liblua.a" "$PREFIX/lib/"
    run_install cp -f "$src/lua.h" "$src/luaconf.h" "$src/lauxlib.h" "$src/lualib.h" "$src/lua.hpp" "$PREFIX/include/"
    [[ -f "$build/lua" ]] && run_install cp -f "$build/lua" "$PREFIX/bin/"
}

# ---------- drogon + PostgreSQL ----------

build_drogon() {
    local src="$ROOT/drogon-1.9.11"
    [[ -d "$src" ]] || ensure_src "drogon-1.9.11.zip" "drogon-1.9.11"
    need_cmd cmake
    CMAKE_PREFIX_PATHS=()
    detect_jsoncpp
    detect_libpq

    local build="$src/build"
    log "编译 drogon-1.9.11 (ORM=$WITH_POSTGRESQL PostgreSQL=$WITH_POSTGRESQL SHARED=$SHARED_LIBS)"

    local cmake_args=(
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
        -DBUILD_SHARED_LIBS="$SHARED_LIBS"
        -DBUILD_CTL=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_MYSQL=OFF
        -DBUILD_SQLITE=OFF
        -DBUILD_REDIS=OFF
    )
    apply_cmake_prefixes

    if [[ "$WITH_POSTGRESQL" == ON ]]; then
        cmake_args+=(-DBUILD_ORM=ON -DBUILD_POSTGRESQL=ON)
    else
        cmake_args+=(-DBUILD_ORM=OFF -DBUILD_POSTGRESQL=OFF)
    fi

    local tls
    tls="$(tls_cmake_args)"
    [[ -n "$tls" ]] && cmake_args+=("$tls")
    if [[ "$tls" == *none* ]]; then
        log "关闭 OpenSSL (TRANTOR_USE_TLS=none)，Xcode 无需链 libssl"
    fi

    case "$WITH_BROTLI" in
        ON|on|1)
            cmake_args+=(-DBUILD_BROTLI=ON)
            log "启用 Brotli"
            ;;
        *)
            cmake_args+=(-DBUILD_BROTLI=OFF)
            log "关闭 Brotli (BUILD_BROTLI=OFF)，Xcode 无需链 libbrotli"
            ;;
    esac

    cmake -S "$src" -B "$build" "${cmake_args[@]}"

    if [[ "$WITH_POSTGRESQL" == ON ]]; then
        if ! grep -qE 'USE_POSTGRESQL[[:space:]]+1' "$build/drogon/config.h"; then
            die "cmake 未启用 PostgreSQL。请确认 libpq 已安装，并检查上面的 cmake 日志是否出现 libpq inc path"
        fi
        log "已确认 drogon 启用 PostgreSQL"
    fi

    cmake --build "$build" --config "$BUILD_TYPE" -j"$JOBS"
    run_install cmake --install "$build" --config "$BUILD_TYPE"
}

usage() {
    cat <<EOF
用法: $0 [目标]

目标:
  all         编译全部（默认）
  deps        安装系统依赖 (apt / brew)
  jemalloc    编译 jemalloc-5.3.0
  lua         编译 lua-5.5.1
  drogon      编译 drogon-1.9.11，默认开启 PostgreSQL

环境变量:
  PREFIX=$PREFIX
  BUILD_TYPE=$BUILD_TYPE
  JOBS=$JOBS
  SHARED_LIBS=$SHARED_LIBS
  WITH_POSTGRESQL=$WITH_POSTGRESQL
  WITH_SSL=$WITH_SSL
  WITH_BROTLI=$WITH_BROTLI
  SKIP_INSTALL=$SKIP_INSTALL

示例:
  ./build.sh deps                 # 先装系统依赖
  ./build.sh                      # 编译并安装（Linux -> /usr/local；macOS -> 3rdback/buildout）
  ./build.sh drogon
  WITH_POSTGRESQL=OFF ./build.sh drogon
  PREFIX=/opt/mf ./build.sh all   # 编译并安装到指定目录
  SKIP_INSTALL=1 ./build.sh       # 只编译不安装
EOF
}

prepare_prefix() {
    mkdir -p "$PREFIX/lib" "$PREFIX/include" "$PREFIX/bin"
}

# Linux 装到 /usr/local 后必须刷新缓存，否则运行时报 liblua.so: cannot open shared object file
refresh_linux_loader() {
    if [[ "$OS" == Darwin || "$SKIP_INSTALL" == "1" ]]; then
        return 0
    fi
    local libdir="$PREFIX/lib"
    [[ -d "$libdir" ]] || return 0
    if [[ "$libdir" != /usr/lib && "$libdir" != /usr/local/lib && "$libdir" != /lib && "$libdir" != /lib64 ]]; then
        echo "$libdir" | sudo tee /etc/ld.so.conf.d/mfserver.conf >/dev/null
    fi
    log "刷新动态库缓存 (ldconfig)"
    if [[ -w /etc/ld.so.cache ]]; then
        ldconfig
    else
        sudo ldconfig
    fi
}

main() {
    local target="${1:-all}"
    case "$target" in
        -h|--help|help) usage ;;
        deps)           install_deps ;;
        jemalloc)
            prepare_prefix
            build_jemalloc
            refresh_linux_loader
            log "完成。产物目录: $PREFIX"
            ;;
        lua)
            prepare_prefix
            build_lua
            refresh_linux_loader
            log "完成。产物目录: $PREFIX"
            ;;
        drogon)
            prepare_prefix
            build_drogon
            refresh_linux_loader
            log "完成。产物目录: $PREFIX"
            ;;
        all)
            prepare_prefix
            build_jemalloc
            build_lua
            build_drogon
            refresh_linux_loader
            log "全部完成。产物目录: $PREFIX"
            ;;
        *)
            usage
            die "未知目标: $target"
            ;;
    esac
}

main "$@"
