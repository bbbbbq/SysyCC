#include "backend/ir/shared/detail/ir_context.hpp"

#include <string>

namespace sysycc::detail {

void IRContext::reset() {
    next_temp_id_ = 0;
    next_label_id_ = 0;
}

int IRContext::allocate_temp_id() { return next_temp_id_++; }

int IRContext::allocate_label_id() { return next_label_id_++; }

std::string IRContext::get_temp_name() {
    return "%t" + std::to_string(allocate_temp_id());
}

std::string IRContext::get_label_name() {
    return "label" + std::to_string(allocate_label_id());
}

} // namespace sysycc::detail
