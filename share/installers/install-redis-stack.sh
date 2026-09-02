#!/bin/sh
set -eu

ACTION="${1:-install}"
REDIS_PACKAGE="${REDIS_PACKAGE:-redis}"
REDIS_VERSION="${REDIS_VERSION:-8.2.7}"
SUDO="${SUDO:-sudo}"
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
fi

usage() {
  cat <<EOF
Usage: $0 [install|upgrade|uninstall|repo-only]

Install or update Redis with Redis Stack modules with the host package manager.

Debian/Ubuntu systems use apt-get. RHEL-family systems use dnf, or yum when
dnf is not available. The script writes package repositories and packages into
system-managed locations such as /etc and /usr, so root privileges or sudo are
required.

Actions:
  install    Register the Redis package repository and install the package.
  upgrade    Register the Redis package repository and upgrade the package.
  uninstall  Remove the package. Repository files, configuration, and data are kept.
  repo-only  Register the Redis package repository only.

Environment:
  REDIS_PACKAGE  Package name to install. Default: ${REDIS_PACKAGE}
                 The Redis 8 redis package includes Redis server and Redis
                 Stack modules, but not RedisInsight. Use redis-stack only when
                 your repository provides it and you want RedisInsight included.
  REDIS_VERSION  Redis version to install. Default: ${REDIS_VERSION}
                 Set to latest to install or upgrade to the repository default.
  SUDO           Privilege wrapper. Default: sudo, or empty when run as root.

Examples:
  $0 install
  REDIS_VERSION=latest $0 install
  REDIS_PACKAGE=redis-stack $0 install
  $0 upgrade
  $0 uninstall
EOF
}

case "${ACTION}" in
  -h|--help)
    usage
    exit 0
    ;;
esac

if [ "${ACTION}" != "install" ] && [ "${ACTION}" != "upgrade" ] && [ "${ACTION}" != "uninstall" ] && [ "${ACTION}" != "repo-only" ]; then
  usage >&2
  exit 2
fi

if [ -r /etc/os-release ]; then
  . /etc/os-release
else
  echo "/etc/os-release is required" >&2
  exit 1
fi

run() {
  echo "+ $*"
  "$@"
}

is_latest_version() {
  [ "${REDIS_VERSION}" = "latest" ]
}

require_redis_package_for_pinned_version() {
  if ! is_latest_version && [ "${REDIS_PACKAGE}" != "redis" ]; then
    echo "REDIS_VERSION=${REDIS_VERSION} pinning is supported only with REDIS_PACKAGE=redis." >&2
    echo "Set REDIS_VERSION=latest when using REDIS_PACKAGE=${REDIS_PACKAGE}." >&2
    exit 1
  fi
}

apt_redis_version() {
  printf '6:%s-1rl1~%s1' "${REDIS_VERSION}" "${codename}"
}

apt_pinned_packages() {
  # The official APT instructions pin these packages together when installing
  # an earlier Redis version so the server, sentinel, and tools stay in sync.
  printf '%s\n' redis redis-server redis-sentinel redis-tools
}

apt_version_is_available() {
  package="$1"
  version="$2"
  apt-cache madison "${package}" | awk '{ print $3 }' | grep -qx "${version}"
}

require_apt_pinned_version() {
  version="$1"
  missing=""
  for package in $(apt_pinned_packages); do
    if ! apt_version_is_available "${package}" "${version}"; then
      missing="${missing} ${package}"
    fi
  done
  if [ -n "${missing}" ]; then
    echo "Redis package version ${version} is not available for:${missing}" >&2
    echo "The Redis APT repository publishes versions per distribution codename (${codename})." >&2
    echo "Available redis package versions include:" >&2
    apt-cache madison redis | awk '{ print "  " $3 }' | head -n 20 >&2
    exit 1
  fi
}

install_deb_repo() {
  # Follow the official APT flow: install repository tooling, import the Redis
  # signing key into /usr/share/keyrings, add the packages.redis.io source list
  # for the distribution codename, then refresh the package index.
  run ${SUDO} apt-get update
  run ${SUDO} apt-get install -y lsb-release curl gpg ca-certificates
  run ${SUDO} mkdir -p /usr/share/keyrings
  curl -fsSL https://packages.redis.io/gpg | run ${SUDO} gpg --dearmor --batch --yes -o /usr/share/keyrings/redis-archive-keyring.gpg
  run ${SUDO} chmod 644 /usr/share/keyrings/redis-archive-keyring.gpg
  codename="$(. /etc/os-release && printf '%s' "${VERSION_CODENAME:-}")"
  if [ -z "${codename}" ] && command -v lsb_release >/dev/null 2>&1; then
    codename="$(lsb_release -cs)"
  fi
  if [ -z "${codename}" ]; then
    echo "Could not determine Debian/Ubuntu codename" >&2
    exit 1
  fi
  codename="$(printf '%s' "${codename}" | tr '[:upper:]' '[:lower:]')"
  echo "deb [signed-by=/usr/share/keyrings/redis-archive-keyring.gpg] https://packages.redis.io/deb ${codename} main" |
    run ${SUDO} tee /etc/apt/sources.list.d/redis.list >/dev/null
  run ${SUDO} apt-get update
}

