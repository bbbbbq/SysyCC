#pragma once

#include <string>

namespace sysycc::detail {

// Stores transient IR generation state such as temporary numbering.
class IRContext {
  private:
    int next_temp_id_ = 0;
    int next_label_id_ = 0;

  public:
    void reset();
    int allocate_temp_id();
    int allocate_label_id();
    std::string get_temp_name();
    std::string get_label_name();
};

} // namespace sysycc::detail
