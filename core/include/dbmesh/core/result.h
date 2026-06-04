#ifndef DBMESH_CORE_RESULT_H_
#define DBMESH_CORE_RESULT_H_

#include <type_traits>
#include <utility>
#include <variant>

namespace dbmesh {

// Result<T, E> — zero-cost sum type for error propagation on the hot path.
// Exceptions are banned on the hot path; this is the alternative.
//
// Usage:
//   Result<int, std::string> safe_div(int a, int b) {
//     if (b == 0) return Err(std::string("division by zero"));
//     return Ok(a / b);
//   }
//   auto r = safe_div(10, 2);
//   if (is_ok(r)) use(get_value(r));
//   if (auto* e = std::get_if<std::string>(&r)) handle(*e);

template <typename T, typename E>
using Result = std::variant<T, E>;

// ── Proxy types enable Ok(val)/Err(err) without explicit template params ──
// The return type of the enclosing function determines T and E.

namespace detail {

template <typename T>
struct OkProxy {
  T value;
  template <typename E>
  operator std::variant<T, E>() &&  // NOLINT(google-explicit-constructor)
  {
    return std::variant<T, E>{std::in_place_index<0>, std::move(value)};
  }
};

template <typename E>
struct ErrProxy {
  E error;
  template <typename T>
  operator std::variant<T, E>() &&  // NOLINT(google-explicit-constructor)
  {
    return std::variant<T, E>{std::in_place_index<1>, std::move(error)};
  }
};

} // namespace detail

template <typename T>
[[nodiscard]] auto Ok(T&& val) {
  return detail::OkProxy<std::decay_t<T>>{std::forward<T>(val)};
}

template <typename E>
[[nodiscard]] auto Err(E&& err) {
  return detail::ErrProxy<std::decay_t<E>>{std::forward<E>(err)};
}

// ── Inspection helpers ────────────────────────────────────────────────────

template <typename T, typename E>
[[nodiscard]] bool is_ok(const Result<T, E>& r) noexcept {
  return r.index() == 0;
}

template <typename T, typename E>
[[nodiscard]] bool is_err(const Result<T, E>& r) noexcept {
  return r.index() == 1;
}

template <typename T, typename E>
[[nodiscard]] T& get_value(Result<T, E>& r) {
  return std::get<0>(r);
}

template <typename T, typename E>
[[nodiscard]] const T& get_value(const Result<T, E>& r) {
  return std::get<0>(r);
}

template <typename T, typename E>
[[nodiscard]] E& get_error(Result<T, E>& r) {
  return std::get<1>(r);
}

template <typename T, typename E>
[[nodiscard]] const E& get_error(const Result<T, E>& r) {
  return std::get<1>(r);
}

} // namespace dbmesh

#endif // DBMESH_CORE_RESULT_H_
