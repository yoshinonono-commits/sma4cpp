#include "expr_eval.hpp"

#include <cctype>
#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace sma4cpp {
namespace expr_detail {

// --- AST ------------------------------------------------------------------
// Python の ast モジュールはノードごとにクラスが分かれているが、ここでは
// 「種類を表すenum + 汎用struct」で表現する (タグ付きバリアント方式)。
// C++でASTを組むときの定番の選択肢のひとつで、Pythonのように言語がASTを
// 標準搭載してくれない代わりに自分で最小限のものを用意する形になる。
enum class NodeType { Number, Name, Call, BinOp, UnaryOp, Compare, IfExp };
enum class BinOpKind { Add, Sub, Mul, Div, Mod, Pow };
enum class CmpKind { Lt, Gt, Le, Ge, Eq, Ne };

struct Node {
    NodeType type;
    double number = 0.0;
    std::string name;        // Name の識別子名、Call の関数名
    BinOpKind bin_op{};
    CmpKind cmp_op{};
    std::vector<std::unique_ptr<Node>> kids;
};

// --- 字句解析 (lexer) -------------------------------------------------------
enum class Tok {
    End, Number, Ident, Plus, Minus, Star, StarStar, Slash, Percent,
    LParen, RParen, Comma, Lt, Gt, Le, Ge, Eq, Ne, KwIf, KwElse,
};

struct Token {
    Tok kind;
    std::string text;
    double number = 0.0;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    Token next() {
        skip_ws();
        if (pos_ >= src_.size()) return {Tok::End, ""};
        char c = src_[pos_];

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return lex_number();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return lex_ident();

        switch (c) {
            case '+': ++pos_; return {Tok::Plus, "+"};
            case '-': ++pos_; return {Tok::Minus, "-"};
            case '*':
                ++pos_;
                if (peek() == '*') { ++pos_; return {Tok::StarStar, "**"}; }
                return {Tok::Star, "*"};
            case '/': ++pos_; return {Tok::Slash, "/"};
            case '%': ++pos_; return {Tok::Percent, "%"};
            case '(': ++pos_; return {Tok::LParen, "("};
            case ')': ++pos_; return {Tok::RParen, ")"};
            case ',': ++pos_; return {Tok::Comma, ","};
            case '<':
                ++pos_;
                if (peek() == '=') { ++pos_; return {Tok::Le, "<="}; }
                return {Tok::Lt, "<"};
            case '>':
                ++pos_;
                if (peek() == '=') { ++pos_; return {Tok::Ge, ">="}; }
                return {Tok::Gt, ">"};
            case '=':
                ++pos_;
                if (peek() == '=') { ++pos_; return {Tok::Eq, "=="}; }
                throw ExprError("'=' は使えません ('==' のつもりですか?)");
            case '!':
                ++pos_;
                if (peek() == '=') { ++pos_; return {Tok::Ne, "!="}; }
                throw ExprError("'!' は使えません");
            default:
                throw ExprError(std::string("使えない文字です: ") + c);
        }
    }

private:
    char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }

    void skip_ws() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) ++pos_;
    }

    Token lex_number() {
        size_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isdigit(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.')) {
            ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            size_t save = pos_;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            if (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
            } else {
                pos_ = save;  // "1e" のような不完全な指数は数値の一部とみなさない
            }
        }
        std::string text = src_.substr(start, pos_ - start);
        return {Tok::Number, text, std::stod(text)};
    }

    Token lex_ident() {
        size_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
            ++pos_;
        }
        std::string text = src_.substr(start, pos_ - start);
        if (text == "if") return {Tok::KwIf, text};
        if (text == "else") return {Tok::KwElse, text};
        return {Tok::Ident, text};
    }

    const std::string& src_;
    size_t pos_ = 0;
};

// --- 構文解析 (parser) ------------------------------------------------------
// 優先順位 (低い方から): 三項 if/else > 比較 > 加減 > 乗除・mod > べき乗 > 単項 > 括弧・関数呼出
class Parser {
public:
    explicit Parser(const std::string& src) : lexer_(src) { advance(); }

    std::unique_ptr<Node> parse_full() {
        auto node = parse_ternary();
        if (cur_.kind != Tok::End) throw ExprError("式の末尾に余分な文字があります: " + cur_.text);
        return node;
    }

private:
    void advance() { cur_ = lexer_.next(); }

    void expect(Tok kind, const char* what) {
        if (cur_.kind != kind) throw ExprError(std::string("構文エラー: ") + what + " が必要です");
        advance();
    }

