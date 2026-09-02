# ウェブコントローラー用ブラウザーファイル

[English](README.md) | [日本語](README.ja.md)

[トップ: NestDAQ](../../README.ja.md) | [前へ: ウェブコントローラー](../../controller/README.ja.md) | [次へ: テレメトリー](../../nestdaq/telemetry/README.ja.md)

このディレクトリには、NestDAQウェブコントローラー`daq-webctl`用にインストールされるブラウザーファイルが含まれています。

<a id="1-daq-webctlhtml"></a>
## 1. `daq-webctl.html`

`daq-webctl.html`は、`daq-webctl`が提供するデフォルトのブラウザー用グラフィカルユーザーインターフェース (GUI) です。
インストール処理は、このファイルを`daq-webctl.html`としてコントローラーのドキュメントルートに配置します。

インストール処理は、このファイルを指すシンボリックリンク`index.html`も作成します。
そのため、ユーザーインターフェース (UI) は`/daq-webctl.html`または`/`のどちらからでも開けます。

コントローラーの実装、起動コマンド、Redisの要件、コマンドラインオプション、起動後の動作、およびブラウザー利用時の注意事項については、[`controller/README.ja.md`](../../controller/README.ja.md)を参照してください。
