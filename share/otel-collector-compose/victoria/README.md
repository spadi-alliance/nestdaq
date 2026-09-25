# Victoria OpenTelemetry (OTel) Backend

[English](README.md) | [日本語](README.ja.md)

[Top: NestDAQ](../../../README.md) | [Previous storage option: OpenSearch](../opensearch/README.md) | [Next storage option: ClickStack](../clickhouse/README.md)

This local validation stack uses OpenTelemetry Collector to receive OpenTelemetry logs, metrics, and traces.
It stores the data in Victoria stack services and displays it in Grafana.

<u><strong>This backend is experimental and not yet fully verified.</strong></u>

In this document, **Compose** means either Docker Compose (`docker compose`) or Podman Compose (`podman compose`).
Use either implementation to manage this stack.

<a id="1-components"></a>
## 1. Components

- `otel-collector`: receives OpenTelemetry Protocol (OTLP) logs, metrics, and traces.
- `victoriametrics`: stores metrics.
- `victorialogs`: stores logs.
- `victoriatraces`: stores traces.
- `grafana`: provides Explore views and dashboards for the Victoria services.

<a id="2-before-startup"></a>
## 2. Before startup

Do not start the stack directly from the installation directory.
Follow the [parent README startup procedure](../README.md#1-start) to copy the setup to a writable working directory.
Run the following commands from `victoria/` in that working copy.

When changing a data location, create the target directory before startup and give the container suitable ownership and permissions.
The `:Z` options already present in the Compose file apply the SELinux labels.

<a id="2-1-environment-variables"></a>
### 2.1. Environment variables

The Compose settings available through environment variables are listed in [Table 1](#tbl-victoria-environment-variables-en).

<a id="tbl-victoria-environment-variables-en"></a>
**Table 1: Victoria Compose environment variables.**

| Variable | Default | Description |
| :-- | :-- | :-- |
| `OTEL_COLLECTOR_IMAGE` | `docker.io/otel/opentelemetry-collector-contrib:0.155.0` | Collector image. |
| `VICTORIAMETRICS_IMAGE` | `docker.io/victoriametrics/victoria-metrics:v1.143.0` | VictoriaMetrics image. |
| `VICTORIALOGS_IMAGE` | `docker.io/victoriametrics/victoria-logs:v1.50.0` | VictoriaLogs image. |
| `VICTORIATRACES_IMAGE` | `docker.io/victoriametrics/victoria-traces:v0.8.2` | VictoriaTraces image. |
| `GRAFANA_IMAGE` | `docker.io/grafana/grafana:12.4.0` | Grafana image. |
| `VICTORIAMETRICS_PORT` | `8428` | Host port mapped to VictoriaMetrics. |
| `VICTORIALOGS_PORT` | `9428` | Host port mapped to VictoriaLogs. |
| `VICTORIATRACES_PORT` | `10428` | Host port mapped to VictoriaTraces. |
| `GRAFANA_PORT` | `3000` | Host port mapped to Grafana. |
| `OTEL_COLLECTOR_GRPC_PORT` | `4317` | Host port mapped to OTLP gRPC. |
| `OTEL_COLLECTOR_HTTP_PORT` | `4318` | Host port mapped to OTLP HTTP. |
| `VICTORIAMETRICS_DATA_DIR` | `./victoriametrics-data` | Host directory for VictoriaMetrics data. |
| `VICTORIALOGS_DATA_DIR` | `./victorialogs-data` | Host directory for VictoriaLogs data. |
| `VICTORIATRACES_DATA_DIR` | `./victoriatraces-data` | Host directory for VictoriaTraces data. |
| `GRAFANA_DATA_DIR` | `./grafana-data` | Host directory bind-mounted to `/var/lib/grafana/`. |
| `GRAFANA_ADMIN_PASSWORD` | `admin` | Grafana admin password. |
| `GRAFANA_PROVISIONING_DIR` | `./grafana/provisioning` | Grafana provisioning directory. |
| `OTEL_COLLECTOR_CONFIG_FILE` | `./otel-collector-config-victoria.yaml` | Collector config file. |

<a id="3-start"></a>
## 3. Start

In the shell command examples below, lines beginning with `#` are explanatory comments for the reader and are not executed by the shell.

<a id="3-1-docker-compose"></a>
### 3.1. Docker Compose

```bash
# Start the Victoria validation stack with Docker Compose.
docker compose -f compose-victoria.yaml up
```

<a id="3-2-podman-compose"></a>
### 3.2. Podman Compose

```bash
# Start the Victoria validation stack with Podman Compose.
podman compose -f compose-victoria.yaml up
```

<a id="4-verification-and-use"></a>
## 4. Verification and use

<a id="4-1-grafana"></a>
### 4.1. Grafana

After the stack starts, open `http://localhost:3000`.
Grafana is provisioned with VictoriaMetrics, VictoriaLogs, and VictoriaTraces data sources.
VictoriaTraces uses Grafana's built-in Jaeger data source with the following URL:

```text
http://victoriatraces:10428/select/jaeger
```

<a id="4-2-ports"></a>
### 4.2. Ports

- VictoriaMetrics: `http://localhost:8428`
- VictoriaLogs: `http://localhost:9428`
- VictoriaTraces: `http://localhost:10428`
- Grafana: `http://localhost:3000`
- OTLP Google remote procedure call (gRPC) receiver: `localhost:4317`
- OTLP HTTP receiver: `http://localhost:4318`

Host processes use the `localhost` endpoints above.
A NestDAQ device container or `daq-webctl` container in the same Compose network should use `otel-collector:4317` for OTLP gRPC or `http://otel-collector:4318` for OTLP HTTP.

<a id="4-3-collector-export-endpoints"></a>
### 4.3. Collector export endpoints

The collector exports logs, metrics, and traces to the following endpoints:

```text
http://victorialogs:9428/insert/opentelemetry/v1/logs
http://victoriametrics:8428/opentelemetry/v1/metrics
http://victoriatraces:10428/insert/opentelemetry/v1/traces
```

<a id="5-stop"></a>
## 5. Stop

Stop and remove the local validation containers and network:

<a id="5-1-docker-compose"></a>
### 5.1. Docker Compose

```bash
# Stop and remove the Docker validation containers and network.
docker compose -f compose-victoria.yaml down
```

<a id="5-2-podman-compose"></a>
### 5.2. Podman Compose

```bash
# Stop and remove the Podman validation containers and network.
podman compose -f compose-victoria.yaml down
```

<a id="6-delete-stored-victoria-and-grafana-data"></a>
## 6. Delete stored Victoria and Grafana data

The `down` command does not delete the Victoria and Grafana data directories.
If you start this Compose setup again with the same data directories, the previous logs, metrics, traces, and Grafana state are reused.

Delete the data directories only when you want to discard the stored backend data:

```bash
# Permanently discard all Victoria and Grafana data.
rm -rf ./victoriametrics-data \
       ./victorialogs-data \
       ./victoriatraces-data \
       ./grafana-data
```

For rootless Podman, file ownership may require removal through the user namespace:

```bash
# Discard rootless Podman data through its user namespace.
podman unshare rm -rf ./victoriametrics-data \
                       ./victorialogs-data \
                       ./victoriatraces-data \
                       ./grafana-data
```