    std::unique_ptr<Node> parse_ternary() {
        auto value_if_true = parse_comparison();
        if (cur_.kind == Tok::KwIf) {
            advance();
            auto cond = parse_comparison();
            expect(Tok::KwElse, "'else'");
            auto value_if_false = parse_ternary();
            auto node = std::make_unique<Node>();
            node->type = NodeType::IfExp;
            node->kids.push_back(std::move(value_if_true));
            node->kids.push_back(std::move(cond));
            node->kids.push_back(std::move(value_if_false));
            return node;
        }
        return value_if_true;
    }

    std::unique_ptr<Node> parse_comparison() {
        auto lhs = parse_additive();
        static const std::unordered_map<Tok, CmpKind> kOps = {
            {Tok::Lt, CmpKind::Lt}, {Tok::Gt, CmpKind::Gt}, {Tok::Le, CmpKind::Le},
            {Tok::Ge, CmpKind::Ge}, {Tok::Eq, CmpKind::Eq}, {Tok::Ne, CmpKind::Ne},
        };
        auto it = kOps.find(cur_.kind);
        if (it != kOps.end()) {
            CmpKind op = it->second;
            advance();
            auto rhs = parse_additive();
            auto node = std::make_unique<Node>();
            node->type = NodeType::Compare;
            node->cmp_op = op;
            node->kids.push_back(std::move(lhs));
            node->kids.push_back(std::move(rhs));
            return node;
        }
        return lhs;
    }

    std::unique_ptr<Node> parse_additive() {
        auto node = parse_term();
        while (cur_.kind == Tok::Plus || cur_.kind == Tok::Minus) {
            BinOpKind op = cur_.kind == Tok::Plus ? BinOpKind::Add : BinOpKind::Sub;
            advance();
            auto rhs = parse_term();
            auto bin = std::make_unique<Node>();
            bin->type = NodeType::BinOp;
            bin->bin_op = op;
            bin->kids.push_back(std::move(node));
            bin->kids.push_back(std::move(rhs));
            node = std::move(bin);
        }
        return node;
    }

    std::unique_ptr<Node> parse_term() {
        auto node = parse_unary();
        while (cur_.kind == Tok::Star || cur_.kind == Tok::Slash || cur_.kind == Tok::Percent) {
            BinOpKind op = cur_.kind == Tok::Star ? BinOpKind::Mul
                          : cur_.kind == Tok::Slash ? BinOpKind::Div
                                                     : BinOpKind::Mod;
            advance();
            auto rhs = parse_unary();
            auto bin = std::make_unique<Node>();
            bin->type = NodeType::BinOp;
            bin->bin_op = op;
            bin->kids.push_back(std::move(node));
            bin->kids.push_back(std::move(rhs));
            node = std::move(bin);
        }
        return node;
    }

    // 単項マイナスと ** の優先順位は Python に合わせる:
    //   -x**2   は  -(x**2)     (「マイナスを 2 乗」ではない)
    //   2**-1   は  2**(-1)
    //   2**3**2 は  2**(3**2)   (** は右結合)
    // そのため parse_unary が parse_power を呼び、power の右辺には再び単項を許す。
    //
    // 逆にすると (単項が ** より先に効くと) exp(-((x-b)/c)**2) が
    // exp(+((x-b)/c)**2) と解釈されてしまい、ガウス関数のフィットが発散する。
    std::unique_ptr<Node> parse_unary() {
        if (cur_.kind == Tok::Minus || cur_.kind == Tok::Plus) {
            bool negate = cur_.kind == Tok::Minus;
            advance();
            auto operand = parse_unary();
            if (!negate) return operand;
            auto node = std::make_unique<Node>();
            node->type = NodeType::UnaryOp;
            node->kids.push_back(std::move(operand));
            return node;
        }
        return parse_power();
    }

    std::unique_ptr<Node> parse_power() {
        auto lhs = parse_primary();
        if (cur_.kind == Tok::StarStar) {
            advance();
            auto rhs = parse_unary();
            auto bin = std::make_unique<Node>();
            bin->type = NodeType::BinOp;
            bin->bin_op = BinOpKind::Pow;
            bin->kids.push_back(std::move(lhs));
            bin->kids.push_back(std::move(rhs));
            return bin;
        }
        return lhs;
    }

