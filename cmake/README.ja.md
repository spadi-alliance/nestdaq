# CMakeサポートファイル

[English](README.md) | [日本語](README.ja.md)

[トップ: NestDAQ](../README.ja.md) | [前へ: パッケージインストーラー](../share/installers/README.ja.md) | [次へ: コントリビューションガイドライン](../CONTRIBUTING.ja.md)

このディレクトリには、NestDAQのビルド、インストール済みの`find_package(NestDAQ)`パッケージ、および独立した外部依存関係プロジェクトで使用するCMakeファイルがあります。
ビルドコマンド、依存関係のバージョン、ユーザー向けオプションについては[`INSTALL.ja.md`](../INSTALL.ja.md)を参照してください。

<a id="1-top-level-build-helpers"></a>
## 1. トップレベルビルドヘルパー

トップレベルのビルドヘルパーと用途を[表1](#tbl-top-level-build-helpers-ja)に示します。

<a id="tbl-top-level-build-helpers-ja"></a>
**表1：トップレベルのビルドヘルパーファイル。**

| ファイル | 用途 |
| :-- | :-- |
| `common.cmake` | メインプロジェクトと依存関係プロジェクトで共有する共通ビルド設定。C++規格の確認、警告フラグ、インストール先ディレクトリ、`Threads`、および`ExternalProject_Add`用CMake互換引数を設定します。 |
| `NestDAQBuildSettings.cmake` | 必要に応じて有効にできる`clang-tidy`連携とインストールRPATH設定のヘルパー関数。 |
| `GitHelper.cmake` | プロジェクトのバージョン導出に使用するGitタグ、コミット、ブランチ、変更状態、リモートのメタデータを読み取ります。 |
| `NestDAQExamplesStandalone.cmake` | `examples/`を独立したCMakeプロジェクトとして構成するときに使用する共通設定。 |
| `PatchDoxygenAwesomeCssRefs.cmake` | 生成されたDoxygen HTMLを後処理し、ページがインストール済みの`doxygen-awesome-css`ファイルのパスを参照するようにします。 |
| `DoxygenMermaidFilter.py.in` | Mermaidのネイティブ対応前のDoxygenでも処理できるように、Mermaid fenced blockをHTMLへ変換するfilterを生成します。 |
| `InjectDoxygenMermaidScript.cmake` | Mermaid図を含むDoxygenページへ、Mermaid runtimeと初期化処理を追加します。 |

<a id="2-installed-package-files"></a>
## 2. インストールされるパッケージファイル

[表2](#tbl-installed-package-files-ja)のファイルはNestDAQとともにインストールされ、`find_package(NestDAQ REQUIRED CONFIG)`を呼び出す下流プロジェクトで使用されます。

<a id="tbl-installed-package-files-ja"></a>
**表2：インストールされるCMakeパッケージファイル。**

| ファイル | 用途 |
| :-- | :-- |
| `NestDAQConfig.cmake` | `NestDAQConfig.cmake.in`から生成される、インストール済みパッケージ設定。`CMAKE_PREFIX_PATH`を調整し、FairMQとその依存関係を検索して、バージョンファイルとターゲットファイルを読み込みます。 |
| `NestDAQConfigVersion.cmake` | `find_package(NestDAQ)`用にCMakeが生成するパッケージのバージョン互換性確認ファイル。 |
| `NestDAQVersion.cmake` | `NestDAQVersion.cmake.in`から生成される、インストール済みNestDAQのバージョンおよびGitメタデータ変数。 |
| `NestDAQTargets.cmake` | インポート済みターゲット`NestDAQ::NestDAQ`と、そのインクルードディレクトリ、リンクディレクトリ、リンクライブラリを定義します。 |

`.cmake.in`ファイルはソーステンプレートであり、インストールされません。

<a id="3-external-dependency-project"></a>
## 3. 外部依存関係プロジェクト

`cmake/CMakeLists.txt`は、NestDAQが使用する外部依存関係をビルドおよびインストールする独立したプロジェクトです。
このプロジェクトは`cmake/dependencies/`内のファイルを読み込み、主にCMakeの`ExternalProject_Add`を使用します。
[Redis Stack](../INSTALL.ja.md#redis-server-and-modules)のビルドでは、モジュールのソースツリーを展開するために`FetchContent`も使用します。

`WITH_SPDLOG=ON`の場合、依存関係プロジェクトは最初にインストール済みのspdlogパッケージを検索します。
spdlogが見つからず、かつ`CMAKE_CXX_STANDARD`が`20`未満の場合、このspdlogビルドでは`std::format`の代わりに外部fmtライブラリーを使用するため、fmtも検索します。
適合するfmtパッケージが見つからない場合は、`dependencies/fmt.cmake`がspdlogより先にfmtをビルドしてインストールします。
C++20以降ではspdlogの依存関係ビルドに`std::format`を使用するため、fmtを追加しません。

依存関係ファイルとその用途を[表3](#tbl-external-dependency-files-ja)に示します。

<a id="tbl-external-dependency-files-ja"></a>
**表3：外部依存関係ファイル。**

| ファイル | 用途 |
| :-- | :-- |
| `dependencies/Boost.cmake` | Boostを検索またはビルドします。 |
| `dependencies/ZeroMQ.cmake` | ZeroMQを検索またはビルドします。 |
| `dependencies/FairLogger.cmake` | FairLoggerを検索またはビルドします。 |
| `dependencies/FairMQ.cmake` | FairMQを検索またはビルドします。 |
| `dependencies/Catch2.cmake` | テスト用Catch2を検索またはビルドします。 |
| `dependencies/nlohmann_json.cmake` | nlohmann/jsonを検索またはビルドします。 |
| `dependencies/hiredis.cmake` | hiredisを検索またはビルドします。 |
| `dependencies/redis_plus_plus.cmake` | redis-plus-plusを検索またはビルドします。 |
| `dependencies/opentelemetry-cpp.cmake` | opentelemetry-cppと、選択した機能に応じた転送用依存関係をビルドします。 |
| `dependencies/spdlog.cmake` | spdlogを検索またはビルドします。spdlogが見つからず、C++規格がC++20未満の場合は、`dependencies/fmt.cmake`を通じてfmtも検索またはビルドします。 |
| `dependencies/fmt.cmake` | C++17のspdlog依存関係ビルドで外部フォーマットライブラリーが必要な場合に、fmtを検索するか、ビルドしてインストールします。 |
| `dependencies/redis-stack.cmake` | Redis 8以降向けRedis Stackコンポーネント (Redis、RedisBloom、RediSearch、RedisJSON、RedisTimeSeries) をビルドします。デフォルトのRedis 8.2.9モジュールバージョンはRedis 8.2.9自身が選択しているリリースタグに従います。 |
| `dependencies/redis-server-7.cmake` | スタンドアロンRedisTimeSeriesとともにRedis 7.xサーバーをビルドします。デフォルトでは、Redis 7.4はRedis 7.4.11とRedisTimeSeries 1.12.14を使用し、Redis 7.2はRedis 7.2.16とRedisTimeSeries 1.10.24を使用します。 |
| `dependencies/doxygen-awesome-css.cmake` | 生成ドキュメントで使用するdoxygen-awesome-cssファイルをビルドまたはインストールします。 |
| `dependencies/patch_redisearch.cmake` | Redis Stackの依存関係ビルド中に、ローカルのRediSearch CMake互換パッチを適用します。 |
| `dependencies/patch_redisjson.cmake` | Redis Stackの依存関係ビルド中に、ローカルのRedisJSONビルド互換パッチを適用します。 |
| `dependencies/build_redis-stack_with_temp_rust.sh` | 必要な場合に一時的なRustツールチェーン環境を提供する、Redis Stackビルド用ラッパー。 |
| `dependencies/build_redis-server-7_with_redistimeseries.sh` | Redis 7.xのビルド経路で使用するラッパー。モジュールソースをRedisのソースツリーにコピーせず、RedisTimeSeriesをスタンドアロンモジュールとしてビルドします。 |

依存関係のデフォルトバージョンと、`WITH_REDIS_STACK`、`WITH_REDIS_SERVER_7`、`WITH_OTEL_CPP`、`WITH_SPDLOG`、`BUILD_PARALLEL_LEVEL`などのオプションについては[`INSTALL.ja.md`](../INSTALL.ja.md)に記載されています。
