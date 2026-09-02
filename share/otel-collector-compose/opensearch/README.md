# OpenSearch OpenTelemetry (OTel) Backend

[English](README.md) | [日本語](README.ja.md)

[Top: NestDAQ](../../../README.md) | [Previous: Storage option selection](../README.md) | [Next storage option: Victoria](../victoria/README.md)

This local validation stack uses OpenTelemetry Collector to receive OpenTelemetry logs and traces.
It stores the data in OpenSearch and displays it in OpenSearch Dashboards.

In this document, **Compose** means either Docker Compose (`docker compose`) or Podman Compose (`podman compose`).
Use either implementation to manage this stack.

## 1. Components

- `otel-collector`: receives OpenTelemetry Protocol (OTLP) logs and traces over Google remote procedure call (gRPC) and HTTP.
- `opensearch`: stores logs and traces exported by the collector.
- `opensearch-dashboards`: provides the web user interface (UI) for OpenSearch.
- `opensearch-dashboards-setup`: creates initial Data Views for logs and traces if they do not already exist.

OpenSearch 2.12 and later, including OpenSearch 3.x, requires `OPENSEARCH_INITIAL_ADMIN_PASSWORD` when the bundled demo security configuration is installed.
This local validation Compose setup disables the demo configuration installer and the Security plugin, so this stack does not require an OpenSearch admin password.

<a id="2-before-startup"></a>
## 2. Before startup

