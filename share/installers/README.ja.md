# パッケージインストーラー補助スクリプト

[English](README.md) | [日本語](README.ja.md)

[トップ: NestDAQ](../../README.ja.md) | [前へ: ClickStack設定](../otel-collector-compose/clickhouse/README.ja.md) | [次へ: CMake](../../cmake/README.ja.md)

このディレクトリには、ホストのパッケージマネージャーで外部サービスをインストールおよび更新するための補助スクリプトが含まれています。
これらのスクリプトは、ローカルの**Compose**例に代わる方法を提供します。
この文書でComposeとは、`docker compose`または`podman compose`を指します。

これらのスクリプトは、管理者が管理するホストでの利用を想定しています。
rootユーザーとして実行しない場合は`sudo`を使用します。
パッケージは`/usr/`、`/etc/`、パッケージマネージャーのリポジトリディレクトリ、systemdユニットディレクトリなど、システムが管理する場所にインストールされるため、root権限が必要です。

DebianおよびUbuntuでは`apt-get`を使用します。
AlmaLinux、Rocky Linux、RHEL、CentOS、FedoraなどのRHEL系システムでは`dnf`を優先し、`dnf`が利用できない場合は`yum`を使用します。

<a id="1-scripts"></a>
## 1. スクリプト

利用できるインストーラースクリプトを[表1](#tbl-installer-scripts-ja)に示します。

<a id="tbl-installer-scripts-ja"></a>
**表1：パッケージインストーラースクリプト。**

| スクリプト | インストールまたは更新するもの |
| :-- | :-- |
| `install-redis-stack.sh` | RedisパッケージリポジトリからRedisサーバーと[Redis Stack](../../INSTALL.ja.md#redis-server-and-modules)モジュールをインストールします。 |
| `install-otelcol-contrib.sh` | OpenTelemetry公式リリースパッケージからOpenTelemetry Collector Contribをインストールします。 |
| `install-opensearch.sh` | OpenSearch 2.xパッケージリポジトリからOpenSearchをインストールします。 |
| `install-opensearch-dashboards.sh` | OpenSearch 2.xパッケージリポジトリからOpenSearch Dashboardsをインストールします。 |

<a id="2-usage"></a>
## 2. 使用方法

次のいずれかのアクションを指定してスクリプトを実行します。
シェルコマンドの例で`#`から始まる行は読者向けのコメントであり、シェルでは実行されません。

```sh
# 設定済みのパッケージソースを使用してパッケージをインストールします。
./install-redis-stack.sh install
# 同じパッケージソースからパッケージを更新します。
./install-redis-stack.sh upgrade
# 設定とデータを残してパッケージを削除します。
./install-redis-stack.sh uninstall
# スクリプトで使用できるオプションとアクションを表示します。
./install-redis-stack.sh --help
```

デフォルトのアクションは`install`です。
`upgrade`は同じパッケージソースを使用し、パッケージマネージャーにインストール済みパッケージの更新を要求します。
`uninstall`はホストのパッケージマネージャーでパッケージを削除します。

`uninstall`アクションは、パッケージリポジトリファイル、サービス設定、ログ、Redis永続化ファイル、OpenSearchデータのパスを削除しません。
これらのファイルやデータを削除する前に、手動で内容を確認してください。
`systemd`でサービスを管理している場合は、パッケージをアンインストールする前にサービスを停止して無効化してください。
<a href="#6-systemd-management">systemdによる管理</a>を参照してください。

通常の実行では、スクリプトはデフォルトかつ推奨される権限昇格ラッパーとして`sudo`を使用します。
`SUDO=sudo`も有効ですが、デフォルトと同じため指定は不要です。
rootユーザーとして実行すると、スクリプトはラッパーを自動的に省略するため、`SUDO`を設定する必要はありません。
`doas`がインストール済みで適切に設定されている場合に限り、任意の代替として`SUDO=doas`を指定できます。

```sh
# 必要なコマンド用にdoasをインストールして設定済みの場合に限り使用します。
SUDO=doas ./install-opensearch.sh install
```

<a id="3-redis"></a>
## 3. Redis

Redisスクリプトは`packages.redis.io`を登録し、デフォルトでRedis `8.2.7`をインストールします。
Redis 8パッケージでは、デフォルトのパッケージ名は`redis`です。
このパッケージはRedisサーバーとRedis Stackモジュールをインストールしますが、RedisInsightは含みません。
Redis 8.2.7パッケージには、次のモジュールが含まれます。

```text
/usr/lib/redis/modules/redisbloom.so
/usr/lib/redis/modules/redisearch.so
/usr/lib/redis/modules/redistimeseries.so
/usr/lib/redis/modules/rejson.so
```

パッケージマネージャーでRedisリポジトリが現在公開している最新バージョンをインストールまたは更新する場合は、`REDIS_VERSION=latest`を使用します。

```sh
# リポジトリで利用可能な最新のRedisバージョンをインストールします。
REDIS_VERSION=latest ./install-redis-stack.sh install
```

使用するディストリビューションのRedisリポジトリがそのパッケージを提供しており、RedisInsightを含むRedis Stackパッケージを使用する場合に限り、`REDIS_PACKAGE=redis-stack`を使用してください。

```sh
# リポジトリが提供している場合にRedis Stackパッケージをインストールします。
REDIS_PACKAGE=redis-stack ./install-redis-stack.sh install
```

デフォルトの`REDIS_PACKAGE=redis`パッケージでは、`REDIS_VERSION=8.2.7`によるバージョン固定を利用できます。
DebianおよびUbuntuでバージョンを固定したインストールは、公式Redis APTパッケージセットに従い、`redis`、`redis-server`、`redis-sentinel`、`redis-tools`を同じパッケージバージョンでインストールします。
`redis-stack-server`や`redis-stack`などの旧パッケージ名を使用する場合は、`REDIS_VERSION=latest`を設定してください。

デフォルトの`redis`パッケージはRedisInsightをインストールしません。
RedisInsightが必要な場合は、個別のRedisInsightパッケージまたは[`../redis-stack-container/`](../redis-stack-container/README.ja.md)のRedis Stackコンテナヘルパーを使用してください。

RedisはディストリビューションのコードネームまたはRPMリポジトリごとにパッケージを公開しています。
設定されたRedisリポジトリが`REDIS_VERSION`を提供していない場合、インストーラーは別のRedisバージョンをインストールせずに失敗します。

DebianおよびUbuntuでは、Redis公式APTリポジトリがディストリビューションのコードネームごとにパッケージを公開しています。
Debian 12 (`bookworm`)、Debian 13 (`trixie`)、Ubuntu 22.04 (`jammy`)、Ubuntu 24.04 (`noble`) では、固定されたパッケージセットを使用してRedis `7.2.14`、`7.4.9`、`8.2.7`をインストールできます。
現在、Ubuntu 26.04 (`resolute`) では`8.8.0`などの新しいRedisパッケージだけが提供されています。
そのため、`7.2.14`、`7.4.9`、`8.2.7`を固定したインストールはUbuntu 26.04で失敗します。

AlmaLinux/RHEL系システムでは、対応するRocky Linuxメジャーバージョン向けのRedis公式RPMリポジトリを使用します。
Redis公式Rocky LinuxリポジトリはRedis 7.xパッケージを提供していません。
AlmaLinux 9の標準AppStreamは`redis:7`モジュールを通してRedis `7.2.14`を提供しますが、このインストーラーはRedis公式リポジトリを対象とするため、そのパッケージを使用しません。
AlmaLinux 8および9ではRedis公式RPMリポジトリからRedis `8.2.7`をインストールできます。
現在、AlmaLinux 10では`8.8.0`などの新しいRedisパッケージだけが提供されています。
そのため、デフォルトの`REDIS_VERSION=8.2.7`によるインストールはAlmaLinux 10で失敗します。

このインストーラーはAlmaLinux AppStreamモジュールからRedisをインストールしません。
RHEL系システムでは常にRedis公式RPMリポジトリを設定し、ディストリビューションのRedisモジュールを無効にして、パッケージ解決に`packages.redis.io`が使用されるようにします。
次のAppStreamの行は参考情報です。

確認済みのRedisパッケージ提供状況を[表2](#tbl-redis-package-availability-ja)に示します。

<a id="tbl-redis-package-availability-ja"></a>
**表2：ディストリビューション別のRedisパッケージ提供状況。**

| ディストリビューション | リポジトリキー | Redis 7.2.14 | Redis 7.4.9 | Redis 8.2.7 |
| --- | --- | --- | --- | --- |
| AlmaLinux 8 | `rockylinux8` RPMリポジトリ | なし | なし | あり |
| AlmaLinux 9 | `rockylinux9` RPMリポジトリ | なし | なし | あり |
| AlmaLinux 9 | AppStream `redis:7`モジュール (このインストーラーでは不使用) | あり | なし | なし |
| AlmaLinux 10 | `rockylinux10` RPMリポジトリ | なし | なし | なし (`8.8.0`を利用可能) |
| Debian 12 | `bookworm` APTリポジトリ | あり | あり | あり |
| Debian 13 | `trixie` APTリポジトリ | あり | あり | あり |
| Ubuntu 22.04 | `jammy` APTリポジトリ | あり | あり | あり |
| Ubuntu 24.04 | `noble` APTリポジトリ | あり | あり | あり |
| Ubuntu 26.04 | `resolute` APTリポジトリ | なし | なし | なし (`8.8.0`を利用可能) |

公式インストール手順:

- https://redis.io/docs/latest/operate/oss_and_stack/install/install-stack/apt/
- https://redis.io/docs/latest/operate/oss_and_stack/install/install-stack/rpm/

<a id="4-opentelemetry-collector-contrib"></a>
## 4. OpenTelemetry Collector Contrib

OpenTelemetryプロジェクトは、各GitHubリリースでLinuxパッケージを公開しています。
このヘルパーはaptまたはdnfリポジトリを設定しません。
選択したリリースパッケージをダウンロードし、`apt`または`dnf`を通してインストールします。

バージョンの選択には`OTELCOL_CONTRIB_VERSION`を使用します。
デフォルトはローカルCompose例で使用するバージョンに合わせています。

```sh
# 指定したOpenTelemetry Collector Contribリリースをインストールします。
OTELCOL_CONTRIB_VERSION=0.155.0 ./install-otelcol-contrib.sh install
```

インストール後、サービスを起動する前に、パッケージで設定された場所 (通常は`/etc/otelcol-contrib/config.yaml`) へCollector設定を配置するか、既存の設定を編集してください。

公式のインストールおよびリリース手順:

- https://opentelemetry.io/docs/collector/install/
- https://github.com/open-telemetry/opentelemetry-collector-releases/releases

<a id="5-opensearch"></a>
## 5. OpenSearch

OpenSearchスクリプトはOpenSearch 2.xパッケージリポジトリを登録します。
デフォルトではOpenSearch `2.19.5`とOpenSearch Dashboards `2.19.5`をインストールします。
リポジトリで現在公開されている最新バージョンをパッケージマネージャーでインストールまたは更新する場合は、`OPENSEARCH_VERSION=latest`または`OPENSEARCH_DASHBOARDS_VERSION=latest`を使用します。

デフォルトでは、`install-opensearch.sh`はインストール時に`DISABLE_INSTALL_DEMO_CONFIG=true`と`DISABLE_SECURITY_PLUGIN=true`を渡します。
これらの設定により、デモ用管理者パスワードなしでパッケージをインストールできます。
パッケージインストーラーでデモ用セキュリティー設定を行う場合は、`OPENSEARCH_INSTALL_SECURITY=demo`を設定して`OPENSEARCH_INITIAL_ADMIN_PASSWORD`を指定してください。

```sh
# 指定したOpenSearchリリースをインストールします。
OPENSEARCH_VERSION=2.19.5 ./install-opensearch.sh install
# 対応するOpenSearch Dashboardsリリースをインストールします。
OPENSEARCH_DASHBOARDS_VERSION=2.19.5 ./install-opensearch-dashboards.sh install

# デモ用セキュリティー設定と必須の管理者パスワードを指定してOpenSearchをインストールします。
OPENSEARCH_INSTALL_SECURITY=demo \
OPENSEARCH_INITIAL_ADMIN_PASSWORD='change-this-strong-password' \
./install-opensearch.sh install
```

これらのスクリプトはパッケージのみをインストールします。
サービスをネットワークに公開する前に、`/etc/opensearch/`および`/etc/opensearch-dashboards/`内のサービス設定を確認して編集してください。

公式インストール手順:

- https://docs.opensearch.org/latest/install-and-configure/install-opensearch/rpm/
- https://docs.opensearch.org/latest/install-and-configure/install-opensearch/debian/
- https://docs.opensearch.org/latest/install-and-configure/install-dashboards/rpm/
- https://docs.opensearch.org/latest/install-and-configure/install-dashboards/debian/

<a id="6-systemd-management"></a>
## 6. systemdによる管理

パッケージスクリプトはソフトウェアのインストールだけを行います。
`systemd`でサービスを有効化または起動する前に、サービス設定を確認してください。

一般的なサービスコマンド:

```sh
# サービスの現在の状態を表示します。
sudo systemctl status <service>
# サービスを起動時に有効化し、すぐに起動します。
sudo systemctl enable --now <service>
# 設定の変更後に実行中のサービスを再起動します。
sudo systemctl restart <service>
# 起動時の設定を変更せずにサービスを停止します。
sudo systemctl stop <service>
# サービスが起動時に自動起動しないようにします。
sudo systemctl disable <service>
```

想定されるサービス名を[表3](#tbl-systemd-service-unit-names-ja)に示します。

<a id="tbl-systemd-service-unit-names-ja"></a>
**表3：想定されるsystemdサービスユニット名。**

| サービス | ユニット名 |
| :-- | :-- |
| Redis | 通常は`redis-server`。旧Redis Stackパッケージでは`redis-stack-server`の場合があります。 |
| OpenTelemetry Collector Contrib | `otelcol-contrib`。 |
| OpenSearch | `opensearch`。 |
| OpenSearch Dashboards | `opensearch-dashboards`。 |

例:

```sh
# OpenTelemetry Collector Contribサービスを有効化して起動します。
sudo systemctl enable --now otelcol-contrib
# OpenSearchサービスを有効化して起動します。
sudo systemctl enable --now opensearch
# OpenSearch Dashboardsサービスを有効化して起動します。
sudo systemctl enable --now opensearch-dashboards
```

Redisでは、まずパッケージによってインストールされたユニット名を確認してください。

```sh
# このホストにインストールされたRedisユニット名を確認します。
systemctl list-unit-files 'redis*'
# 一般的なユニット名のRedisサービスを有効化して起動します。
sudo systemctl enable --now redis-server
```

RedisInsightを含む`redis-stack`パッケージをインストールする場合は、サービスを有効化する前にインストールされたユニット名を確認してください。
Redis Stackコンテナヘルパーの`run-redis-stack.sh`にもRedisInsightが含まれますが、ホストパッケージ用インストーラースクリプトとは別のものです。

`systemd`で管理されているパッケージをアンインストールする前に、サービスを明示的に停止して無効化してください。
インストーラースクリプトの`uninstall`アクションはホストのパッケージマネージャーでパッケージを削除するだけで、`systemctl`は実行しません。

```sh
# サービスを停止し、起動時に起動しないようにします。
sudo systemctl stop <service>
sudo systemctl disable <service>
# インストーラーヘルパーでパッケージを削除します。
./install-xxx.sh uninstall
# パッケージによるユニットファイルの削除後にsystemdを再読み込みします。
sudo systemctl daemon-reload
# 一致するユニットファイルが残っているか確認します。
systemctl list-unit-files '<service-pattern>'
```

Redisではパッケージやディストリビューションによってユニット名が異なる可能性があるため、最初にインストール済みのユニット名を確認してください。

```sh
# このホストにインストールされたRedisユニット名を確認します。
systemctl list-unit-files 'redis*'
# Redisを停止し、起動時に起動しないようにします。
sudo systemctl stop redis-server
sudo systemctl disable redis-server
# インストーラーヘルパーでRedisパッケージを削除します。
./install-redis-stack.sh uninstall
# パッケージによるユニットファイルの削除後にsystemdを再読み込みします。
sudo systemctl daemon-reload
# Redisユニットファイルが残っているか確認します。
systemctl list-unit-files 'redis*'
```
