/// @file
/// @brief `Overloaded`, a visitor made of lambdas for `std::visit`.
#pragma once

namespace terraingen {

/// A visitor with the call operators of every `Fs`, one per alternative of a variant:
/// `std::visit(Overloaded{[](const A&) {...}, [](const B&) {...}}, variant)`.
template <typename... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};

}  // namespace terraingen
