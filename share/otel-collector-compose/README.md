# OpenTelemetry Collector Container Setups

[English](README.md) | [日本語](README.ja.md)

[Top: NestDAQ](../../README.md) | [Previous: Redis containers](../redis-stack-container/README.md) | [Next: OpenSearch setup](opensearch/README.md)

In this documentation, **Compose** means either Docker Compose (`docker compose`) or Podman Compose (`podman compose`).
This directory contains Compose setups for local validation.
Each setup uses OpenTelemetry Collector to receive OpenTelemetry data and forward it to the selected storage destination.
In this document, a **storage stack** combines a data store such as OpenSearch with a viewing tool such as OpenSearch Dashboards.

These stacks are intended for local validation only.
They publish service ports on the host and, where applicable, use simple local credentials.
Do not expose them on a public or shared network.

Each subdirectory provides example Compose and configuration files for one storage stack.
Use the files in the subdirectory that corresponds to the storage stack you want to run:

- [`opensearch/`](opensearch/README.md): Stores logs and traces in OpenSearch and displays them in OpenSearch Dashboards.
- [`victoria/`](victoria/README.md): Stores logs, metrics, and traces in VictoriaLogs, VictoriaMetrics, and VictoriaTraces, and displays them in Grafana.
  <u><strong>This storage stack is experimental and not yet fully verified.</strong></u>
- [`clickhouse/`](clickhouse/README.md): Stores logs, metrics, and traces in ClickStack and displays them in the ClickStack user interface (UI).
  <u><strong>This storage stack is experimental and not yet fully verified.</strong></u>

See the linked README for startup instructions, environment variables, and stored-data directories.

## 1. Start

Do not start a Compose stack directly from the installation directory.
After installation, copy the installed setup to a writable working directory where its configuration files and data locations can be changed.
The working copy lets you adjust bind-mounted data paths, ownership, and permissions for the host without modifying installed files.
The `:Z` options already present in the Compose files apply SELinux labels.
If `./otel-collector-compose/` already exists, remove it first or choose a different destination.
In the shell command examples below, lines beginning with `#` are explanatory comments for the reader and are not executed by the shell.

```bash
# Copy the installed setup and enter the working copy.
cp -a <install-prefix>/share/otel-collector-compose ./otel-collector-compose
cd ./otel-collector-compose
```

Enter a storage-stack directory in the working copy and start one Compose stack:

```bash
# Enter the OpenSearch directory and start its stack.
cd opensearch
docker compose -f compose-opensearch.yaml up
```

```bash
# Enter the Victoria directory and start its stack.
cd victoria
docker compose -f compose-victoria.yaml up
```

```bash
# Enter the ClickHouse directory and start its stack.
cd clickhouse
docker compose -f compose-clickhouse.yaml up
```

For Podman, use the same files with `podman compose`.

If you run multiple stacks at the same time, override conflicting host ports such as `GRAFANA_PORT`, `CLICKSTACK_UI_PORT`, `OTEL_COLLECTOR_GRPC_PORT`, and `OTEL_COLLECTOR_HTTP_PORT`.
OTLP means OpenTelemetry Protocol.
By default, port `4317` carries OTLP over gRPC, and port `4318` carries OTLP over HTTP.

Each storage-stack directory is self-contained.
You can also copy only the selected storage-stack directory from the installation directory to a working directory.

## 2. Stop

Stop the selected Compose stack from the directory in the working copy that was used to start it:

```bash
# Stop and remove the OpenSearch validation containers and network.
docker compose -f compose-opensearch.yaml down
```

For Podman, use the same Compose file with `podman compose`.

The `down` command stops and removes the local validation containers and network.
It does not delete bind-mounted data directories.
Starting the same storage stack again with the same data directories reuses the previous data.
Delete those directories only when you want to discard the stored data.
See the storage-stack README for the exact directory names.

## 3. Telemetry Endpoints

Choose the telemetry endpoint according to where the NestDAQ process runs.
The same rule applies to NestDAQ device processes and `daq-webctl`.

First, select the destination host according to the sender location and storage stack in [Table 1](#tbl-destination-hosts-en).

<a id="tbl-destination-hosts-en"></a>
**Table 1: Destination host by sender location and storage stack.**

| Sender location | OpenSearch/Victoria destination host | ClickStack destination host |
| :-- | :-- | :-- |
| Host process using published ports | `localhost` | `localhost` |
| Container in the same Compose network | `otel-collector` | `clickstack` |
| Docker container outside the Compose network | `host.docker.internal` | `host.docker.internal` |
| Podman container outside the Compose network | `host.containers.internal` | `host.containers.internal` |

Then construct the endpoint with the protocol port and format in [Table 2](#tbl-otlp-endpoints-en).

<a id="tbl-otlp-endpoints-en"></a>
**Table 2: Default ports and endpoint formats by OTLP protocol.**

| Protocol | Default port | Endpoint format |
| :-- | :-- | :-- |
| OTLP gRPC | `4317` | `<host>:4317` |
| OTLP HTTP | `4318` | `http://<host>:4318/v1/<signal>` |

For OTLP HTTP, replace `<signal>` with `logs`, `metrics`, or `traces` according to the data being sent.
For example, when a host process sends data to the OpenTelemetry Collector in the OpenSearch stack, the OTLP gRPC endpoint is `localhost:4317`.
The OTLP HTTP endpoint is `http://localhost:4318/v1/logs` for logs or `http://localhost:4318/v1/traces` for traces.

## 4. Common Compose-File Settings

All stacks use pinned image defaults.
You can override the images with environment variables documented in each storage stack's README.

All Compose files already include the `:Z` label option on their bind mounts.
On Security-Enhanced Linux (SELinux)-enabled systems, Docker or Podman relabels each path for private use by the container.
No Compose-file changes are normally required.
