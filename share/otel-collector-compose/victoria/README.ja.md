# Victoria OpenTelemetry (OTel) バックエンド

[English](README.md) | [日本語](README.ja.md)

[トップ: NestDAQ](../../../README.ja.md) | [前の保存先候補: OpenSearch](../opensearch/README.ja.md) | [次の保存先候補: ClickStack](../clickhouse/README.ja.md)

このローカル検証用スタックは、OpenTelemetry CollectorでOpenTelemetryのログ、メトリクス、トレースを受信します。
受信したデータをVictoriaスタックを構成する各サービスに保存し、Grafanaで表示します。

<u><strong>このバックエンドは実験的で、まだ十分に検証されていません。</strong></u>

この文書で**Compose**とは、Docker Compose (`docker compose`) またはPodman Compose (`podman compose`) を指します。
どちらかを使用してこのスタックを管理します。

<a id="1-components"></a>
## 1. コンポーネント

- `otel-collector`: OpenTelemetry Protocol (OTLP) のログ、メトリクス、トレースを受信します。
- `victoriametrics`: メトリクスを保存します。
- `victorialogs`: ログを保存します。
- `victoriatraces`: トレースを保存します。
- `grafana`: 各Victoriaサービス用のExploreビューとダッシュボードを提供します。

<a id="2-before-startup"></a>
## 2. 起動前の設定