Do not start the stack directly from the installation directory.
Follow the [parent README startup procedure](../README.md#1-start) to copy the setup to a writable working directory.
Run the following commands from `opensearch/` in that working copy.

When changing the data location with `OPENSEARCH_DATA_DIR`, create the directory before startup and give the container suitable ownership and permissions.
The `:Z` option already present in the Compose file applies the SELinux label.
See Section 2.2 for ownership and permission examples for rootless Podman.

### 2.1. Environment variables

The Compose settings available through environment variables are listed in [Table 1](#tbl-opensearch-environment-variables-en).

<a id="tbl-opensearch-environment-variables-en"></a>
**Table 1: OpenSearch Compose environment variables.**

| Variable | Default | Description |
| :-- | :-- | :-- |
| `OTEL_COLLECTOR_IMAGE` | `docker.io/otel/opentelemetry-collector-contrib:0.155.0` | Collector image. |
| `OPENSEARCH_IMAGE` | `docker.io/opensearchproject/opensearch:2.19.5` | OpenSearch image. |
| `OPENSEARCH_DASHBOARDS_IMAGE` | `docker.io/opensearchproject/opensearch-dashboards:2.19.5` | OpenSearch Dashboards image. |
| `OPENSEARCH_PORT` | `9200` | Host port mapped to OpenSearch. |
| `OPENSEARCH_DASHBOARDS_PORT` | `5601` | Host port mapped to OpenSearch Dashboards. |
| `OTEL_COLLECTOR_GRPC_PORT` | `4317` | Host port mapped to OTLP gRPC. |
| `OTEL_COLLECTOR_HTTP_PORT` | `4318` | Host port mapped to OTLP HTTP. |
| `OPENSEARCH_DATA_DIR` | `./opensearch-data` | Host directory bind-mounted to `/usr/share/opensearch/data/`. |
| `OTEL_COLLECTOR_CONFIG_FILE` | `./otel-collector-config-opensearch.yaml` | Collector config file. |
| `OPENSEARCH_DASHBOARDS_CONFIG_FILE` | `./opensearch_dashboards.yaml` | OpenSearch Dashboards config file. |
| `OPENSEARCH_DASHBOARDS_SETUP_SCRIPT` | `./opensearch-dashboards/setup-dashboards.js` | Initial Dashboards setup script. |

### 2.2. Rootless Podman

OpenSearch runs as container `uid=1000,gid=1000`.
Here, `uid/gid` means user identifier/group identifier.
With rootless Podman, the host directory bind-mounted to `/usr/share/opensearch/data/` must be readable and writable by that container uid/gid as seen from the Podman user namespace:

#### 2.2.1. Without `keep-id`

Use `podman unshare` to set ownership and permissions as seen from the Podman user namespace:

```bash
# Prepare the data directory for the container's user in the Podman user namespace.
mkdir -p ./opensearch-data
podman unshare chown -R 1000:1000 ./opensearch-data
podman unshare chmod -R u+rwX ./opensearch-data
```

Start the stack with the command in Section 3.2 after preparing the directory.

#### 2.2.2. With `keep-id`

Alternatively, map the container's `1000:1000` user to the host user that starts Podman Compose.
Create the data directory as the host user before starting the stack:

```bash
# Create the data directory as the host user.
mkdir -p ./opensearch-data
```

The `PODMAN_USERNS` setting changes the user namespace mapping.
It does not change the user ID of the OpenSearch container process, which remains `uid=1000,gid=1000` inside the container.
Start the stack with the command in Section 3.3.

<a id="3-start"></a>
## 3. Start

In the shell command examples below, lines beginning with `#` are explanatory comments for the reader and are not executed by the shell.

### 3.1. Docker Compose

```bash
# Start the OpenSearch validation stack with Docker Compose.
docker compose -f compose-opensearch.yaml up
```

### 3.2. Podman Compose without `keep-id`

Prepare the data directory as described in Section 2.2.1 before running this command:

```bash
# Start the OpenSearch validation stack with Podman Compose.
podman compose -f compose-opensearch.yaml up
```

### 3.3. Podman Compose with `keep-id`

Prepare the data directory as described in Section 2.2.2 before running this command:

```bash
# Start the stack while mapping the container's uid/gid to the host user.
PODMAN_USERNS="keep-id:uid=1000,gid=1000" \
podman compose --in-pod=false -f compose-opensearch.yaml up
```

<a id="4-verification-and-use"></a>
## 4. Verification and use

### 4.1. OpenSearch Dashboards

After the stack starts, open `http://localhost:5601/app/discover`.
The setup service creates Data Views for `otel-logs-*` and `otel-traces-*`.
It sets `otel-logs-*` as the default only when no default Data View is already configured.

### 4.2. Ports

- OpenSearch: `http://localhost:9200`
- OpenSearch Dashboards: `http://localhost:5601`
- OTLP gRPC receiver: `localhost:4317`
- OTLP HTTP receiver: `http://localhost:4318`

Host processes use the `localhost` endpoints above.
A NestDAQ device container or `daq-webctl` container in the same Compose network should use `otel-collector:4317` for OTLP gRPC or `http://otel-collector:4318` for OTLP HTTP.

### 4.3. Collector pipelines

The collector stores logs in indices named:

```text
otel-logs-%{service.name}-yyyy.MM.dd
```

It stores traces in indices named:

```text
otel-traces-%{service.name}-yyyy.MM.dd
```

If `service.name` is missing, the collector uses `unknown-service`.
OpenSearch requires lowercase index names.
NestDAQ telemetry converts ASCII uppercase letters in `service.name` to lowercase before export.
External OTLP clients should also send lowercase `service.name` values when using this Compose setup.

<a id="5-stop"></a>
## 5. Stop

Stop and remove the local validation containers and network:

### 5.1. Docker Compose

```bash
# Stop and remove the Docker validation containers and network.
docker compose -f compose-opensearch.yaml down
```

### 5.2. Podman Compose without `keep-id`

```bash
# Stop and remove the Podman validation containers and network.
podman compose -f compose-opensearch.yaml down
```

### 5.3. Podman Compose with `keep-id`

Use the same user namespace settings as at startup:

```bash
# Stop and remove the Podman validation containers and network.
PODMAN_USERNS="keep-id:uid=1000,gid=1000" \
podman compose --in-pod=false -f compose-opensearch.yaml down
```

<a id="6-delete-stored-opensearch-data"></a>
## 6. Delete stored OpenSearch data

The `down` command does not delete the OpenSearch data directory.
By default, `./opensearch-data/` is bind-mounted to `/usr/share/opensearch/data/`.
If you start this Compose setup again with the same `OPENSEARCH_DATA_DIR`, OpenSearch reuses the previous data.

Delete the OpenSearch data directory only when you want to discard the stored logs, traces, indexes, and OpenSearch metadata:

```bash
# Permanently discard the stored OpenSearch data.
rm -rf ./opensearch-data
```

For rootless Podman, file ownership may require removal through the user namespace:

```bash
# Discard rootless Podman data through its user namespace.
podman unshare rm -rf ./opensearch-data
```
