#!/bin/sh
set -eu

TEMP_RUST_DIR="$1"
MAKE_EXECUTABLE="$2"
REDIS_SOURCE_DIR="$3"
INSTALL_PREFIX="$4"
INSTALL_BINDIR="$5"
INSTALL_LIBDIR="$6"
INSTALL_SYSCONFDIR="$7"
CC_BIN="$8"
CXX_BIN="$9"
BUILD_TLS="${10}"
USE_SYSTEMD="${11}"
MALLOC="${12}"
DISABLE_WERRORS="${13}"
OPENSSL_PREFIX="${14}"
CLEAN_TEMP_RUST="${15}"

RUSTUP_HOME="${TEMP_RUST_DIR}/rustup"
CARGO_HOME="${TEMP_RUST_DIR}/cargo"
RUST_BIN_DIR="${CARGO_HOME}/bin"
RUSTUP_INIT="${TEMP_RUST_DIR}/rustup-init"
RUST_TOOLCHAIN="1.92.0"
LLVM_DIR="${TEMP_RUST_DIR}/llvm"
MODULE_INSTALL_DIR="${INSTALL_LIBDIR}/redis/modules"
DEPS_PREFIX="${INSTALL_LIBDIR}/redis/deps"
PYTHON_VENV="${TEMP_RUST_DIR}/python-venv"
REDIS_MAKE_BINDIR="${INSTALL_PREFIX}/bin"

if [ -n "${OPENSSL_PREFIX}" ]; then
  EFFECTIVE_OPENSSL_PREFIX="${OPENSSL_PREFIX}"
else
  # Prevent Redis/hiredis Makefiles from falling back to /usr/local defaults.
  EFFECTIVE_OPENSSL_PREFIX="${INSTALL_PREFIX}"
fi

mkdir -p "${TEMP_RUST_DIR}" "${RUSTUP_HOME}" "${CARGO_HOME}"

export RUSTUP_HOME
export CARGO_HOME
export CC="${CC_BIN}"
export CXX="${CXX_BIN}"

# Several Redis modules call python3 from their own Makefiles. Use an isolated
# venv under the temporary tool directory so the build does not depend on user
# shell activation or write Python state outside the install tree.
if [ ! -x "${PYTHON_VENV}/bin/python3" ] || ! "${PYTHON_VENV}/bin/python3" -m pip --version >/dev/null 2>&1; then
  HOST_PYTHON3="$(command -v python3 || true)"
  if [ -z "${HOST_PYTHON3}" ]; then
    echo "python3 is required to build Redis modules" >&2
    exit 1
  fi
  rm -rf "${PYTHON_VENV}"
  "${HOST_PYTHON3}" -m venv "${PYTHON_VENV}"
fi

PYTHON3_BIN="${PYTHON_VENV}/bin/python3"
PYTHON_BIN="${PYTHON_VENV}/bin/python"

export VIRTUAL_ENV="${PYTHON_VENV}"
export PYTHON="${PYTHON_BIN}"
export PYTHON3="${PYTHON3_BIN}"
export USER_MYPY="${PYTHON3_BIN}"
export PATH="${PYTHON_VENV}/bin:${RUST_BIN_DIR}:${PATH}"

# RedisJSON depends on bindgen through redismodule-rs, so libclang must be
# discoverable at build time. Prefer a system libclang and fall back to a
# temporary LLVM download for supported Linux architectures.
find_libclang_dir() {
  for dir in \
    "${LLVM_DIR}/lib" \
    "${LLVM_DIR}/lib64" \
    /usr/lib64 \
    /usr/lib \
    /lib64 \
    /lib
  do
    if ls "${dir}"/libclang.so* >/dev/null 2>&1 || ls "${dir}"/libclang-*.so* >/dev/null 2>&1; then
      printf '%s\n' "${dir}"
      return 0
    fi
  done
  return 1
}