    std::unique_ptr<Node> parse_primary() {
        if (cur_.kind == Tok::Number) {
            auto node = std::make_unique<Node>();
            node->type = NodeType::Number;
            node->number = cur_.number;
            advance();
            return node;
        }
        if (cur_.kind == Tok::Ident) {
            std::string name = cur_.text;
            advance();
            if (cur_.kind == Tok::LParen) {
                advance();
                auto node = std::make_unique<Node>();
                node->type = NodeType::Call;
                node->name = name;
                if (cur_.kind != Tok::RParen) {
                    node->kids.push_back(parse_ternary());
                    while (cur_.kind == Tok::Comma) {
                        advance();
                        node->kids.push_back(parse_ternary());
                    }
                }
                expect(Tok::RParen, "')'");
                return node;
            }
            auto node = std::make_unique<Node>();
            node->type = NodeType::Name;
            node->name = name;
            return node;
        }
        if (cur_.kind == Tok::LParen) {
            advance();
            auto node = parse_ternary();
            expect(Tok::RParen, "')'");
            return node;
        }
        throw ExprError("式が不正です (数値・変数・関数・'(' が来るはずの場所です)");
    }

    Lexer lexer_;
    Token cur_;
};

// --- 組み込み関数テーブル ---------------------------------------------------
// Python版 expression.py の FUNCS 辞書に相当。
using Fn = std::function<Eigen::ArrayXd(const std::vector<Eigen::ArrayXd>&)>;

