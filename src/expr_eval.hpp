// expr_eval.hpp -- Sma4Py の expression.py に対応する数式評価。
//
// Python版は Python 自身の eval() を ast モジュールで検証して使っていた。
// C++には「危険だが便利なeval」に相当するものが言語として存在しないため、
// ここでは自作の再帰下降パーサ (recursive descent parser) で式を解析し、
// 独自の小さな評価器で計算する。文法で許可した以外のものはそもそも構文的に
// 受理されないので、Python版のような「ASTを検証して弾く」という後付けの
// 安全対策そのものが不要になる。
//
// 対応する構文は Python 版の expression.py と (ほぼ) 互換:
//   数値, 変数 x/y/i, 定数 pi/e, 関数呼び出し, 四則演算, ** (べき乗),
//   % (mod演算子), 比較演算子, 三項 (A if C else B)。
#pragma once

#include <Eigen/Dense>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sma4cpp {

// 式が壊れている・許可されていない構文/名前を含む場合に投げる。
// Python版の ValueError に相当。
class ExprError : public std::runtime_error {
public:
    explicit ExprError(const std::string& msg) : std::runtime_error(msg) {}
};

namespace expr_detail {
struct Node;
}

// 解析済みの式。パースは一度だけ行い (compile)、以後は eval() を繰り返し呼べる。
// Python版で `ast.parse` を1回だけ行い `eval(code, env)` を繰り返し呼ぶのと同じ構造。
class CompiledExpr {
public:
    // expr: 式の文字列。
    // extra_names: x, y, i, pi, e, 組み込み関数以外に許可する識別子
    //              (フィッティングのパラメータ名 a, b, c... など)。
    static CompiledExpr compile(const std::string& expr,
                                 const std::vector<std::string>& extra_names = {});

    // 配列 x (と任意で y) を使って式全体を評価する。
    // extra_values: extra_names に対応するスカラー値 (パラメータの現在値)。
    Eigen::ArrayXd eval(const Eigen::ArrayXd& x,
                         const Eigen::ArrayXd* y = nullptr,
                         const std::vector<double>* extra_values = nullptr) const;

    CompiledExpr(CompiledExpr&&) noexcept;
    CompiledExpr& operator=(CompiledExpr&&) noexcept;
    ~CompiledExpr();

private:
    explicit CompiledExpr(std::unique_ptr<expr_detail::Node> root,
                           std::vector<std::string> extra_names);
    std::unique_ptr<expr_detail::Node> root_;
    std::vector<std::string> extra_names_;
};

// expression.evaluate(expr, x, y) に相当する簡易関数。
// 変換式 (Series.x_expr / y_expr) や関数の重ね書きの評価に使う。
Eigen::ArrayXd evaluate(const std::string& expr, const Eigen::ArrayXd& x,
                         const Eigen::ArrayXd* y = nullptr);

// expression.make_function(expr, params) に相当。
// フィッティング用に f(x, params...) の関数オブジェクトを作る。
using FitFunction = std::function<Eigen::ArrayXd(const Eigen::ArrayXd&, const std::vector<double>&)>;
FitFunction make_function(const std::string& expr, const std::vector<std::string>& params);

}  // namespace sma4cpp
