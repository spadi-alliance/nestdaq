#!/bin/sh
set -eu

ACTION="${1:-install}"
OPENSEARCH_VERSION="${OPENSEARCH_VERSION:-2.19.5}"
OPENSEARCH_INSTALL_SECURITY="${OPENSEARCH_INSTALL_SECURITY:-disabled}"
OPENSEARCH_APT_GPGCHECK="${OPENSEARCH_APT_GPGCHECK:-enabled}"
OPENSEARCH_RPM_GPGCHECK="${OPENSEARCH_RPM_GPGCHECK:-enabled}"
SUDO="${SUDO:-sudo}"
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
fi

usage() {
  cat <<EOF
Usage: $0 [install|upgrade|uninstall|repo-only]

Install or update OpenSearch from the OpenSearch 2.x package repository.

Actions:
  install    Register the repository and install OpenSearch.
  upgrade    Register the repository and upgrade OpenSearch.
  uninstall  Remove OpenSearch. Repository files, configuration, and data are kept.
  repo-only  Register the OpenSearch repository only.

Environment:
  OPENSEARCH_VERSION                 Version to install, for example 2.19.5.
                                     Default: ${OPENSEARCH_VERSION}. Set to latest for the repository default.
  OPENSEARCH_INSTALL_SECURITY        disabled or demo. Default: ${OPENSEARCH_INSTALL_SECURITY}
  OPENSEARCH_APT_GPGCHECK            enabled or disabled. Default: ${OPENSEARCH_APT_GPGCHECK}
  OPENSEARCH_RPM_GPGCHECK            enabled or disabled. Default: ${OPENSEARCH_RPM_GPGCHECK}
  OPENSEARCH_INITIAL_ADMIN_PASSWORD  Required when OPENSEARCH_INSTALL_SECURITY=demo.
  SUDO                               Privilege wrapper. Default: sudo, or empty when run as root.

Examples:
  $0 install
  OPENSEARCH_VERSION=2.19.5 $0 install
  OPENSEARCH_VERSION=latest $0 install
  $0 uninstall
  OPENSEARCH_INSTALL_SECURITY=demo \\
  OPENSEARCH_INITIAL_ADMIN_PASSWORD='change-this-strong-password' \\
  $0 install
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
  [ "${OPENSEARCH_VERSION}" = "latest" ]
}

apt_repo_options() {
  if [ "${OPENSEARCH_APT_GPGCHECK}" = "disabled" ]; then
    printf '%s\n' 'trusted=yes'
  elif [ "${OPENSEARCH_APT_GPGCHECK}" = "enabled" ]; then
    printf '%s\n' 'signed-by=/etc/apt/keyrings/opensearch-release-keyring'
  else
    echo "OPENSEARCH_APT_GPGCHECK must be enabled or disabled" >&2
    exit 1
  fi
}

install_deb_repo() {
  run ${SUDO} apt-get update
  run ${SUDO} apt-get install -y lsb-release ca-certificates curl gnupg2
  run ${SUDO} mkdir -p /etc/apt/keyrings
  curl -o- https://artifacts.opensearch.org/publickeys/opensearch.pgp |
    run ${SUDO} gpg --dearmor --batch --yes -o /etc/apt/keyrings/opensearch-release-keyring
  echo "deb [$(apt_repo_options)] https://artifacts.opensearch.org/releases/bundle/opensearch/2.x/apt stable main" |
    run ${SUDO} tee /etc/apt/sources.list.d/opensearch-2.x.list >/dev/null
  run ${SUDO} apt-get update
}

install_rpm_repo() {
  run ${SUDO} curl -SL https://artifacts.opensearch.org/releases/bundle/opensearch/2.x/opensearch-2.x.repo -o /etc/yum.repos.d/opensearch-2.x.repo
  if command -v dnf >/dev/null 2>&1; then
    run ${SUDO} dnf clean all
  else
    run ${SUDO} yum clean all
  fi
}

install_env() {
  if [ "${OPENSEARCH_INSTALL_SECURITY}" = "demo" ]; then
    if [ -z "${OPENSEARCH_INITIAL_ADMIN_PASSWORD:-}" ]; then
      echo "OPENSEARCH_INITIAL_ADMIN_PASSWORD is required when OPENSEARCH_INSTALL_SECURITY=demo" >&2
      exit 1
    fi
    printf 'OPENSEARCH_INITIAL_ADMIN_PASSWORD=%s\n' "${OPENSEARCH_INITIAL_ADMIN_PASSWORD}"
  else
    printf 'DISABLE_INSTALL_DEMO_CONFIG=true\n'
    printf 'DISABLE_SECURITY_PLUGIN=true\n'
  fi
}

run_with_install_env() {
  if [ -n "${SUDO}" ]; then
    run ${SUDO} env $(install_env) "$@"
  else
    run env $(install_env) "$@"
  fi
}

rpm_gpgcheck_option() {
  if [ "${OPENSEARCH_RPM_GPGCHECK}" = "disabled" ]; then
    printf '%s\n' '--nogpgcheck'
  elif [ "${OPENSEARCH_RPM_GPGCHECK}" != "enabled" ]; then
    echo "OPENSEARCH_RPM_GPGCHECK must be enabled or disabled" >&2
    exit 1
  fi
}

install_deb_with_install_env() {
  package="$1"
  tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/opensearch-deb.XXXXXX")"
  (
    cd "${tmp_dir}"
    run apt-get download "${package}"
  )
  deb_file="$(find "${tmp_dir}" -maxdepth 1 -name '*.deb' | head -n 1)"
  if [ -z "${deb_file}" ]; then
    echo "Downloaded OpenSearch deb package not found for ${package}" >&2
    rm -rf "${tmp_dir}"
    exit 1
  fi
  run ${SUDO} dpkg --unpack "${deb_file}"
  if [ "${OPENSEARCH_INSTALL_SECURITY}" = "disabled" ]; then
    run ${SUDO} rm -rf /usr/share/opensearch/plugins/opensearch-security
  fi
  run_with_install_env dpkg --configure opensearch
  rm -rf "${tmp_dir}"
}

case "${ID:-}" in
  debian|ubuntu)
    if [ "${ACTION}" = "uninstall" ]; then
      run ${SUDO} apt-get remove -y opensearch
      exit 0
    fi
    install_deb_repo
    if [ "${ACTION}" = "repo-only" ]; then
      exit 0
    fi
    package="opensearch"
    if ! is_latest_version; then
      package="opensearch=${OPENSEARCH_VERSION}"
    fi
    install_deb_with_install_env "${package}"
    ;;
  almalinux|rocky|rhel|centos|fedora)
    if [ "${ACTION}" = "uninstall" ]; then
      pm="dnf"
      if ! command -v dnf >/dev/null 2>&1; then
        pm="yum"
      fi
      run ${SUDO} "${pm}" remove -y opensearch
      exit 0
    fi
    install_rpm_repo
    if [ "${ACTION}" = "repo-only" ]; then
      exit 0
    fi
    package="opensearch"
    if ! is_latest_version; then
      package="opensearch-${OPENSEARCH_VERSION}"
    fi
    pm="dnf"
    if ! command -v dnf >/dev/null 2>&1; then
      pm="yum"
    fi
    if [ "${ACTION}" = "upgrade" ]; then
      run_with_install_env "${pm}" upgrade -y $(rpm_gpgcheck_option) "${package}"
    else
      run_with_install_env "${pm}" install -y $(rpm_gpgcheck_option) "${package}"
    fi
    ;;
  *)
    echo "Unsupported distribution ID=${ID:-unknown}" >&2
    exit 1
    ;;
esac
