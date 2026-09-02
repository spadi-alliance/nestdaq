# Redis Stackコンテナー補助スクリプト

[English](README.md) | [日本語](README.ja.md)

[トップ: NestDAQ](../../README.ja.md) | [前へ: テレメトリー](../../nestdaq/telemetry/README.ja.md) | [次へ: OpenTelemetry Collectorコンテナー設定](../otel-collector-compose/README.ja.md)

このディレクトリには、NestDAQのローカル検証用に[Redis Stack](../../INSTALL.ja.md#redis-server-and-modules)コンテナーを起動する補助スクリプトが含まれています。
これらのコンテナーはホスト上にポートを公開し、デフォルトではRedis認証を有効にしません。
そのまま公開ネットワークや共有ネットワークへ接続すると、Redisポートへ到達できる第三者にデータの読み取り、変更、削除を許すおそれがあります。
RedisInsightを含むイメージでは、そのWeb UI用ポートも公開されます。
これらの補助スクリプトは、信頼できるローカル環境だけで使用してください。

開発やローカルでの確認には、データの表示と操作に使用できるRedisInsightを含むRedis Stackイメージが便利です。
本番配備では、RedisInsightを含まないRedis Stack Serverイメージを推奨します。
サーバー専用イメージを使用すると、本番のRedisと同じコンテナーで実行する必要がないWeb UI、その公開ポート、プロセス、永続データを減らせます。
この選択だけで本番環境が安全になるわけではありません。本番配備では、認証、Transport Layer Security (TLS)、ネットワークアクセス制御、永続化、バックアップを別途設定してください。
イメージの用途については、Redis公式ドキュメントの[DockerでRedis Stackを実行する方法](https://redis.io/docs/latest/operate/oss_and_stack/install/archive/install-stack/docker/)も参照してください。

スクリプトは`latest`ではなく固定されたイメージタグを使用します。

- 開発用Redis Stack (RedisInsightを含む): `docker.io/redis/redis-stack:7.4.0-v8`
- 本番向けRedis Stack Serverのみ: `docker.io/redis/redis-stack-server:7.4.0-v8`
- 公式Redis 8.2.7イメージ: `docker.io/library/redis:8.2.7`
- RedisInsightを含むRedis Stack 7.2: `docker.io/redis/redis-stack:7.2.0-v20`
- Redis Stack Server 7.2のみ: `docker.io/redis/redis-stack-server:7.2.0-v20`

<a id="1-choose-an-image"></a>
## 1. イメージの選択

利用できる補助スクリプトとイメージを[表1](#tbl-helper-script-images-ja)に示します。

<a id="tbl-helper-script-images-ja"></a>
**表1：Redisコンテナー補助スクリプトとイメージ。**

| スクリプト | イメージ | RedisInsight | 備考 |
| :-- | :-- | :-- | :-- |
| `run-redis-8.2.7.sh` | `docker.io/library/redis:8.2.7` | なし | 公式Redisイメージ。Redis 8パッケージにはRedis Stackモジュールが含まれる想定です。起動後に`MODULE LIST`で確認してください。 |
| `run-redis-7.2-stack.sh` | `docker.io/redis/redis-stack:7.2.0-v20` | あり | 開発およびローカルでの確認用のRedis Stack 7.2イメージ系列。 |
| `run-redis-7.2-stack-server.sh` | `docker.io/redis/redis-stack-server:7.2.0-v20` | なし | Redis Stack 7.2のサーバー専用イメージ系列。 |
| `run-redis-stack.sh` | `docker.io/redis/redis-stack:7.4.0-v8` | あり | デフォルトのRedis Stack開発用補助スクリプト。 |
| `run-redis-stack-server.sh` | `docker.io/redis/redis-stack-server:7.4.0-v8` | なし | デフォルトのRedis Stackサーバー専用補助スクリプト。 |

実行中のRedisバージョンと読み込まれたモジュールは、次のコマンドで確認できます。
シェルコマンドの例で`#`から始まる行は読者向けのコメントであり、シェルでは実行されません。

```sh
# 実行中のRedisサーバーのバージョンを表示します。
redis-cli -p 6379 INFO server
# Redisサーバーに読み込まれたモジュールを一覧表示します。
redis-cli -p 6379 MODULE LIST
```

Redis Stack 7.2のイメージタグはRedis Stackのリリースを示しており、Redisサーバーの正確なパッチバージョンを示すタグではありません。
Redisサーバーの正確なパッチバージョンが必要な場合は、起動後に上記のコマンドで確認してください。

<a id="2-start-redis-827"></a>
## 2. Redis 8.2.7の起動

実行:

```sh
# 固定されたRedis 8.2.7コンテナーを起動します。
./run-redis-8.2.7.sh
```

デフォルトのエンドポイント:

- Redis: `localhost:6379`

スクリプトは、スクリプトの隣にある`redis-8.2.7-data/`をコンテナー内の`/data/`へバインドマウントします。
この補助スクリプトは公式Redisイメージを使用するため、`REDIS_ARGS`に指定した追加のRedisサーバー引数をコンテナーコマンドの引数として渡します。

<a id="3-start-redis-stack-72"></a>
## 3. Redis Stack 7.2の起動

RedisInsightを含むRedis Stackを実行します。

```sh
# RedisInsightを含むRedis Stack 7.2を起動します。
./run-redis-7.2-stack.sh
```

Redis Stack Serverだけを実行します。

```sh
# Redis Stack 7.2のサーバー専用コンテナーを起動します。
./run-redis-7.2-stack-server.sh
```

デフォルトのエンドポイント:

- Redis: `localhost:6379`
- RedisInsight: `run-redis-7.2-stack.sh`を使用する場合は`http://localhost:8001`

<a id="4-start-redis-stack-with-redisinsight"></a>
## 4. RedisInsightを含むRedis Stackの起動

実行:

```sh
# RedisInsightを含むデフォルトのRedis Stackコンテナーを起動します。
./run-redis-stack.sh
```

デフォルトのエンドポイント:

- Redis: `localhost:6379`
- RedisInsight: `http://localhost:8001`

スクリプトは、スクリプトの隣にある`redis-stack-data/`をコンテナー内の`/data/`へバインドマウントします。
さらに、`redisinsight-data/`を`/redisinsight/`へバインドマウントします。
RedisInsightは、マウントされたディレクトリ内に内部サブディレクトリを作成できます。

<a id="5-start-redis-stack-server-only"></a>
## 5. Redis Stack Serverのみの起動

実行:

```sh
# デフォルトのRedis Stackサーバー専用コンテナーを起動します。
./run-redis-stack-server.sh
```

デフォルトのエンドポイント:

- Redis: `localhost:6379`

スクリプトは、スクリプトの隣にある`redis-stack-server-data/`をコンテナー内の`/data/`へバインドマウントします。

<a id="6-rerun-behavior"></a>
## 6. 再実行時の動作

デフォルトでは、各スクリプトは新しいコンテナーを起動する前に、設定されたコンテナー名と同じ名前の既存コンテナーを削除します。
そのため、以前の実行が中断された場合や同名のコンテナーが残っている場合でも、スクリプトを再実行できます。
永続化されたRedisデータは、設定されたバインドマウント用データディレクトリまたは名前付きボリュームに残ります。

同名のコンテナーがすでに存在する場合にスクリプトを失敗させるには、`REDIS_CONTAINER_REPLACE=0`を設定します。

<a id="7-security-enhanced-linux-selinux"></a>
## 7. Security-Enhanced Linux (SELinux)

SELinuxラベルオプションは`REDIS_VOLUME_MODE=bind`の場合に限り使用されます。
SELinuxが有効なホストでコンテナーがデータディレクトリへ書き込めるよう、バインドマウントではデフォルトで`:Z`ラベルオプションを使用します。
同じデータディレクトリを複数のコンテナーで共有する場合は、`REDIS_VOLUME_LABEL=z`を設定します。
ラベルオプションを省略するには、`REDIS_VOLUME_LABEL=`を設定します。
RedisInsightを含む補助スクリプトは、RedisとRedisInsightの両方のバインドマウントに同じラベルオプションを適用します。

<a id="8-directory-permissions"></a>
## 8. ディレクトリ権限

デフォルトでは、スクリプトを実行したホストユーザーとしてバインドマウント用データディレクトリを作成し、ディレクトリ権限は変更しません。
ルートレスPodmanでは通常、コンテナーの`root`ユーザーがコンテナーを実行するホストユーザーに対応付けられます。
そのため、作成されたディレクトリは追加の権限変更なしで書き込み可能です。

SELinuxラベル付けとUnix権限は、それぞれ独立した制御です。
`:Z`マウントラベルはSELinuxが有効なホストでコンテナーからディレクトリへのアクセスを許可しますが、ユーザー識別子またはグループ識別子 (uid/gid) の権限不一致は解消しません。
ルートフルコンテナーは、バインドマウントしたディレクトリにホストの`root`ユーザーが所有するファイルを作成する場合があります。
バインドマウントしたディレクトリに書き込めない場合は、この補助スクリプトの外部でホスト側の所有権または権限を明示的に調整してください。

<a id="9-named-volumes"></a>
## 9. 名前付きボリューム

名前付きボリュームの使用は必須ではありません。
補助スクリプトのディレクトリ外でDockerまたはPodmanにRedisデータを管理させる場合は、`REDIS_VOLUME_MODE=volume`を使用します。

ボリュームを確認します。

```sh
# Dockerが管理するボリュームを一覧表示します。
docker volume ls
# Podmanが管理するボリュームを一覧表示します。
podman volume ls
```

ローカルのRedisデータを破棄する場合は名前付きボリュームを削除します。

```sh
# このRedis補助スクリプトが作成したDockerボリュームをすべて削除します。
docker volume rm nestdaq-redis-stack-data nestdaq-redis-stack-redisinsight
docker volume rm nestdaq-redis-stack-server-data
docker volume rm nestdaq-redis-8.2.7-data
docker volume rm nestdaq-redis-7.2-stack-data nestdaq-redis-7.2-stack-redisinsight
docker volume rm nestdaq-redis-7.2-stack-server-data
```

または:

```sh
# このRedis補助スクリプトが作成したPodmanボリュームをすべて削除します。
podman volume rm nestdaq-redis-stack-data nestdaq-redis-stack-redisinsight
podman volume rm nestdaq-redis-stack-server-data
podman volume rm nestdaq-redis-8.2.7-data
podman volume rm nestdaq-redis-7.2-stack-data nestdaq-redis-7.2-stack-redisinsight
podman volume rm nestdaq-redis-7.2-stack-server-data
```

補助スクリプトの隣にRedisデータディレクトリを置かない場合は、名前付きボリュームを使用します。

```sh
# Redisデータをランタイムが管理する名前付きボリュームに保存します。
REDIS_VOLUME_MODE=volume ./run-redis-stack.sh
```

<a id="10-environment-variables"></a>
## 10. 環境変数

各スクリプトは、スクリプトが置かれているディレクトリを`THIS_SCRIPT_DIR`として使用します。
バインドマウント用データディレクトリは`THIS_SCRIPT_DIR`からの相対パスです。
そのため、インストール済みスクリプトをコピーした場合も、バインドマウントされたデータはコピー先のスクリプトの隣に保持されます。

実行時に使用する環境変数を[表2](#tbl-runtime-environment-variables-ja)に示します。

<a id="tbl-runtime-environment-variables-ja"></a>
**表2：Redisコンテナーの実行時環境変数。**

| 変数 | デフォルト | 説明 |
| -------- | ------- | ----------- |
| `CONTAINER_RUNTIME` | `docker` | コンテナーエンジンのコマンド。Podmanを使用する場合は`podman`を設定します。 |
| `REDIS_CONTAINER_NAME` | スクリプト固有の名前 | コンテナー名。 |
| `REDIS_CONTAINER_REPLACE` | `1` | 起動前に同名の既存コンテナーを削除します。削除せずに失敗させる場合は`0`を設定します。 |
| `REDIS_IMAGE` | スクリプト固有の固定イメージ | コンテナーイメージ。 |
| `REDIS_PORT` | `6379` | Redisポート`6379`に割り当てるホストポート。 |
| `REDIS_INSIGHT_PORT` | `8001` | RedisInsightポート`8001`に割り当てるホストポート。RedisInsightを含む補助スクリプトだけで使用します。 |
| `REDIS_CONTAINER_RUN_FLAGS` | `--rm -it` | `docker run`または`podman run`へ渡すフラグ。非対話的な検証には`-d --rm`を使用します。 |
| `REDIS_VOLUME_MODE` | `bind` | ストレージモード。ホストのバインドマウントには`bind`、名前付きボリュームには`volume`を使用します。 |
| `REDIS_DATA_VOLUME` | コンテナー名に基づくボリューム | `/data/`にマウントする名前付きボリューム。`volume`モードだけで使用します。 |
| `REDIS_INSIGHT_VOLUME` | コンテナー名に基づくボリューム | `/redisinsight/`にマウントする名前付きボリューム。RedisInsightを含む補助スクリプトの`volume`モードだけで使用します。 |
| `REDIS_DATA_DIR` | スクリプトの隣のデータディレクトリ | `/data/`にバインドマウントするホストディレクトリ。`bind`モードだけで使用します。 |
| `REDIS_INSIGHT_DATA_DIR` | スクリプト固有のRedisInsightデータディレクトリ | `/redisinsight/`にバインドマウントするホストディレクトリ。RedisInsightを含む補助スクリプトの`bind`モードだけで使用します。 |
| `REDIS_VOLUME_LABEL` | `Z` | SELinuxバインドマウントラベルオプション。`bind`モードだけで使用します。共有ラベル付けには`z`、無効にするには空の値を使用します。 |
| `REDIS_ARGS` | 空文字列（`""`） | 追加のRedisサーバー引数。Redis Stackイメージではイメージの`REDIS_ARGS`環境変数を通して渡され、公式Redis 8.2.7補助スクリプトではコマンド引数として渡されます。 |
| `REDIS_ARGS_MODE` | `env`または`argv` | `run-redis-stack-server.sh`が使用する引数の受け渡しモード。Redis Stackイメージには`env`、公式Redisイメージには`argv`を使用します。 |

例:

```sh
# 任意のポートとパスワード認証を指定してRedis Stackを起動します。
REDIS_PORT=16379 \
REDIS_INSIGHT_PORT=18001 \
REDIS_ARGS="--requirepass nestdaq" \
./run-redis-stack.sh
```

Podmanの場合:

```sh
# Dockerの代わりにPodmanでサーバー専用の補助スクリプトを起動します。
CONTAINER_RUNTIME=podman ./run-redis-stack-server.sh
```

フォアグラウンドコンテナーはCtrl-Cで停止します。
終了時にコンテナーは削除されますが、バインドマウントしたデータディレクトリまたは名前付きボリュームは保持されます。
