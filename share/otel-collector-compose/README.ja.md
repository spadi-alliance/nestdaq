# OpenTelemetry Collectorコンテナー構成

[English](README.md) | [日本語](README.ja.md)

[トップ: NestDAQ](../../README.ja.md) | [前へ: Redisコンテナー](../redis-stack-container/README.ja.md) | [次へ: OpenSearch設定](opensearch/README.ja.md)

この文書で**Compose**とは、Docker Compose (`docker compose`) またはPodman Compose (`podman compose`) を指します。
このディレクトリには、ローカル検証用のCompose構成が含まれています。
各構成では、OpenTelemetry CollectorがOpenTelemetryデータを受信し、選択した保存先へ転送します。
この文書で**保存先構成**とは、OpenSearchなどのデータ保存先と、OpenSearch Dashboardsなどの表示ツールを組み合わせた構成を指します。

これらのスタックはローカル検証専用です。
ホスト上にサービスポートを公開し、構成によっては簡易なローカル認証情報を使用します。
公開ネットワークや共有ネットワークには公開しないでください。

各サブディレクトリには、保存先構成を起動するComposeファイルと設定ファイルの例があります。
使用する保存先構成に対応するサブディレクトリのファイルを使用してください。

- [`opensearch/`](opensearch/README.ja.md): OpenSearchにログとトレースを保存し、OpenSearch Dashboardsで表示します。
- [`victoria/`](victoria/README.ja.md): VictoriaLogs、VictoriaMetrics、VictoriaTracesにログ、メトリクス、トレースを保存し、Grafanaで表示します。
  <u><strong>この保存先構成は実験的で、まだ十分に検証されていません。</strong></u>
- [`clickhouse/`](clickhouse/README.ja.md): ClickStackにログ、メトリクス、トレースを保存し、ClickStackユーザーインターフェース (UI) で表示します。
  <u><strong>この保存先構成は実験的で、まだ十分に検証されていません。</strong></u>

起動方法、環境変数、保存データのディレクトリについては、上記リンク先のREADMEファイルを参照してください。

<a id="1-start"></a>
## 1. 起動

インストール先からComposeスタックを直接起動しないでください。
インストール後、インストール済みの構成を、設定ファイルとデータ保存先を変更できる書き込み可能な作業ディレクトリへコピーします。
作業用コピーを使用すると、インストール済みファイルを変更せずに、バインドマウントするデータディレクトリの場所、所有権、権限を実行環境に合わせて調整できます。
SELinuxラベルは、Composeファイルに設定済みの`:Z`オプションが適用します。
`./otel-collector-compose/`がすでに存在する場合は、先に削除するか別のコピー先を選択してください。
以下のシェルコマンド例では、`#`で始まる行は読者向けの説明コメントであり、シェルでは実行されません。

```bash
# インストール済みの構成をコピーし、作業用コピーへ移動します。
cp -a <install-prefix>/share/otel-collector-compose ./otel-collector-compose
cd ./otel-collector-compose
```

作業用コピー内にある保存先構成のディレクトリへ移動し、Composeスタックを1つ起動します。

```bash
# OpenSearchディレクトリへ移動し、そのスタックを起動します。
cd opensearch
docker compose -f compose-opensearch.yaml up
```

```bash
# Victoriaディレクトリへ移動し、そのスタックを起動します。
cd victoria
docker compose -f compose-victoria.yaml up
```

```bash
# ClickHouseディレクトリへ移動し、そのスタックを起動します。
cd clickhouse
docker compose -f compose-clickhouse.yaml up
```

Podmanでは、同じファイルを`podman compose`で使用してください。

複数のスタックを同時に実行する場合は、`GRAFANA_PORT`、`CLICKSTACK_UI_PORT`、`OTEL_COLLECTOR_GRPC_PORT`、`OTEL_COLLECTOR_HTTP_PORT`など、競合するホストポートを上書きしてください。
OTLPはOpenTelemetry Protocolを意味します。
デフォルトでは、ポート`4317`がOTLP gRPC、ポート`4318`がOTLP HTTPです。

各保存先構成のディレクトリは自己完結しています。
使用する保存先構成のディレクトリだけをインストール先から作業ディレクトリへコピーすることもできます。

<a id="2-stop"></a>
## 2. 停止

選択したComposeスタックを、起動時に使用した作業用コピー内のディレクトリから停止します。

```bash
# OpenSearchの検証用コンテナーとネットワークを停止して削除します。
docker compose -f compose-opensearch.yaml down
```

Podmanでは、同じComposeファイルを`podman compose`で使用してください。

`down`コマンドはローカル検証用のコンテナーとネットワークを停止して削除します。
バインドマウントされたデータディレクトリは削除しません。
同じデータディレクトリを使用して同じ保存先構成を再び起動すると、以前のデータが再利用されます。
保存されたデータを破棄したい場合に限り、これらのディレクトリを削除してください。
正確なディレクトリ名は、各保存先構成のREADMEファイルを参照してください。

<a id="3-telemetry-endpoints"></a>
## 3. テレメトリーエンドポイント

NestDAQプロセスを実行する場所に応じて、テレメトリーエンドポイントを選択してください。
同じ規則がNestDAQデバイスプロセスと`daq-webctl`の両方に適用されます。

最初に、[表1](#tbl-destination-hosts-ja)から送信元と保存先構成に応じた接続先ホストを選択します。

<a id="tbl-destination-hosts-ja"></a>
**表1：送信元と保存先構成に応じた接続先ホスト。**

| 送信元の場所 | OpenSearch/Victoriaの接続先ホスト | ClickStackの接続先ホスト |
| :-- | :-- | :-- |
| 公開ポートを使用するホストプロセス | `localhost` | `localhost` |
| 同じComposeネットワーク内のコンテナー | `otel-collector` | `clickstack` |
| Composeネットワーク外のDockerコンテナー | `host.docker.internal` | `host.docker.internal` |
| Composeネットワーク外のPodmanコンテナー | `host.containers.internal` | `host.containers.internal` |

次に、[表2](#tbl-otlp-endpoints-ja)に示すプロトコルのポートと形式でエンドポイントを組み立てます。

<a id="tbl-otlp-endpoints-ja"></a>
**表2：OTLPプロトコルごとのデフォルトポートとエンドポイント形式。**

| プロトコル | デフォルトのポート | エンドポイントの形式 |
| :-- | :-- | :-- |
| OTLP gRPC | `4317` | `<host>:4317` |
| OTLP HTTP | `4318` | `http://<host>:4318/v1/<signal>` |

OTLP HTTPの`<signal>`には、送信するデータに応じて`logs`、`metrics`、`traces`を指定します。
例えば、ホストプロセスからOpenSearch構成のOpenTelemetry Collectorへ送信する場合、OTLP gRPCエンドポイントは`localhost:4317`です。
OTLP HTTPエンドポイントは、ログでは`http://localhost:4318/v1/logs`、トレースでは`http://localhost:4318/v1/traces`です。

<a id="4-backend-details"></a>
## 4. Composeファイルの共通設定

すべてのスタックは固定されたデフォルトのイメージを使用します。
各保存先構成のREADMEファイルに記載された環境変数でイメージを上書きできます。

すべてのComposeファイルは、バインドマウントの指定に`:Z`ラベルオプションをあらかじめ含んでいます。
Security-Enhanced Linux (SELinux) が有効なシステムでは、DockerまたはPodmanが対象パスへコンテナー専用のSELinuxラベルを付け直します。
通常はComposeファイルを変更する必要はありません。
