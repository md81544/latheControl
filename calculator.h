#pragma once

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace mgo {
namespace calc {

// The calculator in numeric entries: anything typed starting with "=" is an
// expression, worked out when Enter is pressed. It saves reaching for a
// calculator at the machine (e.g. halving a reading, or adding a fraction of
// an inch). Deliberately small:
// - numbers (e.g. 12, 0.5, .5), + - * / (only * multiplies: x would be read
//   as part of a memory slot's name), brackets, and a leading minus
// - p: the value in the entry before the expression was typed (e.g. =p/2)
// - pi
// - m1x and so on: a memory slot's value on an axis, as displayed (slot 1 to
//   6, then the axis's label, e.g. m2z)
// - in (or ") and mm after a number, a product or brackets (e.g. =3/8in or
//   =3/8", which is three eighths of an inch, or =(1+1/8)in), converted to
//   the entry's own units. Only in entries which are lengths
// Letters may be upper or lower case. Spaces aren't allowed: Space is kept for
// stopping everything.

// Each memory slot's value on each axis, as displayed, by its name in lower
// case (e.g. "m1x"): nullopt if that slot isn't set on that axis. (A name not
// in here isn't a memory slot at all)
using Memory = std::map<std::string, std::optional<double>>;

// What an expression may refer to
struct Context {
    // p's value, if it has one: the value in the entry before the
    // expression was typed, or (see previousFixed) e.g. an axis's position
    std::optional<double> previous;
    // Whether previous is fixed by whoever opens the entry (e.g. the axis's
    // position, in a go-to), rather than being the entry's value before the
    // expression (which the entry fills in). If it's fixed and nullopt, p
    // can't be used (e.g. a manual axis without a reading)
    bool previousFixed { false };
    Memory memory;
    // The entry's units: "mm" or "in" let the in and mm suffixes be used;
    // anything else (e.g. "deg", or "" for a count) doesn't
    std::string unit;
};

// Whether text is an expression (starts with "=")
bool isExpression(std::string_view text);

// Works out an expression (with or without its leading "="), giving its value,
// or why it can't be worked out (e.g. "Mem 3 has no X", "dividing by zero")
std::expected<double, std::string> evaluate(std::string_view expression, const Context& context);

// A value as it's put back into the entry: up to six decimal places, without
// trailing zeros (e.g. "9.525", "-0.333333", "2")
std::string formatResult(double value);

} // end namespace calc
} // end namespace mgo