double floored_mod(double a, double b) {
    // Python の "%" / numpy の mod は「あまりの符号が除数に揃う」floored mod。
    // C++ の std::fmod はあまりの符号が被除数に揃う (truncated mod) ので、
    // そのままでは一致しない。ここで numpy 互換に補正する。
    double r = std::fmod(a, b);
    if (r != 0.0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

Eigen::ArrayXd unary(const Eigen::ArrayXd& a, double (*f)(double)) {
    return a.unaryExpr(f);
}

Eigen::ArrayXd voigt_fn(const Eigen::ArrayXd& x, const Eigen::ArrayXd& sigma,
                        const Eigen::ArrayXd& gamma) {
    // 疑似Voigt (Thompson-Cox-Hastings 近似)。
    //
    // Python版は scipy.special.wofz (Faddeeva関数) を使った厳密なVoigtだが、
    // C++版フェーズ1では追加の依存ライブラリを増やさないため、実用上十分な
    // 精度 (誤差 ~1%程度) を持つ近似式を使う。厳密な実装への切り替えは
    // 依存ライブラリの検討と合わせて次フェーズで行う (README参照)。
    const double ln2 = std::log(2.0);
    Eigen::ArrayXd out(x.size());
    for (Eigen::Index n = 0; n < x.size(); ++n) {
        double s = sigma(std::min<Eigen::Index>(n, sigma.size() - 1));
        double g = gamma(std::min<Eigen::Index>(n, gamma.size() - 1));
        double xv = x(n);
        double fg = 2.0 * s * std::sqrt(2.0 * ln2);
        double fl = 2.0 * g;
        double f = std::pow(
            std::pow(fg, 5) + 2.69269 * std::pow(fg, 4) * fl +
                2.42843 * std::pow(fg, 3) * fl * fl + 4.47163 * fg * fg * std::pow(fl, 3) +
                0.07842 * fg * std::pow(fl, 4) + std::pow(fl, 5),
            0.2);
        double ratio = f > 0.0 ? fl / f : 0.0;
        double eta = 1.36603 * ratio - 0.47719 * ratio * ratio + 0.11116 * ratio * ratio * ratio;
        double gauss = f > 0.0 ? (2.0 / f) * std::sqrt(ln2 / M_PI) * std::exp(-4.0 * ln2 * (xv / f) * (xv / f)) : 0.0;
        double lorentz = f > 0.0 ? (2.0 / (M_PI * f)) / (1.0 + 4.0 * (xv / f) * (xv / f)) : 0.0;
        out(n) = eta * lorentz + (1.0 - eta) * gauss;
    }
    return out;
}

const std::unordered_map<std::string, int>& func_arity() {
    static const std::unordered_map<std::string, int> table = {
        {"sin", 1}, {"cos", 1}, {"tan", 1}, {"asin", 1}, {"acos", 1}, {"atan", 1},
        {"atan2", 2}, {"sinh", 1}, {"cosh", 1}, {"tanh", 1}, {"exp", 1}, {"log", 1},
        {"ln", 1}, {"log10", 1}, {"sqrt", 1}, {"abs", 1}, {"fabs", 1}, {"floor", 1},
        {"ceil", 1}, {"sign", 1}, {"pow", 2}, {"mod", 2}, {"min", 2}, {"max", 2},
        {"erf", 1}, {"jn", 2}, {"yn", 2}, {"voigt", 3},
    };
    return table;
}

Eigen::ArrayXd call_func(const std::string& name, const std::vector<Eigen::ArrayXd>& args) {
    if (name == "sin") return unary(args[0], [](double v) { return std::sin(v); });
    if (name == "cos") return unary(args[0], [](double v) { return std::cos(v); });
    if (name == "tan") return unary(args[0], [](double v) { return std::tan(v); });
    if (name == "asin") return unary(args[0], [](double v) { return std::asin(v); });
    if (name == "acos") return unary(args[0], [](double v) { return std::acos(v); });
    if (name == "atan") return unary(args[0], [](double v) { return std::atan(v); });
    if (name == "atan2") return args[0].binaryExpr(args[1], [](double a, double b) { return std::atan2(a, b); });
    if (name == "sinh") return unary(args[0], [](double v) { return std::sinh(v); });
    if (name == "cosh") return unary(args[0], [](double v) { return std::cosh(v); });
    if (name == "tanh") return unary(args[0], [](double v) { return std::tanh(v); });
    if (name == "exp") return unary(args[0], [](double v) { return std::exp(v); });
    if (name == "log" || name == "ln") return unary(args[0], [](double v) { return std::log(v); });
    if (name == "log10") return unary(args[0], [](double v) { return std::log10(v); });
    if (name == "sqrt") return unary(args[0], [](double v) { return std::sqrt(v); });
    if (name == "abs" || name == "fabs") return unary(args[0], [](double v) { return std::fabs(v); });
    if (name == "floor") return unary(args[0], [](double v) { return std::floor(v); });
    if (name == "ceil") return unary(args[0], [](double v) { return std::ceil(v); });
    if (name == "sign") return unary(args[0], [](double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); });
    if (name == "pow") return args[0].binaryExpr(args[1], [](double a, double b) { return std::pow(a, b); });
    if (name == "mod") return args[0].binaryExpr(args[1], [](double a, double b) { return floored_mod(a, b); });
    if (name == "min") return args[0].binaryExpr(args[1], [](double a, double b) { return std::min(a, b); });
    if (name == "max") return args[0].binaryExpr(args[1], [](double a, double b) { return std::max(a, b); });
    if (name == "erf") return unary(args[0], [](double v) { return std::erf(v); });
    if (name == "jn") return args[0].binaryExpr(args[1], [](double n, double x) { return std::cyl_bessel_j(n, x); });
    if (name == "yn") return args[0].binaryExpr(args[1], [](double n, double x) { return std::cyl_neumann(n, x); });
    if (name == "voigt") return voigt_fn(args[0], args[1], args[2]);
    throw ExprError("未知の関数です: " + name);
}

// --- 名前の検証 -------------------------------------------------------------
// Python版 _validate() に相当。パース時に一度だけ全ノードを走査する。
void validate(const Node& node, const std::unordered_set<std::string>& allowed_names) {
    switch (node.type) {
        case NodeType::Name:
            if (!allowed_names.count(node.name)) {
                throw ExprError("未知の名前です: " + node.name);
            }
            break;
        case NodeType::Call:
            if (!func_arity().count(node.name)) {
                throw ExprError("未知の関数です: " + node.name);
            }
            break;
        default:
            break;
    }
    for (const auto& kid : node.kids) validate(*kid, allowed_names);
}

// --- 評価 -------------------------------------------------------------------
struct Env {
    const Eigen::ArrayXd& x;
    const Eigen::ArrayXd* y;
    const std::vector<std::string>* extra_names;
    const std::vector<double>* extra_values;
};

Eigen::ArrayXd eval_node(const Node& node, const Env& env) {
    switch (node.type) {
        case NodeType::Number:
            return Eigen::ArrayXd::Constant(env.x.size(), node.number);
        case NodeType::Name: {
            if (node.name == "x") return env.x;
            if (node.name == "y") {
                if (!env.y) throw ExprError("y が使えない場面で参照されました");
                return *env.y;
            }
            if (node.name == "i") {
                Eigen::ArrayXd idx(env.x.size());
                for (Eigen::Index n = 0; n < env.x.size(); ++n) idx(n) = static_cast<double>(n);
                return idx;
            }
            if (node.name == "pi") return Eigen::ArrayXd::Constant(env.x.size(), M_PI);
            if (node.name == "e") return Eigen::ArrayXd::Constant(env.x.size(), M_E);
            if (env.extra_names) {
                for (size_t i = 0; i < env.extra_names->size(); ++i) {
                    if ((*env.extra_names)[i] == node.name) {
                        return Eigen::ArrayXd::Constant(env.x.size(), (*env.extra_values)[i]);
                    }
                }
            }
            throw ExprError("未知の名前です: " + node.name);
        }
        case NodeType::Call: {
            std::vector<Eigen::ArrayXd> args;
            args.reserve(node.kids.size());
            for (const auto& kid : node.kids) args.push_back(eval_node(*kid, env));
            return call_func(node.name, args);
        }
        case NodeType::UnaryOp:
            return -eval_node(*node.kids[0], env);
        case NodeType::BinOp: {
            Eigen::ArrayXd lhs = eval_node(*node.kids[0], env);
            Eigen::ArrayXd rhs = eval_node(*node.kids[1], env);
            switch (node.bin_op) {
                case BinOpKind::Add: return lhs + rhs;
                case BinOpKind::Sub: return lhs - rhs;
                case BinOpKind::Mul: return lhs * rhs;
                case BinOpKind::Div: return lhs / rhs;
                case BinOpKind::Mod: return lhs.binaryExpr(rhs, [](double a, double b) { return floored_mod(a, b); });
                case BinOpKind::Pow: return lhs.binaryExpr(rhs, [](double a, double b) { return std::pow(a, b); });
            }
            throw ExprError("内部エラー: 未知の演算子");
        }
        case NodeType::Compare: {
            Eigen::ArrayXd lhs = eval_node(*node.kids[0], env);
            Eigen::ArrayXd rhs = eval_node(*node.kids[1], env);
            Eigen::ArrayXd out(lhs.size());
            for (Eigen::Index n = 0; n < lhs.size(); ++n) {
                bool result;
                switch (node.cmp_op) {
                    case CmpKind::Lt: result = lhs(n) < rhs(n); break;
                    case CmpKind::Gt: result = lhs(n) > rhs(n); break;
                    case CmpKind::Le: result = lhs(n) <= rhs(n); break;
                    case CmpKind::Ge: result = lhs(n) >= rhs(n); break;
                    case CmpKind::Eq: result = lhs(n) == rhs(n); break;
                    case CmpKind::Ne: result = lhs(n) != rhs(n); break;
                    default: result = false;
                }
                out(n) = result ? 1.0 : 0.0;
            }
            return out;
        }
        case NodeType::IfExp: {
            Eigen::ArrayXd value_if_true = eval_node(*node.kids[0], env);
            Eigen::ArrayXd cond = eval_node(*node.kids[1], env);
            Eigen::ArrayXd value_if_false = eval_node(*node.kids[2], env);
            Eigen::ArrayXd out(cond.size());
            for (Eigen::Index n = 0; n < cond.size(); ++n) {
                out(n) = cond(n) != 0.0 ? value_if_true(n) : value_if_false(n);
            }
            return out;
        }
    }
    throw ExprError("内部エラー: 未知のノード種別");
}

}  // namespace expr_detail

