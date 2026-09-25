#!/bin/sh
set -eu

MAKE_EXECUTABLE="$1"
REDIS_SOURCE_DIR="$2"
REDISTIMESERIES_SOURCE_DIR="$3"
INSTALL_PREFIX="$4"
INSTALL_BINDIR="$5"
INSTALL_LIBDIR="$6"
INSTALL_SYSCONFDIR="$7"
CC_BIN="$8"
CXX_BIN="$9"
BUILD_TLS="${10}"
USE_SYSTEMD="${11}"
MALLOC="${12}"
OPENSSL_PREFIX="${13}"

MODULE_INSTALL_DIR="${INSTALL_LIBDIR}/redis/modules"
DEPS_PREFIX="${INSTALL_LIBDIR}/redis/deps"
REDIS_MAKE_BINDIR="${INSTALL_PREFIX}/bin"

if [ -n "${OPENSSL_PREFIX}" ]; then
  EFFECTIVE_OPENSSL_PREFIX="${OPENSSL_PREFIX}"
else
  # Prevent Redis/hiredis Makefiles from falling back to /usr/local defaults.
  EFFECTIVE_OPENSSL_PREFIX="${INSTALL_PREFIX}"
fi

export CC="${CC_BIN}"
export CXX="${CXX_BIN}"

# Redis 7 is built as the server only. RedisTimeSeries is built below as a
# standalone module from its own source tree; do not copy module sources into
# redis/modules as the Redis 8 Stack build does.
env \
  CC="${CC_BIN}" \
  CXX="${CXX_BIN}" \
  PREFIX="${INSTALL_PREFIX}" \
  prefix="${DEPS_PREFIX}" \
  exec_prefix="${DEPS_PREFIX}" \
  OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
  JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}" \
  BUILD_TLS="${BUILD_TLS}" \
  USE_SYSTEMD="${USE_SYSTEMD}" \
  MALLOC="${MALLOC}" \
  "${MAKE_EXECUTABLE}" -C "${REDIS_SOURCE_DIR}" all \
    PREFIX="${INSTALL_PREFIX}" \
    prefix="${DEPS_PREFIX}" \
    exec_prefix="${DEPS_PREFIX}" \
    OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
    JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}"

env \
  CC="${CC_BIN}" \
  CXX="${CXX_BIN}" \
  PREFIX="${INSTALL_PREFIX}" \
  prefix="${DEPS_PREFIX}" \
  exec_prefix="${DEPS_PREFIX}" \
  OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
  JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}" \
  BUILD_TLS="${BUILD_TLS}" \
  USE_SYSTEMD="${USE_SYSTEMD}" \
  MALLOC="${MALLOC}" \
  "${MAKE_EXECUTABLE}" -C "${REDIS_SOURCE_DIR}" install \
    PREFIX="${INSTALL_PREFIX}" \
    prefix="${DEPS_PREFIX}" \
    exec_prefix="${DEPS_PREFIX}" \
    OPENSSL_PREFIX="${EFFECTIVE_OPENSSL_PREFIX}" \
    JEMALLOC_CONFIGURE_OPTS="--prefix=${DEPS_PREFIX}"

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

# RedisTimeSeries 1.x builds its dependencies and module from its own Makefile.
env \
  CC="${CC_BIN}" \
  CXX="${CXX_BIN}" \
  "${MAKE_EXECUTABLE}" -C "${REDISTIMESERIES_SOURCE_DIR}" deps

env \
  CC="${CC_BIN}" \
  CXX="${CXX_BIN}" \
  "${MAKE_EXECUTABLE}" -C "${REDISTIMESERIES_SOURCE_DIR}" build

REDISTIMESERIES_MODULE=""
for module_file in "${REDISTIMESERIES_SOURCE_DIR}"/bin/*-release/redistimeseries.so; do
  if [ -f "${module_file}" ]; then
    REDISTIMESERIES_MODULE="${module_file}"
    break
  fi
done
if [ ! -f "${REDISTIMESERIES_MODULE}" ]; then
  echo "RedisTimeSeries module was not built under ${REDISTIMESERIES_SOURCE_DIR}/bin/*-release" >&2
  exit 1
fi

mkdir -p "${MODULE_INSTALL_DIR}"
cp -p "${REDISTIMESERIES_MODULE}" "${MODULE_INSTALL_DIR}/redistimeseries.so"

CONF_DIR="${INSTALL_SYSCONFDIR}/redis"
CONF_FILE="${CONF_DIR}/redis-full.conf"
BASE_CONF_FILE="${CONF_DIR}/redis.conf"
mkdir -p "${CONF_DIR}"

if [ -f "${REDIS_SOURCE_DIR}/redis.conf" ]; then
  cp "${REDIS_SOURCE_DIR}/redis.conf" "${BASE_CONF_FILE}"
fi

{
  if [ -f "${BASE_CONF_FILE}" ]; then
    printf 'include %s\n' "${BASE_CONF_FILE}"
  fi
  printf 'loadmodule %s\n' "${MODULE_INSTALL_DIR}/redistimeseries.so"
} > "${CONF_FILE}"
