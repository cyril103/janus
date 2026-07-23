#pragma once

namespace janus::ownership {

template <typename Value, typename DirectOwnership, typename VisitChildren>
[[nodiscard]] bool recursively_owns_value(const Value &value,
                                          DirectOwnership direct_ownership,
                                          VisitChildren visit_children) {
  if (direct_ownership(value))
    return true;

  bool owns_value = false;
  visit_children(value, [&](const Value &child) {
    if (!owns_value)
      owns_value =
          recursively_owns_value(child, direct_ownership, visit_children);
  });
  return owns_value;
}

} // namespace janus::ownership