if ! LIBCLANG_DIR="$(find_libclang_dir)"; then
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)
      LLVM_ARCHIVE="clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-18.04.tar.xz"
      ;;
    Linux-aarch64)
      LLVM_ARCHIVE="clang+llvm-18.1.8-aarch64-linux-gnu.tar.xz"
      ;;
    *)
      echo "libclang is required to build RedisJSON, and automatic LLVM download is not configured for $(uname -s)-$(uname -m)" >&2
      exit 1
      ;;
  esac

  LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/${LLVM_ARCHIVE}"
  LLVM_TARBALL="${TEMP_RUST_DIR}/${LLVM_ARCHIVE}"
  rm -rf "${LLVM_DIR}"
  curl -fsSL "${LLVM_URL}" -o "${LLVM_TARBALL}"
  mkdir -p "${LLVM_DIR}"
  tar -xJf "${LLVM_TARBALL}" -C "${LLVM_DIR}" --strip-components=1
  rm -f "${LLVM_TARBALL}"
  LIBCLANG_DIR="$(find_libclang_dir)"
fi

export LIBCLANG_PATH="${LIBCLANG_DIR}"
export CLANG_PATH="${LLVM_DIR}/bin/clang"
# The upstream LLVM binary archive for old Linux targets can reference
# libtinfo.so.5. AlmaLinux commonly ships libtinfo.so.6, which is compatible for
# this use case, so create a local symlink inside the temporary LLVM tree.
if [ ! -e "${LIBCLANG_DIR}/libtinfo.so.5" ]; then
  for tinfo in /usr/lib64/libtinfo.so.6 /usr/lib/libtinfo.so.6 /lib64/libtinfo.so.6 /lib/libtinfo.so.6; do
    if [ -e "${tinfo}" ]; then
      ln -s "${tinfo}" "${LIBCLANG_DIR}/libtinfo.so.5"
      break
    fi
  done
fi
if [ -x "${LLVM_DIR}/bin/clang" ]; then
  export PATH="${LLVM_DIR}/bin:${PATH}"
fi
export LD_LIBRARY_PATH="${LIBCLANG_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

# Install a pinned Rust toolchain only when the temporary tool directory does not
# already contain the requested version. The built Redis/RedisJSON artifacts do
# not require cargo or rustc at runtime.
if [ ! -x "${RUST_BIN_DIR}/cargo" ] || [ ! -x "${RUST_BIN_DIR}/rustc" ] || ! "${RUST_BIN_DIR}/rustc" --version | grep -q " ${RUST_TOOLCHAIN} "; then
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)
      RUSTUP_URL="https://static.rust-lang.org/rustup/dist/x86_64-unknown-linux-gnu/rustup-init"
      ;;
    Linux-aarch64)
      RUSTUP_URL="https://static.rust-lang.org/rustup/dist/aarch64-unknown-linux-gnu/rustup-init"
      ;;
    Darwin-x86_64)
      RUSTUP_URL="https://static.rust-lang.org/rustup/dist/x86_64-apple-darwin/rustup-init"
      ;;
    Darwin-arm64)
      RUSTUP_URL="https://static.rust-lang.org/rustup/dist/aarch64-apple-darwin/rustup-init"
      ;;
    *)
      echo "unsupported platform: $(uname -s)-$(uname -m)" >&2
      exit 1
      ;;
  esac

  curl -fsSL "${RUSTUP_URL}" -o "${RUSTUP_INIT}"
  chmod +x "${RUSTUP_INIT}"
  "${RUSTUP_INIT}" -y --profile minimal --no-modify-path --default-toolchain "${RUST_TOOLCHAIN}"
fi

"${RUST_BIN_DIR}/cargo" --version
"${RUST_BIN_DIR}/rustc" --version

# Redis vendors a reduced xxHash tree without tests/, but xxHash's clean target
# still descends into those directories when Redis decides to rebuild deps.
XXHASH_DIR="${REDIS_SOURCE_DIR}/deps/xxhash"
if [ -f "${XXHASH_DIR}/Makefile" ] && [ ! -d "${XXHASH_DIR}/tests" ]; then
  mkdir -p "${XXHASH_DIR}/tests/bench" "${XXHASH_DIR}/tests/collisions"
  for makefile in \
    "${XXHASH_DIR}/tests/Makefile" \
    "${XXHASH_DIR}/tests/bench/Makefile" \
    "${XXHASH_DIR}/tests/collisions/Makefile"
  do
    {
      echo "clean:"
      printf "\t@:\n"
    } > "${makefile}"
  done
fi

