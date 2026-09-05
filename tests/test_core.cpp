// test_core.cpp -- コア (expr_eval / fitting) の動作確認。
//
// Python版の tests/test_features.py と同じ考え方: assert失敗で即座に落として
// 原因を分かりやすくする。GUI(Qt)には依存しないので、Windows専用の最終形でも
// このテストは Linux/macOS の開発機やCIでそのまま動く (コア層はOS非依存)。
//
//   cmake --build build && ./build/test_core
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>
#include <stdexcept>

#include "expr_eval.hpp"
#include "fitting.hpp"

using namespace sma4cpp;

namespace {

int g_test_no = 0;

void section(const std::string& title) {
    std::cout << "\n=== " << ++g_test_no << ". " << title << " ===" << std::endl;
}

void check(bool cond, const std::string& msg) {
    if (!cond) throw std::runtime_error("check failed: " + msg);
}

Eigen::ArrayXd linspace(double lo, double hi, int n) {
    Eigen::ArrayXd out(n);
    for (int i = 0; i < n; ++i) out(i) = lo + (hi - lo) * i / (n - 1);
    return out;
}

}  // namespace

int main() {
    std::mt19937 rng(2);
    std::normal_distribution<double> noise01(0.0, 0.01);
    std::normal_distribution<double> noise005(0.0, 0.05);

    try {
        section("四則演算・べき乗・mod");
        {
            Eigen::ArrayXd x = linspace(0, 4, 5);  // 0,1,2,3,4
            Eigen::ArrayXd r = evaluate("2*x**2 + 1", x);
            std::cout << "  2*x^2+1 @ x=2 -> " << r(2) << " (期待値 9)" << std::endl;
            check(std::fabs(r(2) - 9.0) < 1e-9, "2*x**2+1");

            Eigen::ArrayXd m = evaluate("x % 3", x);
            std::cout << "  x%3 @ x=4 -> " << m(4) << " (期待値 1)" << std::endl;
            check(std::fabs(m(4) - 1.0) < 1e-9, "mod");
        }

        section("単項マイナスと ** の優先順位 (Python と同じか)");
        {
            // ここは「パーサ自身で作った値」ではなく手計算の値と突き合わせる。
            // 自分で生成したデータを自分で解釈すると、優先順位が逆でも辻褄が
            // 合ってしまい、バグをすり抜けさせてしまうため。
            struct Case { const char* expr; double x; double want; };
            const Case cases[] = {
                {"-x**2",   3.0, -9.0},     // -(3^2)。(-3)^2 = +9 ではない
                {"2**-1",   0.0, 0.5},      // 2^(-1)
                {"2**3**2", 0.0, 512.0},    // 2^(3^2) = 2^9 (** は右結合)
                {"-2**2",   0.0, -4.0},
                {"(-x)**2", 3.0, 9.0},      // 括弧で囲めばこちらは +9
                // ガウス関数の形。マイナスが先に効かないと exp(+16) に発散する
                {"exp(-((x-1)/1.5)**2)", 7.0, 1.1253517e-07},
                {"exp(-((x-1)/1.5)**2)", 1.0, 1.0},
            };
            for (const auto& c : cases) {
                Eigen::ArrayXd xv(1);
                xv(0) = c.x;
                double got = evaluate(c.expr, xv)(0);
                std::cout << "  " << c.expr << " @ x=" << c.x << " -> " << got
                          << " (期待値 " << c.want << ")" << std::endl;
                check(std::fabs(got - c.want) <= std::max(1e-9, std::fabs(c.want) * 1e-6), c.expr);
            }
        }

        section("三項 (if/else) と比較演算子");
        {
            Eigen::ArrayXd x = linspace(-2, 2, 5);  // -2,-1,0,1,2
            Eigen::ArrayXd r = evaluate("1 if x > 0 else -1", x);
            std::cout << "  sign相当: " << r.transpose() << std::endl;
            check(r(0) == -1.0 && r(4) == 1.0, "ternary");
        }

        section("未知の名前は構文エラーになる (Python版のASTホワイトリストに相当)");
        {
            bool threw = false;
            try {
                Eigen::ArrayXd x = linspace(0, 1, 3);
                evaluate("__import__('os')", x);
            } catch (const ExprError&) {
                threw = true;
            }
            std::cout << "  __import__ は弾かれる: " << threw << std::endl;
            check(threw, "unknown name rejected");
        }

        section("diff/integ 相当ではなく、直接ベクトル演算での erf/voigt 呼び出し");
        {
            Eigen::ArrayXd x = linspace(-3, 3, 7);
            Eigen::ArrayXd r = evaluate("erf(x)", x);
            std::cout << "  erf(3) ≈ " << r(6) << " (期待値 ≈ 1.0)" << std::endl;
            check(std::fabs(r(6) - 1.0) < 1e-4, "erf");
        }

        section("直線フィット (プリセット)");
        {
            const Preset* linear = nullptr;
            for (const auto& p : presets())
                if (p.name.rfind("直線", 0) == 0) linear = &p;
            check(linear != nullptr, "linear preset exists");

            Eigen::ArrayXd x = linspace(0, 10, 30);
            Eigen::ArrayXd y(30);
            for (int i = 0; i < 30; ++i) y(i) = 2.0 + 3.0 * x(i) + noise005(rng);

            FitResult res = fit(x, y, linear->expr, linear->params, {1.0, 1.0});
            std::cout << "  a=" << res.values[0] << " b=" << res.values[1]
                      << " (真値 2, 3)  R2=" << res.r2 << std::endl;
            check(std::fabs(res.values[0] - 2.0) < 0.2, "linear a");
            check(std::fabs(res.values[1] - 3.0) < 0.05, "linear b");
            check(res.r2 > 0.99, "linear r2");
        }

        section("ガウスプリセットでのフィッティング (データはパーサを使わずに作る)");
        {
            // データ生成にパーサを使うと、式の解釈が間違っていても生成側と
            // フィット側で打ち消し合って通ってしまう。ここは C++ で直接
            // 計算した値を使い、パーサの解釈が正しいことまで含めて確かめる。
            const Preset* gauss = nullptr;
            for (const auto& p : presets())
                if (p.name.rfind("ガウス", 0) == 0) gauss = &p;
            check(gauss != nullptr, "gauss preset exists");

            const double A = 3.0, B = 1.0, C = 1.5;
            Eigen::ArrayXd x = linspace(-6, 6, 120);
            Eigen::ArrayXd y(x.size());
            for (Eigen::Index i = 0; i < x.size(); ++i) {
                const double t = (x(i) - B) / C;
                y(i) = A * std::exp(-t * t) + noise01(rng);
            }

            FitResult res = fit(x, y, gauss->expr, gauss->params, {1.0, 0.0, 1.0});
            std::cout << "  a=" << res.values[0] << " b=" << res.values[1]
                      << " c=" << std::fabs(res.values[2])
                      << " (真値 3, 1, 1.5)  R2=" << res.r2 << std::endl;
            check(std::fabs(res.values[0] - A) < 0.05, "gauss a");
            check(std::fabs(res.values[1] - B) < 0.05, "gauss b");
            check(std::fabs(std::fabs(res.values[2]) - C) < 0.05, "gauss c");
            check(res.r2 > 0.99, "gauss r2");
        }

        section("Voigt プリセットでのフィッティング (自己整合性チェック)");
        {
            const Preset* voigt = nullptr;
            for (const auto& p : presets())
                if (p.name.rfind("Voigt", 0) == 0) voigt = &p;
            check(voigt != nullptr, "voigt preset exists");

            Eigen::ArrayXd x = linspace(-5, 5, 200);
            auto f = make_function(voigt->expr, voigt->params);
            Eigen::ArrayXd y = f(x, {3.0, 0.0, 1.0, 0.5});
            for (int i = 0; i < y.size(); ++i) y(i) += noise01(rng);

            FitResult res = fit(x, y, voigt->expr, voigt->params, {1.0, 0.0, 1.0, 1.0});
            std::cout << "  a=" << res.values[0] << " b=" << res.values[1]
                      << " s=" << res.values[2] << " g=" << res.values[3]
                      << " (真値 3, 0, 1, 0.5)" << std::endl;
            check(std::fabs(res.values[0] - 3.0) < 0.3, "voigt a");
            check(res.r2 > 0.9, "voigt r2");
        }

        section("誤差関数(erf)プリセットでのフィッティング");
        {
            const Preset* erf_preset = nullptr;
            for (const auto& p : presets())
                if (p.expr.find("erf") != std::string::npos) erf_preset = &p;
            check(erf_preset != nullptr, "erf preset exists");

            Eigen::ArrayXd x = linspace(-10, 10, 200);
            auto f = make_function(erf_preset->expr, erf_preset->params);
            Eigen::ArrayXd y = f(x, {5.0, 3.0, 1.0, 2.0});
            for (int i = 0; i < y.size(); ++i) y(i) += noise005(rng);

            FitResult res = fit(x, y, erf_preset->expr, erf_preset->params, {1.0, 1.0, 0.0, 1.0});
            std::cout << "  a=" << res.values[0] << " b=" << res.values[1]
                      << " c=" << res.values[2] << " d=" << res.values[3]
                      << " (真値 5, 3, 1, 2)" << std::endl;
            check(std::fabs(res.values[0] - 5.0) < 0.3, "erf a");
            check(res.r2 > 0.95, "erf r2");
        }

        section("多重ピーク (ガウス2本) の組み立てとフィット");
        {
            auto [expr, params] = build_multipeak("ガウス", 2, /*baseline=*/true);
            std::cout << "  生成された式: " << expr << std::endl;
            check(params.size() == 7, "multipeak param count");

            Eigen::ArrayXd x = linspace(-10, 10, 300);
            auto f = make_function(expr, params);
            std::vector<double> truth = {0.5, 3.0, -3.0, 1.0, 2.0, 3.0, 1.5};
            Eigen::ArrayXd y = f(x, truth);
            for (int i = 0; i < y.size(); ++i) y(i) += noise01(rng);

            FitResult res = fit(x, y, expr, params, truth);
            std::cout << "  b1=" << res.values[2] << " b2=" << res.values[5]
                       << " (真値 -3, 3)" << std::endl;
            check(std::fabs(res.values[2] - (-3.0)) < 0.3, "peak1 center");
            check(std::fabs(res.values[5] - 3.0) < 0.3, "peak2 center");
        }

        std::cout << "\n全テスト通過" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nテスト失敗: " << e.what() << std::endl;
        return 1;
    }
}
