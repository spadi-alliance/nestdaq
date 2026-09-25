# コントリビューションガイドライン

[English](CONTRIBUTING.md) | [日本語](CONTRIBUTING.ja.md)

[トップ: NestDAQ](README.ja.md) | [前へ: CMake](cmake/README.ja.md)

このドキュメントでは、NestDAQへのコントリビューションにおける推奨事項と禁止事項を説明します。

<a id="forking-workflow"></a>
## フォークを使用した開発手順

このドキュメントで**上流リポジトリ**とは、[github.com/spadi-alliance/nestdaq](https://github.com/spadi-alliance/nestdaq)を指します。

- `main`ブランチにはNestDAQの最新リリース版が含まれ、利用者やその他の非開発者が通常使用します。
- `develop`ブランチには、NestDAQの最新開発版が含まれます。
- 開発を始める前に、上流リポジトリを自身のGitHubアカウントへフォークしてください。
- 開発を始める前に、自身のフォークを上流の`develop`ブランチと同期してください。
- 自身のフォークで変更を行い、コミットを自身のフォークへプッシュしてください。
- 上流リポジトリには作業ブランチを作成しないでください。
- 上流の`main`および`develop`ブランチは保護されており、直接プッシュできません。
- 自身のフォークから、上流の`develop`ブランチを対象としてプルリクエストまたはドラフトプルリクエストを作成してください。
- 上流の`main`ブランチへ変更を反映できるのは、権限を持つメンテナーだけです。
- 上流の`main`を対象とするプルリクエストは、上流の`develop`ブランチから作成するものだけを許可します。
- フォークやその他のブランチから上流の`main`へのプルリクエストは受け付けません。
- 変更が最終レビューの準備段階にない場合でも、早期のフィードバックが有用であればドラフトプルリクエストを使用してください。

<a id="commits-and-pull-requests"></a>
## コミットとプルリクエスト

- 関連のない複数の変更を1つの巨大なコミットにまとめることは避けてください。
- 個別にレビューできる変更は、意図ごとにコミットを分けてください。
- プルリクエストは、慎重にレビューできる規模に保ってください。
- 関連のない変更が多数蓄積するまで待たず、こまめにプルリクエストを作成してください。

<a id="formatting"></a>
## フォーマット

- プルリクエストを作成する前、またはドラフトプルリクエストをレビュー可能な状態へ変更する前に、フォーマッターを適用してください。
- C/C++ファイルには`astyle`を適用してください。
- 変更で触れたファイルだけをフォーマットしてください。
- 関係のないファイルを再フォーマットしないでください。

<a id="static-analysis"></a>
## 静的解析

- プルリクエストを作成する前に`clang-tidy`を実行してください。
- リポジトリ内の[`.clang-tidy`](.clang-tidy)設定を使用してください。
- プルリクエスト自体がclang-tidyの方針に関するものでない限り、プロジェクトのコードに追加のチェックを有効にしないでください。
- CMakeを介して`clang-tidy`を実行するには、`-DNESTDAQ_ENABLE_CLANG_TIDY=ON`を指定して構成してください。

<a id="code-style-and-naming"></a>
## コードスタイルと命名規則

<a id="c"></a>
### C++

- 4個の空白でインデントしてください。

<a id="naming"></a>
### 命名規則

- `PascalCase`と`UpperCamelCase`は同じ命名形式を意味します。
- `class`名および型名: `PascalCase` / `UpperCamelCase`。
- `namespace`名: `snake_case`。
- 関数およびメンバー関数には`lowerCamelCase`を優先します。
  既存スタイルとの一貫性を保つ場合は、`PascalCase` / `UpperCamelCase`も許容します。
- 変数には`snake_case`を優先します。
  既存スタイルとの一貫性を保つ場合は、`lowerCamelCase`も許容します。
- `using`による別名は命名規則の対象外です。
  局所的な可読性、外部ライブラリの規約、または一般的な短縮形に従って構いません。
- `public struct`のデータフィールド: `snake_case`。
- `private`および`protected`の`class`データメンバー: `fPascalCase`。
- `static`データメンバー: `fg`で開始します (例: `fgPascalCase`)。
- `static`変数: `g`で開始します (例: `gPascalCase`)。
- 定数: `k`で開始する`kPascalCase`、または`SCREAMING_SNAKE_CASE`を使用します。
- マクロ名: `SCREAMING_SNAKE_CASE`。
- 列挙定数: `kPascalCase`、`PascalCase` / `UpperCamelCase`、または`SCREAMING_SNAKE_CASE`。
- NestDAQコードの基底`namespace`: `nestdaq`。

<a id="file-naming"></a>
### ファイル名

- 推奨するファイル拡張子: `.cpp`, `.hpp`。
- 許容するファイル拡張子: `.cxx`, `.h`, `.hh`, `.hxx`。
