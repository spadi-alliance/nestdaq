# ClickStack OpenTelemetry (OTel) Backend

[English](README.md) | [日本語](README.ja.md)

[Top: NestDAQ](../../../README.md) | [Previous storage option: Victoria](../victoria/README.md) | [Next: Package installers](../../installers/README.md)

This local validation stack uses the ClickStack OpenTelemetry Collector to receive OpenTelemetry logs, metrics, and traces.
It stores the data in ClickHouse and displays it in the ClickStack user interface (UI).

<u><strong>This backend is experimental and not yet fully verified.</strong></u>

In this document, **Compose** means either Docker Compose (`docker compose`) or Podman Compose (`podman compose`).
Use either implementation to manage this stack.

## 1. Components

- `clickstack`: runs the ClickStack UI, OpenTelemetry Collector, and ClickHouse in one container.

This stack is intended for local validation.
Production deployments should use explicit credentials, retention and backup policies, and a deployment topology managed outside this sample Compose file.

<a id="2-before-startup"></a>
## 2. Before startup

Do not start the stack directly from the installation directory.
Follow the [parent README startup procedure](../README.md#1-start) to copy the setup to a writable working directory.
Run the following commands from `clickhouse/` in that working copy.

When changing a data location, create the target directory before startup and give the container suitable ownership and permissions.
The `:Z` options already present in the Compose file apply the SELinux labels.

### 2.1. Environment variables

The Compose settings available through environment variables are listed in [Table 1](#tbl-clickstack-environment-variables-en).

<a id="tbl-clickstack-environment-variables-en"></a>
**Table 1: ClickStack Compose environment variables.**

| Variable | Default | Description |
| :-- | :-- | :-- |
| `CLICKSTACK_IMAGE` | `docker.io/clickhouse/clickstack-all-in-one:2` | ClickStack all-in-one image. |
| `CLICKSTACK_UI_PORT` | `8080` | Host port mapped to the ClickStack UI. |
| `CLICKHOUSE_HTTP_PORT` | `8123` | Host port mapped to ClickHouse HTTP. |
| `OTEL_COLLECTOR_GRPC_PORT` | `4317` | Host port mapped to OTLP gRPC. |
| `OTEL_COLLECTOR_HTTP_PORT` | `4318` | Host port mapped to OTLP HTTP. |
| `CLICKSTACK_DB_DIR` | `./clickstack-db` | Host directory bind-mounted to `/data/db/`. |
| `CLICKSTACK_CLICKHOUSE_DATA_DIR` | `./clickstack-clickhouse-data` | Host directory bind-mounted to `/var/lib/clickhouse/`. |
| `CLICKSTACK_CLICKHOUSE_LOG_DIR` | `./clickstack-clickhouse-logs` | Host directory bind-mounted to `/var/log/clickhouse-server/`. |

<a id="3-start"></a>
## 3. Start

In the shell command examples below, lines beginning with `#` are explanatory comments for the reader and are not executed by the shell.

### 3.1. Docker Compose

```bash
# Start the ClickStack validation stack with Docker Compose.
docker compose -f compose-clickhouse.yaml up
```

### 3.2. Podman Compose

```bash
# Start the ClickStack validation stack with Podman Compose.
podman compose -f compose-clickhouse.yaml up
```

<a id="4-verification-and-use"></a>
## 4. Verification and use

### 4.1. ClickStack UI

Open the ClickStack UI at `http://localhost:8080` after the stack starts.
On first use, create the UI user.
ClickStack connects to the local ClickHouse instance and prepares data sources for logs, metrics, and traces.

### 4.2. Ports

- ClickStack UI: `http://localhost:8080`
- ClickHouse HTTP: `http://localhost:8123`
- OpenTelemetry Protocol (OTLP) Google remote procedure call (gRPC) receiver: `localhost:4317`
- OTLP HTTP receiver: `http://localhost:4318`

### 4.3. NestDAQ telemetry endpoints

Host processes use `localhost:4317` for OTLP/gRPC or `http://localhost:4318` for OTLP/HTTP.
A NestDAQ device container or `daq-webctl` container in the same Compose network should use `clickstack:4317` for OTLP gRPC or `http://clickstack:4318` for OTLP HTTP.

For example, HTTP endpoints use these paths:

```text
http://localhost:4318/v1/logs
http://localhost:4318/v1/metrics
http://localhost:4318/v1/traces
```

<a id="5-stop"></a>
## 5. Stop

Stop and remove the local validation container and network.

### 5.1. Docker Compose

```bash
# Stop and remove the Docker validation container and network.
docker compose -f compose-clickhouse.yaml down
```

### 5.2. Podman Compose

```bash
# Stop and remove the Podman validation container and network.
podman compose -f compose-clickhouse.yaml down
```

<a id="6-delete-stored-data"></a>
## 6. Delete stored ClickStack and ClickHouse data

The `down` command does not delete the ClickStack and ClickHouse data and log directories.
If you start this Compose setup again with the same directories, the previous backend data is reused.

Delete the data and log directories only when you want to discard the stored backend data:

```bash
# Permanently discard all ClickStack and ClickHouse data and logs.
rm -rf ./clickstack-db \
       ./clickstack-clickhouse-data \
       ./clickstack-clickhouse-logs
```

For rootless Podman, file ownership may require removal through the user namespace:

```bash
# Discard rootless Podman data and logs through its user namespace.
podman unshare rm -rf ./clickstack-db \
                       ./clickstack-clickhouse-data \
                       ./clickstack-clickhouse-logs
```