// --- CompiledExpr の実体 -----------------------------------------------------
CompiledExpr::CompiledExpr(std::unique_ptr<expr_detail::Node> root,
                            std::vector<std::string> extra_names)
    : root_(std::move(root)), extra_names_(std::move(extra_names)) {}

CompiledExpr::CompiledExpr(CompiledExpr&&) noexcept = default;
CompiledExpr& CompiledExpr::operator=(CompiledExpr&&) noexcept = default;
CompiledExpr::~CompiledExpr() = default;

CompiledExpr CompiledExpr::compile(const std::string& expr,
                                    const std::vector<std::string>& extra_names) {
    if (expr.empty()) throw ExprError("式が空です。");
    expr_detail::Parser parser(expr);
    auto root = parser.parse_full();

    std::unordered_set<std::string> allowed = {"x", "y", "i", "pi", "e"};
    allowed.insert(extra_names.begin(), extra_names.end());
    expr_detail::validate(*root, allowed);

    return CompiledExpr(std::move(root), extra_names);
}

Eigen::ArrayXd CompiledExpr::eval(const Eigen::ArrayXd& x, const Eigen::ArrayXd* y,
                                   const std::vector<double>* extra_values) const {
    expr_detail::Env env{x, y, &extra_names_, extra_values};
    return expr_detail::eval_node(*root_, env);
}

Eigen::ArrayXd evaluate(const std::string& expr, const Eigen::ArrayXd& x, const Eigen::ArrayXd* y) {
    auto compiled = CompiledExpr::compile(expr, {});
    return compiled.eval(x, y, nullptr);
}

FitFunction make_function(const std::string& expr, const std::vector<std::string>& params) {
    auto compiled = std::make_shared<CompiledExpr>(CompiledExpr::compile(expr, params));
    return [compiled](const Eigen::ArrayXd& x, const std::vector<double>& values) -> Eigen::ArrayXd {
        return compiled->eval(x, nullptr, &values);
    };
}

}  // namespace sma4cpp
