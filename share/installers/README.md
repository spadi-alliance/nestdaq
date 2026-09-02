# Package Installer Helpers

[English](README.md) | [日本語](README.ja.md)

[Top: NestDAQ](../../README.md) | [Previous: ClickStack setup](../otel-collector-compose/clickhouse/README.md) | [Next: CMake](../../cmake/README.md)

This directory contains helper scripts for installing and updating external services with the host package manager.
They provide an alternative to the local **Compose** examples, where Compose means either `docker compose` or `podman compose`.

The scripts are intended for hosts managed by an administrator.
They use `sudo` unless run as root.
Because they install packages into system-managed locations such as `/usr/`, `/etc/`, package-manager repository directories, and systemd unit directories, they require root privileges.

On Debian and Ubuntu systems, the scripts use `apt-get`.
On RHEL-family systems such as AlmaLinux, Rocky Linux, RHEL, CentOS, and Fedora, the scripts prefer `dnf` and fall back to `yum` when `dnf` is unavailable.

## 1. Scripts

The available installer scripts are listed in [Table 1](#tbl-installer-scripts-en).

<a id="tbl-installer-scripts-en"></a>
**Table 1: Package installer scripts.**

| Script | Installs or updates |
| :-- | :-- |
| `install-redis-stack.sh` | Redis server and [Redis Stack](../../INSTALL.md#external-runtime-components) modules from the Redis package repository. |
| `install-otelcol-contrib.sh` | OpenTelemetry Collector Contrib from the official OpenTelemetry release package. |
| `install-opensearch.sh` | OpenSearch from the OpenSearch 2.x package repository. |
| `install-opensearch-dashboards.sh` | OpenSearch Dashboards from the OpenSearch 2.x package repository. |

## 2. Usage

Run a script with one of these actions:
In shell command examples, lines beginning with `#` are comments for the reader and are not executed by the shell.

```sh
# Install the package using the configured package source.
./install-redis-stack.sh install
# Upgrade the package from the same package source.
./install-redis-stack.sh upgrade
# Remove the package while retaining configuration and data.
./install-redis-stack.sh uninstall
# Display the script's supported options and actions.
./install-redis-stack.sh --help
```

`install` is the default action.
`upgrade` uses the same package source and asks the package manager to update the installed package.
`uninstall` removes the package with the host package manager.

The `uninstall` action does not delete package repository files, service configuration, logs, Redis persistence files, or OpenSearch data paths.
Review these files manually before deleting them.
If `systemd` manages a service, stop and disable the service before uninstalling its package.
See <a href="#6-systemd-management">systemd Management</a>.

For normal use, the scripts use `sudo` as the default and recommended privilege wrapper.
Setting `SUDO=sudo` is valid but redundant.
When run as root, the scripts automatically omit the wrapper, so no `SUDO` assignment is needed.
If `doas` is installed and configured, it can be selected as an optional alternative.

```sh
# Use doas only when it is installed and configured for the required commands.
SUDO=doas ./install-opensearch.sh install
```

## 3. Redis

The Redis script registers `packages.redis.io` and installs Redis `8.2.7` by default.
For Redis 8 packages, the default package name is `redis`.
This package installs Redis server and Redis Stack modules, but it does not include RedisInsight.
The Redis 8.2.7 package includes modules such as:

```text
/usr/lib/redis/modules/redisbloom.so
/usr/lib/redis/modules/redisearch.so
/usr/lib/redis/modules/redistimeseries.so
/usr/lib/redis/modules/rejson.so
```

Use `REDIS_VERSION=latest` to install or upgrade to the latest version currently published by the Redis repository:

```sh
# Install the newest Redis version available from the repository.
REDIS_VERSION=latest ./install-redis-stack.sh install
```

Use `REDIS_PACKAGE=redis-stack` only when the Redis repository for the host distribution provides that package and RedisInsight is required:

```sh
# Install the Redis Stack package when the repository provides it.
REDIS_PACKAGE=redis-stack ./install-redis-stack.sh install
```

The default `REDIS_PACKAGE=redis` package supports version pinning with `REDIS_VERSION=8.2.7`.
On Debian and Ubuntu, a pinned installation follows the official Redis APT package set and installs `redis`, `redis-server`, `redis-sentinel`, and `redis-tools` at the same package version.
Set `REDIS_VERSION=latest` when using a legacy package name such as `redis-stack-server` or `redis-stack`.

The default `redis` package does not install RedisInsight.
When RedisInsight is required, use a separate RedisInsight package or the Redis Stack container helper in [`../redis-stack-container/`](../redis-stack-container/README.md).

Redis publishes packages for specific distribution codenames or RPM repositories.
If the configured Redis repository does not publish `REDIS_VERSION`, the installer fails rather than installing a different Redis version.

For Debian and Ubuntu systems, the official Redis APT repository publishes packages for each distribution codename.
Debian 12 (`bookworm`), Debian 13 (`trixie`), Ubuntu 22.04 (`jammy`), and Ubuntu 24.04 (`noble`) can install Redis `7.2.14`, `7.4.9`, and `8.2.7` with the pinned package set.
Ubuntu 26.04 (`resolute`) currently provides only newer Redis packages such as `8.8.0`.
Pinned installations of `7.2.14`, `7.4.9`, and `8.2.7` therefore fail on Ubuntu 26.04.

For AlmaLinux and other RHEL-family systems, the installer uses the official Redis RPM repository for the matching Rocky Linux major version.
The official Redis repositories for Rocky Linux do not provide Redis 7.x packages.
AlmaLinux 9 AppStream provides Redis `7.2.14` through the `redis:7` module, but this installer does not use that package because it targets the official Redis repository.
AlmaLinux 8 and 9 can install Redis `8.2.7` from the official Redis RPM repository.
AlmaLinux 10 currently provides only newer Redis packages such as `8.8.0` in that repository.
The default `REDIS_VERSION=8.2.7` installation therefore fails on AlmaLinux 10.

This installer does not install Redis from AlmaLinux AppStream modules.
On RHEL-family systems, it always configures the official Redis RPM repository and disables the distribution Redis module so that package resolution uses `packages.redis.io`.
The AppStream row below is for reference only.

Verified Redis package availability is shown in [Table 2](#tbl-redis-package-availability-en).

<a id="tbl-redis-package-availability-en"></a>
**Table 2: Verified Redis package availability by distribution.**

| Distribution | Repository key | Redis 7.2.14 | Redis 7.4.9 | Redis 8.2.7 |
| --- | --- | --- | --- | --- |
| AlmaLinux 8 | `rockylinux8` RPM repo | No | No | Yes |
| AlmaLinux 9 | `rockylinux9` RPM repo | No | No | Yes |
| AlmaLinux 9 | AppStream `redis:7` module (not used by this installer) | Yes | No | No |
| AlmaLinux 10 | `rockylinux10` RPM repo | No | No | No (`8.8.0` available) |
| Debian 12 | `bookworm` APT repo | Yes | Yes | Yes |
| Debian 13 | `trixie` APT repo | Yes | Yes | Yes |
| Ubuntu 22.04 | `jammy` APT repo | Yes | Yes | Yes |
| Ubuntu 24.04 | `noble` APT repo | Yes | Yes | Yes |
| Ubuntu 26.04 | `resolute` APT repo | No | No | No (`8.8.0` available) |

Official install instructions:

- https://redis.io/docs/latest/operate/oss_and_stack/install/install-stack/apt/
- https://redis.io/docs/latest/operate/oss_and_stack/install/install-stack/rpm/

## 4. OpenTelemetry Collector Contrib

The OpenTelemetry project publishes Linux packages with each GitHub release.
This helper does not configure an apt or dnf repository.
The script downloads the selected release package and installs it through `apt` or `dnf`.

Use `OTELCOL_CONTRIB_VERSION` to select a version.
The default matches the version used by the local Compose examples.

```sh
# Install the selected OpenTelemetry Collector Contrib release.
OTELCOL_CONTRIB_VERSION=0.155.0 ./install-otelcol-contrib.sh install
```

Before starting the service, create or edit the collector configuration in the location configured by the package, commonly `/etc/otelcol-contrib/config.yaml`.

Official install and release instructions:

- https://opentelemetry.io/docs/collector/install/
- https://github.com/open-telemetry/opentelemetry-collector-releases/releases

## 5. OpenSearch

The OpenSearch scripts register the OpenSearch 2.x package repositories.
By default, they install OpenSearch `2.19.5` and OpenSearch Dashboards `2.19.5`.
Use `OPENSEARCH_VERSION=latest` or `OPENSEARCH_DASHBOARDS_VERSION=latest` to install or upgrade to the latest version currently published by the repository.

By default, `install-opensearch.sh` passes `DISABLE_INSTALL_DEMO_CONFIG=true` and `DISABLE_SECURITY_PLUGIN=true` during installation.
These settings allow the package to be installed without a demo administrator password.
To configure demo security, set `OPENSEARCH_INSTALL_SECURITY=demo` and provide `OPENSEARCH_INITIAL_ADMIN_PASSWORD`.

```sh
# Install the selected OpenSearch release.
OPENSEARCH_VERSION=2.19.5 ./install-opensearch.sh install
# Install the matching OpenSearch Dashboards release.
OPENSEARCH_DASHBOARDS_VERSION=2.19.5 ./install-opensearch-dashboards.sh install

# Install OpenSearch with demo security and the required administrator password.
OPENSEARCH_INSTALL_SECURITY=demo \
OPENSEARCH_INITIAL_ADMIN_PASSWORD='change-this-strong-password' \
./install-opensearch.sh install
```

These scripts install packages only.
Before exposing the services on a network, review and edit the service configuration under `/etc/opensearch/` and `/etc/opensearch-dashboards/`.

Official install instructions:

- https://docs.opensearch.org/latest/install-and-configure/install-opensearch/rpm/
- https://docs.opensearch.org/latest/install-and-configure/install-opensearch/debian/
- https://docs.opensearch.org/latest/install-and-configure/install-dashboards/rpm/
- https://docs.opensearch.org/latest/install-and-configure/install-dashboards/debian/

<a id="6-systemd-management"></a>
## 6. systemd Management

The package scripts install software only.
Review the service configuration before enabling or starting services with `systemd`.

Common service commands:

```sh
# Show the service's current status.
sudo systemctl status <service>
# Enable the service at boot and start it now.
sudo systemctl enable --now <service>
# Restart the running service after a configuration change.
sudo systemctl restart <service>
# Stop the service without changing its boot setting.
sudo systemctl stop <service>
# Prevent the service from starting automatically at boot.
sudo systemctl disable <service>
```

Likely service names are listed in [Table 3](#tbl-systemd-service-unit-names-en).

<a id="tbl-systemd-service-unit-names-en"></a>
**Table 3: Likely systemd service unit names.**

| Service | Unit name |
| :-- | :-- |
| Redis | commonly `redis-server`; legacy Redis Stack packages may use `redis-stack-server`. |
| OpenTelemetry Collector Contrib | `otelcol-contrib`. |
| OpenSearch | `opensearch`. |
| OpenSearch Dashboards | `opensearch-dashboards`. |

Examples:

```sh
# Enable and start the OpenTelemetry Collector Contrib service.
sudo systemctl enable --now otelcol-contrib
# Enable and start the OpenSearch service.
sudo systemctl enable --now opensearch
# Enable and start the OpenSearch Dashboards service.
sudo systemctl enable --now opensearch-dashboards
```

For Redis, check the unit name installed by your package first:

```sh
# Find the Redis unit name installed on this host.
systemctl list-unit-files 'redis*'
# Enable and start the commonly named Redis service.
sudo systemctl enable --now redis-server
```

If you install the RedisInsight-inclusive `redis-stack` package, check the installed unit names before enabling services.
The Redis Stack container helper `run-redis-stack.sh` also includes RedisInsight, but it is separate from these host package installer scripts.

Before uninstalling a package managed by `systemd`, explicitly stop and disable its service.
The installer scripts' `uninstall` action only removes the package with the host package manager; it does not run `systemctl`.

```sh
# Stop the service and prevent it from starting at boot.
sudo systemctl stop <service>
sudo systemctl disable <service>
# Remove the package with its installer helper.
./install-xxx.sh uninstall
# Reload systemd after the package removes its unit files.
sudo systemctl daemon-reload
# Confirm whether matching unit files remain.
systemctl list-unit-files '<service-pattern>'
```

For Redis, confirm the installed unit name first because the name can differ between packages and distributions:

```sh
# Find the Redis unit name installed on this host.
systemctl list-unit-files 'redis*'
# Stop Redis and prevent it from starting at boot.
sudo systemctl stop redis-server
sudo systemctl disable redis-server
# Remove the Redis package with the installer helper.
./install-redis-stack.sh uninstall
# Reload systemd after the package removes its unit files.
sudo systemctl daemon-reload
# Confirm whether Redis unit files remain.
systemctl list-unit-files 'redis*'
```
