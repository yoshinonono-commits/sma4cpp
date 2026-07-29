# Python → C++ 移植 質問スクリプト

Sma4Py (Python) と Sma4Cpp (このリポジトリ) を見比べながら、Claude に
そのまま聞くと理解が深まる質問集。実際に書いたコードの具体的な箇所を
指しているので、一般論ではなく「このプロジェクトのこの部分はなぜこうなっているか」
という答えが返ってくるはず。

使い方: 気になるセクションの質問をそのままコピーしてClaudeに送るだけでよい。
「◯◯ファイルの◯◯行目を見て」と付け加えるとさらに具体的な説明になる。

---

## 1. 全体設計・ビルドの違い

1. Pythonは `pip install -r requirements.txt` で実行時に依存を揃えていたが、
   C++版の `CMakeLists.txt` は依存(Eigen, 将来のQt6)をどう扱っている?
   「実行時に読み込む」と「ビルド時に組み込む」の違いを、このリポジトリの
   具体的なファイルを指して説明して。
2. `python -m sma4py` は即座に起動できたが、C++版は `cmake` → `cmake --build`
   という2段階が必要。この「コンパイル」というステップが開発のやり方
   (試行錯誤のサイクル)にどう影響する?
3. Python版はGUI環境が無くても `tests/test_features.py` が動く設計だった
   (`CLAUDE.md` の「ロジック層はPySide6に依存しない」という約束ごと)。
   C++版の `src/expr_eval.*` / `src/fitting.*` がQtに一切依存していないのは
   同じ理由。この境界を保つメリットを、実際にこのリポジトリでLinux上で
   `test_core` が動いていることと結び付けて説明して。

## 2. メモリ管理・型システムの違い

4. `model.py` の `Series` は `@dataclass` でPythonのGCに任せていたが、
   `src/model.hpp` の `Series` は普通の `struct`。`std::vector<double> x` は
   スタック上の変数として見えるが、実際のデータはどこに置かれる?
   コピーしたときと参照(`&`)で渡したときで何が起きるかも教えて。
5. `expression.py` は `x = np.asarray(x, dtype=float)` のように実行時に型を
   決めていたが、`expr_eval.hpp` の関数は `Eigen::ArrayXd` という型を
   コンパイル時に固定している。もし関数に整数の配列を渡そうとしたら
   Pythonと C++でそれぞれ何が起きる?
6. `std::optional<std::vector<double>>` (`model.hpp` の `xerr`/`yerr`) は
   Pythonの `Optional[np.ndarray] = None` と何が違う? 特に「値が無い」
   ことをメモリ上でどう表現しているかに注目して。

## 3. 自作の数式パーサ (`src/expr_eval.cpp`)

7. Python版の `expression.py` は `ast.parse` + `_validate` で危険な構文を
   弾いていた。C++版はPythonのような `eval` 自体が言語に無いので、代わりに
   何をしている? `expr_detail::Lexer` と `expr_detail::Parser` の役割分担を
   説明して。
8. `Node` 構造体は `enum class NodeType` とデータを1つのstructにまとめている
   (タグ付きバリアント方式)。Pythonの `ast` モジュールはノードごとに
   専用クラスがある。C++でも継承+仮想関数でノードクラスを分ける書き方が
   できたはずだが、あえてタグ付きstructにした理由・トレードオフは?
9. `**` を右結合にするための `parse_power()` の再帰の書き方を、
   具体的に `2**3**2` を渡した場合の関数呼び出しの流れで説明して。
10. `mod()` 関数と `%` 演算子の実装で `floored_mod()` という自作関数を
    使っている理由は? `std::fmod` をそのまま使わなかった理由を、
    `-1 % 3` のPythonでの結果とC++の `std::fmod(-1,3)` の結果を比べて説明して。
11. `validate()` 関数は式全体を1回だけ木構造で走査して未知の名前を検出する。
    これはPython版の `_validate()` の `ast.walk()` と同じ考え方だが、
    C++側では再帰関数として自分で書く必要がある。この「言語が用意して
    くれるか、自分で書くか」の違いを説明して。

