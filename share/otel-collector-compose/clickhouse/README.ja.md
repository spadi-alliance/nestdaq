# ClickStack OpenTelemetry (OTel) バックエンド

[English](README.md) | [日本語](README.ja.md)

[トップ: NestDAQ](../../../README.ja.md) | [前の保存先候補: Victoria](../victoria/README.ja.md) | [次へ: パッケージインストーラー](../../installers/README.ja.md)

このローカル検証用スタックは、ClickStack OpenTelemetry CollectorでOpenTelemetryのログ、メトリクス、トレースを受信します。
受信したデータをClickHouseに保存し、ClickStackユーザーインターフェース (UI) で表示します。

<u><strong>このバックエンドは実験的で、まだ十分に検証されていません。</strong></u>

この文書で**Compose**とは、Docker Compose (`docker compose`) またはPodman Compose (`podman compose`) を指します。
どちらかを使用してこのスタックを管理します。

<a id="1-components"></a>
## 1. コンポーネント

- `clickstack`: ClickStack UI、OpenTelemetry Collector、ClickHouseを1つのコンテナーで実行します。

このスタックはローカル検証用です。
本番環境では、明示的な認証情報、保持ポリシー、バックアップポリシー、およびこのサンプルComposeファイルの外部で管理する配備トポロジーを使用してください。

<a id="2-before-startup"></a>
## 2. 起動前の設定

インストール先から直接起動せず、[親READMEの起動手順](../README.ja.md#1-start)に従って構成を書き込み可能な作業ディレクトリへコピーしてください。
以下のコマンドは、作業用コピー内の`clickhouse/`で実行します。

データ保存先を変更する場合は、起動前に対象ディレクトリを作成し、コンテナーから書き込める所有権と権限を設定してください。
Composeファイルに設定済みの`:Z`オプションが、SELinuxラベルを適用します。

<a id="2-1-environment-variables"></a>
### 2.1. 環境変数

Composeの設定に使用できる環境変数を[表1](#tbl-clickstack-environment-variables-ja)に示します。

<a id="tbl-clickstack-environment-variables-ja"></a>
**表1：ClickStack Composeの環境変数。**

| 変数 | デフォルト | 説明 |
| :-- | :-- | :-- |
| `CLICKSTACK_IMAGE` | `docker.io/clickhouse/clickstack-all-in-one:2` | ClickStack一体型イメージ。 |
| `CLICKSTACK_UI_PORT` | `8080` | ClickStack UIに割り当てるホストポート。 |
| `CLICKHOUSE_HTTP_PORT` | `8123` | ClickHouse HTTPに割り当てるホストポート。 |
| `OTEL_COLLECTOR_GRPC_PORT` | `4317` | OTLP gRPCに割り当てるホストポート。 |
| `OTEL_COLLECTOR_HTTP_PORT` | `4318` | OTLP HTTPに割り当てるホストポート。 |
| `CLICKSTACK_DB_DIR` | `./clickstack-db` | `/data/db/`にバインドマウントするホストディレクトリ。 |
| `CLICKSTACK_CLICKHOUSE_DATA_DIR` | `./clickstack-clickhouse-data` | `/var/lib/clickhouse/`にバインドマウントするホストディレクトリ。 |
| `CLICKSTACK_CLICKHOUSE_LOG_DIR` | `./clickstack-clickhouse-logs` | `/var/log/clickhouse-server/`にバインドマウントするホストディレクトリ。 |

<a id="3-start"></a>
## 3. 起動

以下のシェルコマンド例では、`#`で始まる行は読者向けの説明コメントであり、シェルでは実行されません。

<a id="3-1-docker-compose"></a>
### 3.1. Docker Compose

```bash
# Docker ComposeでClickStackの検証用スタックを起動します。
docker compose -f compose-clickhouse.yaml up
```

<a id="3-2-podman-compose"></a>
### 3.2. Podman Compose

```bash
# Podman ComposeでClickStackの検証用スタックを起動します。
podman compose -f compose-clickhouse.yaml up
```

<a id="4-verification-and-use"></a>
## 4. 動作確認と利用

<a id="4-1-clickstack-ui"></a>
### 4.1. ClickStack UI

スタックの起動後に`http://localhost:8080`でClickStack UIを開いてください。
初回利用時にUIユーザーを作成します。
ClickStackはローカルのClickHouseインスタンスに接続し、ログ、メトリクス、トレース用のデータソースを準備します。

<a id="4-2-ports"></a>
### 4.2. ポート

- ClickStack UI: `http://localhost:8080`
- ClickHouse HTTP: `http://localhost:8123`
- OpenTelemetry Protocol (OTLP) Googleリモートプロシージャコール (gRPC) レシーバー: `localhost:4317`
- OTLP HTTPレシーバー: `http://localhost:4318`

<a id="4-3-nestdaq-telemetry-endpoints"></a>
### 4.3. NestDAQテレメトリーエンドポイント

ホストプロセスはOTLP/gRPCに`localhost:4317`、OTLP/HTTPに`http://localhost:4318`を使用します。
同じComposeネットワーク内のNestDAQデバイスコンテナーまたは`daq-webctl`コンテナーは、OTLP gRPCに`clickstack:4317`、OTLP HTTPに`http://clickstack:4318`を使用してください。

例えば、HTTPエンドポイントでは次のパスを使用します。

```text
http://localhost:4318/v1/logs
http://localhost:4318/v1/metrics
http://localhost:4318/v1/traces
```

<a id="5-stop"></a>
## 5. 停止

ローカル検証用コンテナーとネットワークを停止して削除します。

<a id="5-1-docker-compose"></a>
### 5.1. Docker Compose

```bash
# Dockerの検証用コンテナーとネットワークを停止して削除します。
docker compose -f compose-clickhouse.yaml down
```

<a id="5-2-podman-compose"></a>
### 5.2. Podman Compose

```bash
# Podmanの検証用コンテナーとネットワークを停止して削除します。
podman compose -f compose-clickhouse.yaml down
```

<a id="6-delete-stored-data"></a>
## 6. 保存済みClickStackおよびClickHouseデータの削除

`down`ではClickStackとClickHouseのデータディレクトリおよびログディレクトリを削除しません。
同じディレクトリでこのCompose構成を再び起動すると、以前のバックエンドデータが再利用されます。

保存されたバックエンドデータを破棄したい場合に限り、データディレクトリとログディレクトリを削除してください。

```bash
# ClickStackとClickHouseの全データおよびログを完全に破棄します。
rm -rf ./clickstack-db \
       ./clickstack-clickhouse-data \
       ./clickstack-clickhouse-logs
```

ルートレスPodmanでは、ファイル所有権のためユーザー名前空間経由で削除する必要がある場合があります。

```bash
# ルートレスPodmanのデータとログをユーザー名前空間経由で破棄します。
podman unshare rm -rf ./clickstack-db \
                       ./clickstack-clickhouse-data \
                       ./clickstack-clickhouse-logs
```
