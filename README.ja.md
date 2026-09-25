# NestDAQ

[English](README.md) | [日本語](README.ja.md)

[次へ: インストール](INSTALL.ja.md)

NestDAQは、粒子計測向けのストリーミングデータ収集 (DAQ) アプリケーションを構築するためのフレームワークです。
このリポジトリのコードをビルドしてインストールすると、共通機能とツールを利用できますが、それだけでは実際の回路に接続して動作するDAQアプリケーションにはなりません。
計測対象や回路に対応するDAQアプリケーションコードは、このリポジトリの提供範囲外です。

<a id="1-project-guide"></a>
## 1. プロジェクトガイド

<a id="11-user-and-operator-guides"></a>
### 1.1. 利用者および運用者向けガイド

<a id="user-operator-guides-table-ja"></a>
**表1：NestDAQの利用者および運用者向けドキュメント。**

| パス / ドキュメント | 用途 |
| :-- | :-- |
| [INSTALL.ja.md](INSTALL.ja.md) | 前提条件、依存関係のバージョンとビルドオプション、NestDAQのビルドオプション、外部サービス、サンプル、ドキュメント生成。 |
| [examples/](examples/README.ja.md) | `Sampler`、`Sink`、`NullDevice`などのデバイス例、詳細なローカル実行手順、[カスタムユーザーデバイスの作成方法](examples/README.ja.md#4-creating-your-own-user-device)。 |
| [scripts/](scripts/README.ja.md) | プロセス起動およびトポロジー用の補助スクリプトと、デバイスのひな型を生成する`generate-device-skeleton.py`。 |
| [plugins/](plugins/README.ja.md) | DAQサービス、メトリクス、パラメーター設定用のFairMQプラグイン。 |
| [controller/](controller/README.ja.md) | `daq-webctl` HTTP/WebSocketサーバー、Redis制御、テレメトリー設定。 |
| [share/controller/](share/controller/README.ja.md) | `daq-webctl`が配信するウェブブラウザー向けファイル。 |
| [nestdaq/telemetry/](nestdaq/telemetry/README.ja.md) | 必要に応じて有効にできるOpenTelemetry連携。 |
| [share/redis-stack-container/](share/redis-stack-container/README.ja.md) | [Redis Stack](INSTALL.ja.md#redis-server-and-modules)コンテナを実行する補助スクリプト。 |
| [share/otel-collector-compose/](share/otel-collector-compose/README.ja.md) | `docker compose`または`podman compose`で実行するローカルOpenTelemetry Collectorと、[OpenSearch](share/otel-collector-compose/opensearch/README.ja.md)、[Victoria](share/otel-collector-compose/victoria/README.ja.md)、[ClickStack](share/otel-collector-compose/clickhouse/README.ja.md)用のデータ保存および可視化Composeスタック。 |
| [share/installers/](share/installers/README.ja.md) | 外部サービスをホストへインストールする`apt`および`dnf`用補助スクリプト。 |

<a id="12-repository-reference"></a>
### 1.2. リポジトリ構成

<a id="repository-reference-table-ja"></a>
**表2：ソースディレクトリとその役割。**

| パス / ドキュメント | 用途 |
| :-- | :-- |
| `nestdaq/` | バージョンヘッダーのテンプレート、FairMQデバイスアプリケーションのエントリーポイントと`main()`を提供する`runDevice.h`、テレメトリーコードなど、NestDAQのコアヘッダーとソースコード。 |
| `share/` | NestDAQとともにインストールされる設定ファイルと補助ファイル。 |
| [tests/](tests/) | C++テストとテスト用の補助ファイル。 |

<a id="13-developer-guides"></a>
### 1.3. 開発者向けガイド

<a id="developer-guides-table-ja"></a>
**表3：NestDAQ開発者向けドキュメント。**

| パス / ドキュメント | 用途 |
| :-- | :-- |
| [cmake/](cmake/README.ja.md) | CMakeヘルパー、インストールされるパッケージファイル、外部依存関係をビルドするプロジェクト。 |
| [CONTRIBUTING.ja.md](CONTRIBUTING.ja.md) | ブランチ運用方針、コントリビューション手順、フォーマット、静的解析、命名規則。 |

<a id="2-tested-systems"></a>
## 2. 検証済みシステム

<a id="tested-systems-table-ja"></a>
**表4：NestDAQで検証済みのOSおよびツールチェーンのバージョン。**

| ディストリビューション | バージョン | コンパイラー | CMake  | FairMQ |
| ---       | ---     | ---         | ---    | ---    |
| AlmaLinux | 8.10    | GCC 8.5.0   | 3.26.5 | 1.9.2  |
| AlmaLinux | 9.8     | GCC 11.5.0  | 3.31.8 | 1.10.0 |
| AlmaLinux | 10.2    | GCC 14.3.1  | 3.31.8 | 1.10.0 |
| Debian    | 12      | GCC 12.2.0  | 3.25.1 | 1.10.0 |
| Debian    | 13      | GCC 14.2.0  | 3.31.6 | 1.10.0 |
| Ubuntu    | 22.04   | GCC 11.4.0  | 3.22.1 | 1.10.0 |
| Ubuntu    | 24.04   | GCC 13.3.0  | 3.28.3 | 1.10.0 |
| Ubuntu    | 26.04   | GCC 15.2.0  | 4.2.3  | 1.10.0 |

<a id="3-dependencies"></a>
## 3. 依存関係

NestDAQはBoost、FairLogger、FairMQ、hiredis、redis-plus-plusを使用します。
対応する機能を有効にした場合は、opentelemetry-cppやspdlogなどのテレメトリーおよびロギング用の依存関係も使用します。
前提パッケージ、依存関係のデフォルトバージョン、CMakeの上書きオプション、アプリケーションの実行時に使用する外部サービス、プラットフォーム固有のビルド上の制約については、[INSTALL.ja.md](INSTALL.ja.md)を参照してください。
