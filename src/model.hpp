// model.hpp -- Sma4Py の model.py に対応するコア構造体。
//
// Python版は @dataclass (GC管理・動的属性) だったが、C++版は普通の struct。
// メモリはスタック上の std::vector が値として直接埋め込まれる (Pythonの
// numpy.ndarray はヒープ上のバッファへの参照だが、std::vector<double> を
// 値メンバに持つ struct はコピー時にバッファも複製される点に注意)。
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace sma4cpp {

// Series (model.py の Series dataclass に相当)。
// Python版の xerr/yerr は Optional[np.ndarray] だったが、ここでは
// std::optional<std::vector<double>> で「無い」状態を表現する。
struct Series {
    std::string name = "series";
    std::vector<double> x;
    std::vector<double> y;
    std::optional<std::vector<double>> xerr;
    std::optional<std::vector<double>> yerr;

    // 数式変換 (空文字なら変換しない)。expression.evaluate に相当する処理は
    // expr_eval.hpp の Evaluator が担う。
    std::string x_expr;
    std::string y_expr;
};

// FitCurve (model.py の FitCurve dataclass に相当)。
struct FitCurve {
    std::string name = "fit";
    std::string expr;
    std::vector<std::string> params;
    std::vector<double> values;
    std::string source_series;  // 残差プロット用に、フィット元の系列名を持つ

    double xmin = 0.0;
    double xmax = 1.0;
    int npoints = 400;
};

}  // namespace sma4cpp
