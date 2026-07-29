#include "fitting.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>

#include "expr_eval.hpp"

namespace sma4cpp {
namespace {

using ResidualFn = std::function<Eigen::VectorXd(const Eigen::VectorXd&)>;

// 数値ヤコビアン (前進差分)。scipy も解析的ヤコビアンを与えない限り内部で
// これと同じことをしている。Python版では見えなかった処理がここで露出する。
Eigen::MatrixXd numerical_jacobian(const ResidualFn& residual_fn, const Eigen::VectorXd& p,
                                    const Eigen::VectorXd& r0) {
    const double eps = 1e-6;
    Eigen::MatrixXd J(r0.size(), p.size());
    for (Eigen::Index j = 0; j < p.size(); ++j) {
        Eigen::VectorXd p2 = p;
        double h = eps * std::max(1.0, std::fabs(p(j)));
        p2(j) += h;
        Eigen::VectorXd r2 = residual_fn(p2);
        J.col(j) = (r2 - r0) / h;
    }
    return J;
}

struct LmResult {
    Eigen::VectorXd params;
    Eigen::MatrixXd jtj;  // 収束点での J^T J (共分散の計算に使う)
    bool converged = false;
};

// 減衰付きガウス・ニュートン法 (Levenberg-Marquardt)。
// レポジトリ全体の中で唯一「自分で線形代数を書く」部分。
LmResult levenberg_marquardt(const ResidualFn& residual_fn, Eigen::VectorXd p, int max_iter) {
    Eigen::VectorXd r = residual_fn(p);
    double cost = r.squaredNorm();
    double lambda = 1e-3;
    constexpr double kLambdaUp = 10.0;
    constexpr double kLambdaDown = 10.0;
    Eigen::MatrixXd JtJ = Eigen::MatrixXd::Zero(p.size(), p.size());

    for (int iter = 0; iter < max_iter; ++iter) {
        Eigen::MatrixXd J = numerical_jacobian(residual_fn, p, r);
        JtJ = J.transpose() * J;
        // ||r + J*delta||^2 を最小化する delta は (J^T J) delta = -J^T r の解。
        // マイナスを忘れると勾配の逆方向 (登り方向) に進んでしまい、一切収束しない。
        Eigen::VectorXd Jtr = -(J.transpose() * r);

        bool improved = false;
        for (int attempt = 0; attempt < 30; ++attempt) {
            Eigen::MatrixXd A = JtJ;
            for (Eigen::Index d = 0; d < A.rows(); ++d) {
                A(d, d) += lambda * std::max(JtJ(d, d), 1e-12);
            }
            Eigen::VectorXd delta = A.ldlt().solve(Jtr);
            if (!delta.allFinite()) {
                lambda *= kLambdaUp;
                continue;
            }
            Eigen::VectorXd p_new = p + delta;
            Eigen::VectorXd r_new = residual_fn(p_new);
            double cost_new = r_new.squaredNorm();

            if (std::isfinite(cost_new) && cost_new < cost) {
                double rel_change = (cost - cost_new) / std::max(cost, 1e-300);
                p = p_new;
                r = r_new;
                cost = cost_new;
                lambda = std::max(lambda / kLambdaDown, 1e-12);
                improved = true;
                if (rel_change < 1e-12) {
                    Eigen::MatrixXd J_final = numerical_jacobian(residual_fn, p, r);
                    return {p, J_final.transpose() * J_final, true};
                }
                break;
            }
            lambda *= kLambdaUp;
            if (lambda > 1e12) break;
        }
        if (!improved) {
            return {p, JtJ, true};  // これ以上下げられない = 収束とみなす
        }
    }
    return {p, JtJ, false};  // max_iter に達した
}

}  // namespace

std::string FitResult::summary() const {
    std::ostringstream ss;
    ss << "式: y = " << expr << "\n";
    ss << "データ点数: " << npoints << "\n\n";
    for (size_t i = 0; i < params.size(); ++i) {
        ss << params[i] << " = " << values[i];
        if (std::isfinite(errors[i])) ss << "  +/- " << errors[i];
        ss << "\n";
    }
    ss << "\nchi2 (残差二乗和) = " << chi2 << "\n";
    ss << "決定係数 R^2      = " << r2 << "\n";
    return ss.str();
}

Eigen::ArrayXd FitResult::curve(const Eigen::ArrayXd& x) const {
    auto f = make_function(expr, params);
    return f(x, values);
}

FitResult fit(const Eigen::ArrayXd& x_in, const Eigen::ArrayXd& y_in, const std::string& expr,
              const std::vector<std::string>& params, const std::vector<double>& p0,
              const Eigen::ArrayXd* sigma_in, int max_iterations) {
    std::vector<double> xs, ys, ss;
    for (Eigen::Index i = 0; i < x_in.size(); ++i) {
        if (std::isfinite(x_in(i)) && std::isfinite(y_in(i))) {
            xs.push_back(x_in(i));
            ys.push_back(y_in(i));
            if (sigma_in) ss.push_back((*sigma_in)(i));
        }
    }
    if (static_cast<int>(xs.size()) < static_cast<int>(params.size())) {
        throw ExprError("データ点が " + std::to_string(xs.size()) + " 個しかなく、パラメータ " +
                         std::to_string(params.size()) + " 個を決められません。");
    }

    Eigen::ArrayXd x = Eigen::Map<Eigen::ArrayXd>(xs.data(), static_cast<Eigen::Index>(xs.size()));
    Eigen::ArrayXd y = Eigen::Map<Eigen::ArrayXd>(ys.data(), static_cast<Eigen::Index>(ys.size()));
    bool has_sigma = sigma_in != nullptr;
    Eigen::ArrayXd sigma;
    if (has_sigma) sigma = Eigen::Map<Eigen::ArrayXd>(ss.data(), static_cast<Eigen::Index>(ss.size()));

    FitFunction f = make_function(expr, params);

    // 初期値で一度評価し、式が壊れていないか先に確かめる (Python版と同じ安全策)。
    {
        Eigen::ArrayXd test = f(x, p0);
        for (Eigen::Index i = 0; i < test.size(); ++i) {
            if (!std::isfinite(test(i))) {
                throw ExprError("初期値で計算すると無限大または NaN になります。初期値を見直してください。");
            }
        }
    }

    ResidualFn residual_fn = [&](const Eigen::VectorXd& p) -> Eigen::VectorXd {
        std::vector<double> pv(p.data(), p.data() + p.size());
        Eigen::ArrayXd model = f(x, pv);
        Eigen::ArrayXd diff = y - model;
        if (has_sigma) diff = diff / sigma;
        return diff.matrix();
    };

    Eigen::VectorXd p0_vec = Eigen::Map<const Eigen::VectorXd>(p0.data(), static_cast<Eigen::Index>(p0.size()));
    LmResult lm = levenberg_marquardt(residual_fn, p0_vec, max_iterations);

    std::vector<double> popt(lm.params.data(), lm.params.data() + lm.params.size());
    Eigen::ArrayXd model_final = f(x, popt);
    Eigen::ArrayXd resid = y - model_final;
    double chi2 = (resid * resid).sum();
    double ymean = y.mean();
    Eigen::ArrayXd centered = y - ymean;
    double ss_tot = (centered * centered).sum();
    double r2 = ss_tot > 0 ? 1.0 - chi2 / ss_tot : std::numeric_limits<double>::quiet_NaN();

    // 標準誤差: sigma を渡したときは絶対値として信頼し、渡さなかったときは
    // 残差分散でスケールする (scipy.curve_fit の absolute_sigma と同じ考え方)。
    int dof = static_cast<int>(x.size()) - static_cast<int>(params.size());
    double scale = 1.0;
    if (!has_sigma && dof > 0) scale = chi2 / dof;

    Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd> decomp(lm.jtj);
    Eigen::MatrixXd cov = decomp.pseudoInverse() * scale;

    std::vector<double> errors(params.size(), std::numeric_limits<double>::quiet_NaN());
    for (size_t i = 0; i < params.size(); ++i) {
        double v = cov(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(i));
        if (std::isfinite(v) && v >= 0) errors[i] = std::sqrt(v);
    }

    FitResult result;
    result.expr = expr;
    result.params = params;
    result.values = popt;
    result.errors = errors;
    result.chi2 = chi2;
    result.r2 = r2;
    result.npoints = static_cast<int>(x.size());
    return result;
}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> table = {
        {"直線 a + b*x", "a + b*x", {"a", "b"}},
        {"2次 a + b*x + c*x^2", "a + b*x + c*x**2", {"a", "b", "c"}},
        {"指数 a*exp(b*x)", "a*exp(b*x)", {"a", "b"}},
        {"指数+定数 a + b*exp(c*x)", "a + b*exp(c*x)", {"a", "b", "c"}},
        {"べき乗 a*x^b", "a*x**b", {"a", "b"}},
        {"ガウス a*exp(-((x-b)/c)^2)", "a*exp(-((x-b)/c)**2)", {"a", "b", "c"}},
        {"ローレンツ a/((x-b)^2+c)", "a/((x-b)**2+c)", {"a", "b", "c"}},
        {"対数 a + b*log(x)", "a + b*log(x)", {"a", "b"}},
        {"Voigt a*voigt(x-b,s,g)", "a*voigt(x-b, s, g)", {"a", "b", "s", "g"}},
        {"誤差関数 a + b*erf((x-c)/d)", "a + b*erf((x-c)/d)", {"a", "b", "c", "d"}},
    };
    return table;
}