install_rpm_repo() {
  major="${VERSION_ID%%.*}"
  case "${ID:-}" in
    almalinux|rocky|rhel|centos|fedora)
      ;;
    *)
      echo "Unsupported RPM distribution ID=${ID:-unknown}; set up the Redis repository manually" >&2
      exit 1
      ;;
  esac
  # The official RPM instructions publish repository files per compatible
  # platform. AlmaLinux uses the matching Rocky Linux major-version repository.
  case "${major}" in
    8|9|10)
      repo_platform="rockylinux${major}"
      ;;
    *)
      echo "Unsupported Redis RPM platform version ${VERSION_ID:-unknown}" >&2
      exit 1
      ;;
  esac
  tmp_key="${TMPDIR:-/tmp}/redis.key"
  # The official RPM flow imports the Redis signing key before installing
  # packages from the Redis repository.
  curl -fsSL https://packages.redis.io/gpg -o "${tmp_key}"
  run ${SUDO} rpm --import "${tmp_key}"
  # Create /etc/yum.repos.d/redis.repo with the same fields shown in the
  # official Redis RPM installation instructions.
  {
    echo "[Redis]"
    echo "name=Redis"
    echo "baseurl=https://packages.redis.io/rpm/${repo_platform}"
    echo "enabled=1"
    echo "gpgcheck=1"
  } | run ${SUDO} tee /etc/yum.repos.d/redis.repo >/dev/null
  if command -v dnf >/dev/null 2>&1; then
    # Some RHEL-family images expose a distribution Redis module. Disable it so
    # the subsequent install resolves to packages.redis.io, not AppStream.
    run ${SUDO} dnf -y module disable redis || true
    run ${SUDO} dnf clean all
  else
    run ${SUDO} yum clean all
  fi
}

case "${ID:-}" in
  debian|ubuntu)
    if [ "${ACTION}" = "uninstall" ]; then
      if [ "${REDIS_PACKAGE}" = "redis" ]; then
        run ${SUDO} apt-get remove -y redis redis-server redis-sentinel redis-tools
      else
        run ${SUDO} apt-get remove -y "${REDIS_PACKAGE}"
      fi
      exit 0
    fi
    require_redis_package_for_pinned_version
    install_deb_repo
    if [ "${ACTION}" = "repo-only" ]; then
      exit 0
    fi
    if is_latest_version; then
      if [ "${ACTION}" = "upgrade" ]; then
        run ${SUDO} apt-get install --only-upgrade -y "${REDIS_PACKAGE}"
      else
        # Official APT default path: install the repository's current Redis
        # package, which also installs redis-tools.
        run ${SUDO} apt-get install -y "${REDIS_PACKAGE}"
      fi
    else
      redis_version="$(apt_redis_version)"
      require_apt_pinned_version "${redis_version}"
      # Official APT earlier-version path: install all Redis packages with the
      # exact codename-qualified package version.
      if [ "${ACTION}" = "upgrade" ]; then
        run ${SUDO} apt-get install --only-upgrade -y \
          "redis=${redis_version}" \
          "redis-server=${redis_version}" \
          "redis-sentinel=${redis_version}" \
          "redis-tools=${redis_version}"
      else
        run ${SUDO} apt-get install -y \
          "redis=${redis_version}" \
          "redis-server=${redis_version}" \
          "redis-sentinel=${redis_version}" \
          "redis-tools=${redis_version}"
      fi
    fi
    ;;
  almalinux|rocky|rhel|centos|fedora)
    if [ "${ACTION}" = "uninstall" ]; then
      pm="dnf"
      if ! command -v dnf >/dev/null 2>&1; then
        pm="yum"
      fi
      run ${SUDO} "${pm}" remove -y "${REDIS_PACKAGE}"
      exit 0
    fi
    require_redis_package_for_pinned_version
    install_rpm_repo
    if [ "${ACTION}" = "repo-only" ]; then
      exit 0
    fi
    pm="dnf"
    if ! command -v dnf >/dev/null 2>&1; then
      pm="yum"
    fi
    if is_latest_version; then
      if [ "${ACTION}" = "upgrade" ]; then
        run ${SUDO} "${pm}" upgrade -y "${REDIS_PACKAGE}"
      else
        # Official RPM default path: install redis from the configured Redis
        # repository after redis.repo and the GPG key are in place.
        run ${SUDO} "${pm}" install -y "${REDIS_PACKAGE}"
      fi
    else
      if ! "${pm}" --showduplicates list "${REDIS_PACKAGE}" 2>/dev/null | awk '{ print $2 }' | grep -qx "${REDIS_VERSION}-1"; then
        echo "Redis package ${REDIS_PACKAGE}-${REDIS_VERSION}-1 is not available from the configured Redis repository for ${ID:-unknown} ${VERSION_ID:-unknown}." >&2
        echo "Use REDIS_VERSION=latest or choose an OS/repository that publishes Redis ${REDIS_VERSION}." >&2
        exit 1
      fi
      if [ "${ACTION}" = "upgrade" ]; then
        run ${SUDO} "${pm}" upgrade -y "${REDIS_PACKAGE}-${REDIS_VERSION}-1"
      else
        run ${SUDO} "${pm}" install -y "${REDIS_PACKAGE}-${REDIS_VERSION}-1"
      fi
    fi
    ;;
  *)
    echo "Unsupported distribution ID=${ID:-unknown}" >&2
    exit 1
    ;;
esac