# Pass install/dependency prefixes both through the environment and as make
# variables because Redis and the modules consult a mix of both. prefix and
# exec_prefix keep bundled deps away from /usr/local.
env \
  CC="${CC_BIN}" \
  CXX="${CXX_BIN}" \
  PREFIX="${INSTALL_PREFIX}" \
  INSTALL_DIR="${MODULE_INSTALL_DIR}" \
  prefix="${DEPS_PREFIX}" \
  exec_prefix="${DEPS_PREFIX}" \
  OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
  JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}" \
  LIBCLANG_PATH="${LIBCLANG_PATH}" \
  CLANG_PATH="${CLANG_PATH}" \
  LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
  CARGO_TERM_VERBOSE=true \
  CARGO_TERM_COLOR=never \
  RUST_BACKTRACE=1 \
  PYTHON="${PYTHON_BIN}" \
  PYTHON3="${PYTHON3_BIN}" \
  USER_MYPY="${PYTHON3_BIN}" \
  IGNORE_MISSING_DEPS=1 \
  VERBOSE=0 \
  BUILD_WITH_MODULES=yes \
  BUILD_TLS="${BUILD_TLS}" \
  USE_SYSTEMD="${USE_SYSTEMD}" \
  MALLOC="${MALLOC}" \
  INSTALL_RUST_TOOLCHAIN=no \
  DISABLE_WERRORS="${DISABLE_WERRORS}" \
  "${MAKE_EXECUTABLE}" -C "${REDIS_SOURCE_DIR}" all \
    PREFIX="${INSTALL_PREFIX}" \
    INSTALL_DIR="${MODULE_INSTALL_DIR}" \
    prefix="${DEPS_PREFIX}" \
    exec_prefix="${DEPS_PREFIX}" \
    OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
    JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}" \
    LIBCLANG_PATH="${LIBCLANG_PATH}" \
    CLANG_PATH="${CLANG_PATH}" \
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
    CARGO_TERM_VERBOSE=true \
    CARGO_TERM_COLOR=never \
    RUST_BACKTRACE=1 \
    PYTHON="${PYTHON_BIN}" \
    PYTHON3="${PYTHON3_BIN}" \
    USER_MYPY="${PYTHON3_BIN}" \
    IGNORE_MISSING_DEPS=1

# Run install as a separate Makefile phase so Redis programs and module shared
# objects land under the same CMAKE_INSTALL_PREFIX-controlled tree.
env \
  CC="${CC_BIN}" \
  CXX="${CXX_BIN}" \
  PREFIX="${INSTALL_PREFIX}" \
  INSTALL_DIR="${MODULE_INSTALL_DIR}" \
  prefix="${DEPS_PREFIX}" \
  exec_prefix="${DEPS_PREFIX}" \
  OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
  JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}" \
  LIBCLANG_PATH="${LIBCLANG_PATH}" \
  CLANG_PATH="${CLANG_PATH}" \
  LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
  CARGO_TERM_VERBOSE=true \
  CARGO_TERM_COLOR=never \
  RUST_BACKTRACE=1 \
  PYTHON="${PYTHON_BIN}" \
  PYTHON3="${PYTHON3_BIN}" \
  USER_MYPY="${PYTHON3_BIN}" \
  IGNORE_MISSING_DEPS=1 \
  VERBOSE=0 \
  BUILD_WITH_MODULES=yes \
  BUILD_TLS="${BUILD_TLS}" \
  USE_SYSTEMD="${USE_SYSTEMD}" \
  MALLOC="${MALLOC}" \
  INSTALL_RUST_TOOLCHAIN=no \
  DISABLE_WERRORS="${DISABLE_WERRORS}" \
  "${MAKE_EXECUTABLE}" -C "${REDIS_SOURCE_DIR}" install \
    PREFIX="${INSTALL_PREFIX}" \
    INSTALL_DIR="${MODULE_INSTALL_DIR}" \
    prefix="${DEPS_PREFIX}" \
    exec_prefix="${DEPS_PREFIX}" \
    OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
    JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}" \
    LIBCLANG_PATH="${LIBCLANG_PATH}" \
    CLANG_PATH="${CLANG_PATH}" \
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
    CARGO_TERM_VERBOSE=true \
    CARGO_TERM_COLOR=never \
    RUST_BACKTRACE=1 \
    PYTHON="${PYTHON_BIN}" \
    PYTHON3="${PYTHON3_BIN}" \
    USER_MYPY="${PYTHON3_BIN}" \
    IGNORE_MISSING_DEPS=1