const std::vector<PeakShape>& peak_shapes() {
    static const std::vector<PeakShape> table = {
        {"ガウス", {"a", "b", "c"}, "{a}*exp(-((x-{b})/{c})**2)"},
        {"ローレンツ", {"a", "b", "c"}, "{a}/((x-{b})**2+{c})"},
        {"Voigt", {"a", "b", "s", "g"}, "{a}*voigt(x-{b}, {s}, {g})"},
    };
    return table;
}

std::pair<std::string, std::vector<std::string>> build_multipeak(const std::string& shape_name,
                                                                   int n, bool baseline) {
    const PeakShape* shape = nullptr;
    for (const auto& s : peak_shapes()) {
        if (s.name == shape_name) shape = &s;
    }
    if (!shape) throw ExprError("未知のピーク形状です: " + shape_name);

    std::vector<std::string> terms;
    std::vector<std::string> params;
    for (int i = 1; i <= n; ++i) {
        std::string term = shape->template_expr;
        for (const auto& base_name : shape->param_names) {
            std::string full_name = base_name + std::to_string(i);
            std::string placeholder = "{" + base_name + "}";
            size_t pos;
            while ((pos = term.find(placeholder)) != std::string::npos) {
                term.replace(pos, placeholder.size(), full_name);
            }
            params.push_back(full_name);
        }
        terms.push_back(term);
    }

    std::string expr;
    for (size_t i = 0; i < terms.size(); ++i) {
        if (i) expr += " + ";
        expr += terms[i];
    }
    if (baseline) {
        expr = "d0 + " + expr;
        params.insert(params.begin(), "d0");
    }
    return {expr, params};
}

}  // namespace sma4cpp
