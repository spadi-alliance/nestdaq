#!/bin/sh
set -eu

ACTION="${1:-install}"
OTELCOL_CONTRIB_VERSION="${OTELCOL_CONTRIB_VERSION:-0.155.0}"
SUDO="${SUDO:-sudo}"
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
fi

usage() {
  cat <<EOF
Usage: $0 [install|upgrade|uninstall]

Install or update OpenTelemetry Collector Contrib from an upstream release
package.

Actions:
  install    Download and install the selected release package.
  upgrade    Download and install the selected release package.
  uninstall  Remove the package. Configuration and data are kept.

Environment:
  OTELCOL_CONTRIB_VERSION  Release version. Default: ${OTELCOL_CONTRIB_VERSION}
  SUDO                     Privilege wrapper. Default: sudo, or empty when run as root.

Examples:
  $0 install
  OTELCOL_CONTRIB_VERSION=0.155.0 $0 install
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

if [ "${ACTION}" != "install" ] && [ "${ACTION}" != "upgrade" ] && [ "${ACTION}" != "uninstall" ]; then
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

arch="$(uname -m)"
case "${arch}" in
  x86_64|amd64)
    otel_arch="amd64"
    ;;
  aarch64|arm64)
    otel_arch="arm64"
    ;;
  *)
    echo "Unsupported architecture: ${arch}" >&2
    exit 1
    ;;
esac

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/otelcol-contrib.XXXXXX")"
trap 'rm -rf "${tmp_dir}"' EXIT

case "${ID:-}" in
  debian|ubuntu)
    if [ "${ACTION}" = "uninstall" ]; then
      run ${SUDO} apt-get remove -y otelcol-contrib
      exit 0
    fi
    pkg="otelcol-contrib_${OTELCOL_CONTRIB_VERSION}_linux_${otel_arch}.deb"
    url="https://github.com/open-telemetry/opentelemetry-collector-releases/releases/download/v${OTELCOL_CONTRIB_VERSION}/${pkg}"
    curl -fL "${url}" -o "${tmp_dir}/${pkg}"
    run ${SUDO} apt-get install -y "${tmp_dir}/${pkg}"
    ;;
  almalinux|rocky|rhel|centos|fedora)
    if [ "${ACTION}" = "uninstall" ]; then
      if command -v dnf >/dev/null 2>&1; then
        run ${SUDO} dnf remove -y otelcol-contrib
      else
        run ${SUDO} yum remove -y otelcol-contrib
      fi
      exit 0
    fi
    pkg="otelcol-contrib_${OTELCOL_CONTRIB_VERSION}_linux_${otel_arch}.rpm"
    url="https://github.com/open-telemetry/opentelemetry-collector-releases/releases/download/v${OTELCOL_CONTRIB_VERSION}/${pkg}"
    curl -fL "${url}" -o "${tmp_dir}/${pkg}"
    if command -v dnf >/dev/null 2>&1; then
      run ${SUDO} dnf install -y "${tmp_dir}/${pkg}"
    else
      run ${SUDO} yum install -y "${tmp_dir}/${pkg}"
    fi
    ;;
  *)
    echo "Unsupported distribution ID=${ID:-unknown}" >&2
    exit 1
    ;;
esac
