#include "calculator.h"

#include "units.h"

#include <fmt/format.h>

#include <cctype>
#include <cmath>
#include <numbers>

namespace mgo {
namespace calc {

namespace {

// A recursive descent parser, working out the value as it goes:
//   sum     = product { ("+" | "-") product }
//   product = signed [unit] { ("*" | "/") signed [unit] }
//   signed  = ("-" | "+") signed | value
//   value   = number | "(" sum ")" | name
//   unit    = "in" | "mm"
// A unit applies to the product so far, so 3/8in is three eighths of an inch
// (not 3 divided by 8 inches), as it would be written
class Parser {
public:
    Parser(std::string_view text, const Context& context)
        : m_text(text)
        , m_context(context)
    {
    }

    std::expected<double, std::string> parse()
    {
        if (atEnd()) {
            return std::unexpected("Type a sum after the =");
        }
        auto v = sum();
        if (!v) {
            return v;
        }
        if (!atEnd()) {
            return std::unexpected(unexpected());
        }
        if (!std::isfinite(*v)) {
            return std::unexpected("The result is too large");
        }
        return v;
    }

private:
    bool atEnd() const
    {
        return m_pos >= m_text.size();
    }

    char peek() const
    {
        return atEnd() ? '\0' : m_text[m_pos];
    }

    static bool isLetter(char c)
    {
        return std::isalpha(static_cast<unsigned char>(c)) != 0;
    }

    static bool isDigit(char c)
    {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    }

    // What to say about the character at m_pos, which can't go there
    std::string unexpected() const
    {
        if (atEnd()) {
            return "The sum isn't finished";
        }
        const char c = peek();
        if (c == ' ') {
            return "Spaces can't be used";
        }
        if (c == ')') {
            return "A ) has no ( before it";
        }
        if (isLetter(c) || isDigit(c) || c == '.' || c == '(') {
            // e.g. "2x3", "2(1+1)" or "p2": there's no implied multiply
            return "Use * to multiply";
        }
        return fmt::format("{} can't go there", c);
    }

    std::expected<double, std::string> sum()
    {
        auto v = product();
        while (v && (peek() == '+' || peek() == '-')) {
            const char op = m_text[m_pos++];
            const auto rhs = product();
            if (!rhs) {
                return rhs;
            }
            *v = op == '+' ? *v + *rhs : *v - *rhs;
        }
        return v;
    }

    std::expected<double, std::string> product()
    {
        auto v = signedValue();
        if (v) {
            v = withUnit(*v);
        }
        while (v && (peek() == '*' || peek() == '/')) {
            const char op = m_text[m_pos++];
            const auto rhs = signedValue();
            if (!rhs) {
                return rhs;
            }
            if (op == '/' && *rhs == 0.0) {
                return std::unexpected("Dividing by zero");
            }
            v = withUnit(op == '*' ? *v * *rhs : *v / *rhs);
        }
        return v;
    }

    std::expected<double, std::string> signedValue()
    {
        if (peek() == '-' || peek() == '+') {
            const bool minus = m_text[m_pos++] == '-';
            auto v = signedValue();
            if (v && minus) {
                *v = -*v;
            }
            return v;
        }
        return value();
    }

    std::expected<double, std::string> value()
    {
        const char c = peek();
        if (c == '(') {
            ++m_pos;
            auto v = sum();
            if (!v) {
                return v;
            }
            if (atEnd()) {
                return std::unexpected("A ( isn't closed");
            }
            if (peek() != ')') {
                return std::unexpected(unexpected());
            }
            ++m_pos;
            return v;
        }
        if (isDigit(c) || c == '.') {
            const std::size_t start = m_pos;
            while (isDigit(peek()) || peek() == '.') {
                ++m_pos;
            }
            const std::string number(m_text.substr(start, m_pos - start));
            if (number == "." || number.find('.') != number.rfind('.')) {
                return std::unexpected(fmt::format("{} isn't a number", number));
            }
            return std::stod(number);
        }
        if (isLetter(c)) {
            return name(word());
        }
        return std::unexpected(unexpected());
    }

    // The letters and digits at m_pos (e.g. "m1x"), in lower case
    std::string word()
    {
        std::string w;
        while (isLetter(peek()) || isDigit(peek())) {
            w += static_cast<char>(std::tolower(static_cast<unsigned char>(m_text[m_pos++])));
        }
        return w;
    }

    std::expected<double, std::string> name(const std::string& w) const
    {
        if (w == "p") {
            if (!m_context.previous) {
                return std::unexpected("There's no previous value for p");
            }
            return *m_context.previous;
        }
        if (w == "pi") {
            return std::numbers::pi;
        }
        if (const auto it = m_context.memory.find(w); it != m_context.memory.end()) {
            if (!it->second) {
                // e.g. "m3x": "Mem 3 has no X"
                std::size_t n = 1;
                while (n < w.size() && isDigit(w[n])) {
                    ++n;
                }
                std::string axis = w.substr(n);
                for (char& a : axis) {
                    a = static_cast<char>(std::toupper(static_cast<unsigned char>(a)));
                }
                return std::unexpected(fmt::format("Mem {} has no {}", w.substr(1, n - 1), axis));
            }
            return *it->second;
        }
        if (w == "in" || w == "mm") {
            return std::unexpected(fmt::format("{} goes after a number", w));
        }
        if (w.starts_with('x')) {
            return std::unexpected("Use * to multiply");
        }
        return std::unexpected(fmt::format("Unknown name: {}", w));
    }

    // A product so far, followed by "in" or "mm" (or not)
    std::expected<double, std::string> withUnit(double v)
    {
        if (!isLetter(peek())) {
            return v;
        }
        const std::size_t start = m_pos;
        const std::string w = word();
        if (w != "in" && w != "mm") {
            m_pos = start; // (e.g. "2x3": said in parse())
            return v;
        }
        if (m_context.unit == "mm") {
            return w == "in" ? v * MM_PER_INCH : v;
        }
        if (m_context.unit == "in") {
            return w == "mm" ? v / MM_PER_INCH : v;
        }
        return std::unexpected(fmt::format("This isn't a length, so {} can't be used", w));
    }

    std::string_view m_text;
    const Context& m_context;
    std::size_t m_pos { 0 };
};

} // end anonymous namespace

bool isExpression(std::string_view text)
{
    return text.starts_with('=');
}

std::expected<double, std::string> evaluate(std::string_view expression, const Context& context)
{
    if (isExpression(expression)) {
        expression.remove_prefix(1);
    }
    return Parser(expression, context).parse();
}

std::string formatResult(double value)
{
    std::string s = fmt::format("{:.6f}", value);
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.') {
        s.pop_back();
    }
    if (s == "-0") {
        s = "0";
    }
    return s;
}

} // end namespace calc
} // end namespace mgo