インストール先から直接起動せず、[親READMEの起動手順](../README.ja.md#1-start)に従って構成を書き込み可能な作業ディレクトリへコピーしてください。
以下のコマンドは、作業用コピー内の`victoria/`で実行します。

データ保存先を変更する場合は、起動前に対象ディレクトリを作成し、コンテナーから書き込める所有権と権限を設定してください。
Composeファイルに設定済みの`:Z`オプションが、SELinuxラベルを適用します。

<a id="2-1-environment-variables"></a>
### 2.1. 環境変数

Composeの設定に使用できる環境変数を[表1](#tbl-victoria-environment-variables-ja)に示します。

<a id="tbl-victoria-environment-variables-ja"></a>
**表1：Victoria Composeの環境変数。**

| 変数 | デフォルト | 説明 |
| :-- | :-- | :-- |
| `OTEL_COLLECTOR_IMAGE` | `docker.io/otel/opentelemetry-collector-contrib:0.155.0` | コレクターイメージ。 |
| `VICTORIAMETRICS_IMAGE` | `docker.io/victoriametrics/victoria-metrics:v1.143.0` | VictoriaMetricsイメージ。 |
| `VICTORIALOGS_IMAGE` | `docker.io/victoriametrics/victoria-logs:v1.50.0` | VictoriaLogsイメージ。 |
| `VICTORIATRACES_IMAGE` | `docker.io/victoriametrics/victoria-traces:v0.8.2` | VictoriaTracesイメージ。 |
| `GRAFANA_IMAGE` | `docker.io/grafana/grafana:12.4.0` | Grafanaイメージ。 |
| `VICTORIAMETRICS_PORT` | `8428` | VictoriaMetricsに割り当てるホストポート。 |
| `VICTORIALOGS_PORT` | `9428` | VictoriaLogsに割り当てるホストポート。 |
| `VICTORIATRACES_PORT` | `10428` | VictoriaTracesに割り当てるホストポート。 |
| `GRAFANA_PORT` | `3000` | Grafanaに割り当てるホストポート。 |
| `OTEL_COLLECTOR_GRPC_PORT` | `4317` | OTLP gRPCに割り当てるホストポート。 |
| `OTEL_COLLECTOR_HTTP_PORT` | `4318` | OTLP HTTPに割り当てるホストポート。 |
| `VICTORIAMETRICS_DATA_DIR` | `./victoriametrics-data` | VictoriaMetricsデータ用のホストディレクトリ。 |
| `VICTORIALOGS_DATA_DIR` | `./victorialogs-data` | VictoriaLogsデータ用のホストディレクトリ。 |
| `VICTORIATRACES_DATA_DIR` | `./victoriatraces-data` | VictoriaTracesデータ用のホストディレクトリ。 |
| `GRAFANA_DATA_DIR` | `./grafana-data` | `/var/lib/grafana/`にバインドマウントするホストディレクトリ。 |
| `GRAFANA_ADMIN_PASSWORD` | `admin` | Grafana管理者パスワード。 |
| `GRAFANA_PROVISIONING_DIR` | `./grafana/provisioning` | Grafanaプロビジョニングディレクトリ。 |
| `OTEL_COLLECTOR_CONFIG_FILE` | `./otel-collector-config-victoria.yaml` | コレクター設定ファイル。 |

<a id="3-start"></a>
## 3. 起動

以下のシェルコマンド例では、`#`で始まる行は読者向けの説明コメントであり、シェルでは実行されません。

<a id="3-1-docker-compose"></a>
### 3.1. Docker Compose

```bash
# Docker ComposeでVictoriaの検証用スタックを起動します。
docker compose -f compose-victoria.yaml up
```

<a id="3-2-podman-compose"></a>
### 3.2. Podman Compose

```bash
# Podman ComposeでVictoriaの検証用スタックを起動します。
podman compose -f compose-victoria.yaml up
```

<a id="4-verification-and-use"></a>
## 4. 動作確認と利用

<a id="4-1-grafana"></a>
### 4.1. Grafana

スタックの起動後に`http://localhost:3000`を開いてください。
GrafanaにはVictoriaMetrics、VictoriaLogs、VictoriaTracesのデータソースがプロビジョニングによって設定されています。
VictoriaTracesはGrafana組み込みのJaegerデータソースを使用し、次のURLに接続します。

```text
http://victoriatraces:10428/select/jaeger
```

<a id="4-2-ports"></a>
### 4.2. ポート

- VictoriaMetrics: `http://localhost:8428`
- VictoriaLogs: `http://localhost:9428`
- VictoriaTraces: `http://localhost:10428`
- Grafana: `http://localhost:3000`
- OTLP Googleリモートプロシージャコール (gRPC) レシーバー: `localhost:4317`
- OTLP HTTPレシーバー: `http://localhost:4318`

ホストプロセスは上記の`localhost`エンドポイントを使用します。
同じComposeネットワーク内のNestDAQデバイスコンテナーまたは`daq-webctl`コンテナーは、OTLP gRPCには`otel-collector:4317`を、OTLP HTTPには`http://otel-collector:4318`を使用してください。

<a id="4-3-collector-export-endpoints"></a>
### 4.3. コレクターのエクスポート先エンドポイント

コレクターはログ、メトリクス、トレースを次のエンドポイントへエクスポートします。

```text
http://victorialogs:9428/insert/opentelemetry/v1/logs
http://victoriametrics:8428/opentelemetry/v1/metrics
http://victoriatraces:10428/insert/opentelemetry/v1/traces
```

<a id="5-stop"></a>
## 5. 停止

ローカル検証用コンテナーとネットワークを停止して削除します。

<a id="5-1-docker-compose"></a>
### 5.1. Docker Compose

```bash
# Dockerの検証用コンテナーとネットワークを停止して削除します。
docker compose -f compose-victoria.yaml down
```

<a id="5-2-podman-compose"></a>
### 5.2. Podman Compose

```bash
# Podmanの検証用コンテナーとネットワークを停止して削除します。
podman compose -f compose-victoria.yaml down
```

<a id="6-delete-stored-victoria-and-grafana-data"></a>
## 6. 保存済みVictoriaおよびGrafanaデータの削除

`down`ではVictoriaとGrafanaのデータディレクトリを削除しません。
同じデータディレクトリでこのCompose構成を再び起動すると、以前のログ、メトリクス、トレース、およびGrafanaの状態が再利用されます。

保存されたバックエンドデータを破棄したい場合に限り、データディレクトリを削除してください。

```bash
# VictoriaとGrafanaの全データを完全に破棄します。
rm -rf ./victoriametrics-data \
       ./victorialogs-data \
       ./victoriatraces-data \
       ./grafana-data
```

ルートレスPodmanでは、ファイル所有権のためユーザー名前空間経由で削除する必要がある場合があります。

```bash
# ルートレスPodmanのデータをユーザー名前空間経由で破棄します。
podman unshare rm -rf ./victoriametrics-data \
                       ./victorialogs-data \
                       ./victoriatraces-data \
                       ./grafana-data
```
