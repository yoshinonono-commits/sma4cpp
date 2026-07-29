# Sma4Cpp

[Sma4Py](https://github.com/yoshinonono-commits/sma4pi) (散布図描画・最小二乗フィッティングのPython/PySide6アプリ) を、
**Windows専用・高速化** を目的として C++ で再実装するプロジェクト。

Pythonとの「プログラミング手法の違いを理解し、高速化を体感する」ことを目的の一つとしている。
そのための質問集を [`docs/PYTHON_VS_CPP_QA.md`](docs/PYTHON_VS_CPP_QA.md) に用意した。
実装したコードを見ながら、この質問集をClaudeにそのまま聞くと理解が深まる。

## 現在のステータス: フェーズ1 (コアのみ)

Sma4Pyの `expression.py` (数式の安全な評価) と `fitting.py` (最小二乗フィッティング)
に相当するコア機能を、GUIに依存しない形で実装済み。GUI (Qt) はまだ無い。

| Python版 | C++版 | 状態 |
|---|---|---|
| `expression.py` | `src/expr_eval.hpp/cpp` | 実装済み。自作の再帰下降パーサ + Eigenでの評価 |
| `fitting.py` | `src/fitting.hpp/cpp` | 実装済み。scipy.curve_fitの代わりに自作Levenberg-Marquardt |
| `model.py` | `src/model.hpp` | データ構造のみ (Series/FitCurve) |
| `canvas.py` / `mainwindow.py` / `dialogs.py` / `interaction.py` | (未着手) | フェーズ2でQt6 + QCustomPlotを使って実装予定 |
| `data_io.py` | (未着手) | フェーズ2 |
| `notation.py` | (未着手) | フェーズ2 (mathtextの代わりにQt側の描画方法を検討) |

コア層 (`expr_eval` / `fitting`) はOS非依存の標準C++なので、開発中はLinux/macOSでも
ビルド・テストできる。最終的なWindows向けGUIアプリの土台として、まずここを固めている。

## ビルドして動作確認する (コアのみ、GUI無し)

```bash
sudo apt install libeigen3-dev cmake g++   # Linuxの場合。Windowsは下記参照
cmake -S . -B build
cmake --build build
./build/test_core          # Python版の tests/test_features.py に相当
```

`test_core` は Python版の `test_features.py` と同じ考え方の非GUIテストで、
数式評価・各種プリセット(直線/Voigt/erf/多重ピーク)のフィッティングが
正しく動くことを確認する。**コードを変更したら必ずこれを実行すること。**

### Windows (最終的なターゲット)

1. [Qt6](https://www.qt.io/download-qt-installer) をインストール (MSVCキット)
2. Eigen は header-only なので [vcpkg](https://vcpkg.io/) 経由が簡単:
   ```powershell
   vcpkg install eigen3
   ```
3. CMake + Visual Studio (MSVC) でビルド:
   ```powershell
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkgのパス>/scripts/buildsystems/vcpkg.cmake
   cmake --build build --config Release
   ```
4. GUI (フェーズ2) を実装したら、Qt6が見つかれば自動的にビルド対象に入る
   (`CMakeLists.txt` の `find_package(Qt6 ...)` 参照)。

## 設計方針 (Python版との違い)

- **ビルド時に依存を組み込む。** Python版は `pip install` で実行時にライブラリを揃えたが、
  C++版はコンパイル時にEigen/Qtを静的/動的にリンクする。「実行時に何を読み込むか」ではなく
  「ビルド時に何を組み込むか」で考える。
- **`eval` の代わりに自作パーサ。** Python版は `ast` で検証した `eval` を使っていたが、
  C++には危険なevalに相当するものが言語として無い。代わりに再帰下降パーサ
  (`src/expr_eval.cpp`) で式を解析するので、そもそも文法的に許可された構文しか
  受理できず、「危険な入力を後から弾く」という発想自体が不要になる。
- **scipy.curve_fit の中身を自分で書く。** Python版は `scipy.optimize.curve_fit`
  (Levenberg-Marquardt) にフィッティングを丸投げしていたが、C++版は
  `src/fitting.cpp` に自前のLM実装がある。数値ヤコビアン・減衰パラメータ・
  正規方程式など、Python版では見えなかった内部が全て見える。
- **式の文法はPython版と同じにしてある。** `a + b*exp(c*x)` のような式は
  Python版・C++版のどちらでも同じ文字列で通じる (三項 `A if C else B` も含む)。
  移植であることを活かして「同じ入力、違う実装」で比較しやすくしている。

## 既知の簡略化 (フェーズ2以降で見直す候補)

- `voigt(x, sigma, gamma)` はPython版の厳密なFaddeeva関数(`scipy.special.wofz`)ではなく、
  疑似Voigt近似 (Thompson-Cox-Hastings, 誤差 ~1%程度) を使っている。追加の依存
  ライブラリを増やさないための判断。
- Undo/Redo、残差プロット、データ点テーブル編集などGUI側の機能はまだ無い
  (GUI自体がまだ無いため)。
- `.s4p` (JSON) ファイルの読み書きは未実装。

## ライセンス・注意

Sma4Winのソースやバイナリは一切含まない、独立実装。