## 4. フィッティングの中身 (`src/fitting.cpp`)

12. Python版は `scipy.optimize.curve_fit` の中身を見ることは無かったが、
    C++版では `levenberg_marquardt()` という関数として実装が丸見えになった。
    このコードのどこが「ガウス・ニュートン法」で、どこが
    「Levenberg-Marquardt法(減衰項)」なのか説明して。
13. 実装中に実際に踏んだバグとして、`Jtr = J.transpose() * r` の符号を
    忘れて `Jtr = -(J.transpose() * r)` にしなかったため、フィットが
    一切収束しなかった (常に初期値のまま) ことがあった。
    `fitting.cpp` の該当コメントを見て、なぜ符号を忘れると「登り方向に進む」
    ことになるのか、数式で説明して。
14. `numerical_jacobian()` は前進差分でヤコビアンを近似している。
    Python版では `scipy.optimize.curve_fit` が解析的ヤコビアンを渡さない限り
    同じことを内部でしていたが、それは見えなかった。数値微分の刻み幅
    `eps * std::max(1.0, std::fabs(p(j)))` はなぜこう決めている? 刻み幅が
    小さすぎる/大きすぎるとそれぞれ何が起きる?
15. `fit()` 関数の中で、`sigma` (Y誤差) を渡したときと渡さなかったときで
    標準誤差の計算 (`scale` 変数) が変わる。これは Python版が
    `curve_fit(..., absolute_sigma=sigma is not None)` としていたことに
    対応する。なぜ「誤差を渡さないとき」は残差分散でスケールし直す必要が
    あるのか説明して。
16. `Eigen::CompleteOrthogonalDecomposition` で疑似逆行列を計算している
    (`decomp.pseudoInverse()`)。単純に `.inverse()` を使わなかった理由を、
    パラメータ数がデータに対して過剰だったり、パラメータ同士が強く
    相関している場合 (`J^T J` が特異に近い場合) を例に説明して。

## 5. Voigt関数の近似について

17. `voigt_fn()` はPython版の `scipy.special.wofz` を使った厳密な計算では
    なく、疑似Voigt近似 (Thompson-Cox-Hastings) を使っている。
    なぜ「同じ結果になるコード」を書かなかったのか (依存ライブラリの話)、
    近似の誤差がどの程度で、どういう場面で問題になり得るか説明して。
18. `voigt_fn()` だけ他の関数と違って `for` ループで1点ずつ計算している
    (他は `unaryExpr`/`binaryExpr` でベクトル化している)。なぜここだけ
    ループにしたのか、ベクトル化するとしたらどう書き換えられるか説明して。

## 6. 次のフェーズ (Qt GUI) に向けて

19. Python版の `canvas.py` は `matplotlib` の `Axes` に描画していた。
    C++版でQt6 + QCustomPlot (または Qt Charts) を使う場合、
    `matplotlib.Figure/Axes` は何に対応する? 描画のたびに `ax.clear()` して
    全部描き直していたPython版のやり方は、C++/Qtでもそのまま使えるか、
    それとも作り方を変えるべきか説明して。
20. Python版の `interaction.py` は matplotlib のマウスイベント
    (`button_press_event` など) をハンドラに繋いでいた。Qtでは
    `QWidget::mousePressEvent` のようなイベントハンドラのオーバーライドに
    なる。「コールバック関数を渡す」スタイルと「オーバーライドする」
    スタイルの違いを、実際にこのあとGUIを書くときにどちらが自然になるか
    という観点で説明して。
21. Python版は `.s4p` をJSON (`json.dump`/`json.load`) で読み書きしていた。
    C++でJSONを扱うには標準ライブラリだけでは足りない (別途ライブラリが必要)。
    どのライブラリを使うのが妥当か、Windows専用という制約でおすすめは
    変わるか説明して。
