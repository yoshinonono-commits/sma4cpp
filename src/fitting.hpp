// fitting.hpp -- Sma4Py の fitting.py に対応する最小二乗フィッティング。
//
// Python版は scipy.optimize.curve_fit (Levenberg-Marquardt) に丸投げしていた。
// ここではEigenの行列演算だけを使い、Levenberg-Marquardt法を自分で書く。
// 「ブラックボックスの中に何が入っているか」を体験するのが目的のひとつ。
#pragma once

#include <Eigen/Dense>
#include <string>
#include <utility>
#include <vector>

namespace sma4cpp {

struct FitResult {
    std::string expr;
    std::vector<std::string> params;
    std::vector<double> values;   // 収束値
    std::vector<double> errors;   // 標準誤差 (推定できなければ NaN)
    double chi2 = 0.0;            // 残差二乗和
    double r2 = 0.0;              // 決定係数
    int npoints = 0;

    std::string summary() const;
    Eigen::ArrayXd curve(const Eigen::ArrayXd& x) const;
};

// 最小二乗フィッティングを行う。sigma を渡すと重み付き (Y誤差) フィット。
// 収束しなかった/初期値が壊れている場合は ExprError を投げる。
FitResult fit(const Eigen::ArrayXd& x, const Eigen::ArrayXd& y, const std::string& expr,
              const std::vector<std::string>& params, const std::vector<double>& p0,
              const Eigen::ArrayXd* sigma = nullptr, int max_iterations = 200);

struct Preset {
    std::string name;
    std::string expr;
    std::vector<std::string> params;
};
const std::vector<Preset>& presets();

struct PeakShape {
    std::string name;
    std::vector<std::string> param_names;  // 1ピーク分のパラメータ名 (a, b, c など)
    std::string template_expr;             // "{a}*exp(-((x-{b})/{c})**2)" のような雛形
};
const std::vector<PeakShape>& peak_shapes();

// 多重ピークの式を組み立てる (expr, パラメータ名の並び) を返す。
std::pair<std::string, std::vector<std::string>> build_multipeak(const std::string& shape_name,
                                                                   int n, bool baseline = true);

}  // namespace sma4cpp