# Redis' upstream Makefile installs executables under PREFIX/bin. Honor
# CMAKE_INSTALL_BINDIR by copying those executables to the requested bindir when
# it differs from PREFIX/bin.
if [ "${INSTALL_BINDIR}" != "${REDIS_MAKE_BINDIR}" ]; then
  mkdir -p "${INSTALL_BINDIR}"
  for program in redis-server redis-cli redis-benchmark redis-sentinel redis-check-rdb redis-check-aof; do
    if [ -f "${REDIS_MAKE_BINDIR}/${program}" ]; then
      cp -p "${REDIS_MAKE_BINDIR}/${program}" "${INSTALL_BINDIR}/${program}"
    fi
  done
fi

# Fail fast if module installation silently regresses. RedisJSON installs its
# module as rejson.so, not redisjson.so.
if [ ! -f "${INSTALL_LIBDIR}/redis/modules/rejson.so" ]; then
  echo "RedisJSON module was not installed: ${INSTALL_LIBDIR}/redis/modules/rejson.so" >&2
  exit 1
fi

if [ ! -f "${INSTALL_LIBDIR}/redis/modules/redisearch.so" ]; then
  echo "RediSearch module was not installed: ${INSTALL_LIBDIR}/redis/modules/redisearch.so" >&2
  exit 1
fi

CONF_DIR="${INSTALL_SYSCONFDIR}/redis"
CONF_FILE="${CONF_DIR}/redis-full.conf"
BASE_CONF_FILE="${CONF_DIR}/redis.conf"
mkdir -p "${CONF_DIR}"

# Install the upstream base redis.conf unchanged, then rewrite redis-full.conf
# so its include and loadmodule lines refer to absolute install paths. This lets
# redis-server be launched from any working directory.
if [ -f "${REDIS_SOURCE_DIR}/redis.conf" ]; then
  cp "${REDIS_SOURCE_DIR}/redis.conf" "${BASE_CONF_FILE}"
fi

if [ -f "${REDIS_SOURCE_DIR}/redis-full.conf" ]; then
  sed \
    -e "s|^[[:space:]]*include[[:space:]]\+redis\.conf[[:space:]]*$|include ${BASE_CONF_FILE}|" \
    -e "s|^[[:space:]]*loadmodule[[:space:]].*redisbloom.*$|loadmodule ${MODULE_INSTALL_DIR}/redisbloom.so|" \
    -e "s|^[[:space:]]*loadmodule[[:space:]].*redisearch.*$|loadmodule ${MODULE_INSTALL_DIR}/redisearch.so|" \
    -e "s|^[[:space:]]*loadmodule[[:space:]].*redisjson.*$|loadmodule ${MODULE_INSTALL_DIR}/rejson.so|" \
    -e "s|^[[:space:]]*loadmodule[[:space:]].*rejson.*$|loadmodule ${MODULE_INSTALL_DIR}/rejson.so|" \
    -e "s|^[[:space:]]*loadmodule[[:space:]].*redistimeseries.*$|loadmodule ${MODULE_INSTALL_DIR}/redistimeseries.so|" \
    "${REDIS_SOURCE_DIR}/redis-full.conf" > "${CONF_FILE}"
else
  {
    printf 'loadmodule %s\n' "${MODULE_INSTALL_DIR}/redisbloom.so"
    printf 'loadmodule %s\n' "${MODULE_INSTALL_DIR}/redisearch.so"
    printf 'loadmodule %s\n' "${MODULE_INSTALL_DIR}/rejson.so"
    printf 'loadmodule %s\n' "${MODULE_INSTALL_DIR}/redistimeseries.so"
  } > "${CONF_FILE}"
fi

# The temporary Rust/LLVM/Python toolchain is a build-only dependency. Keep it
# only when the caller wants faster incremental rebuilds or post-build debugging.
if [ "${CLEAN_TEMP_RUST}" = "yes" ]; then
  rm -rf "${TEMP_RUST_DIR}"
fi
