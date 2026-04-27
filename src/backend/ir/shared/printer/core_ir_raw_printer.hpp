#pragma once

#include <string>
#include <unordered_set>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrConstant;
class CoreIrFunction;
class CoreIrGlobal;
class CoreIrInstruction;
class CoreIrModule;
class CoreIrType;
class CoreIrValue;

class CoreIrRawPrinter {
  private:
    std::string format_type(const CoreIrType *type) const;
    std::string format_type_impl(
        const CoreIrType *type,
        std::unordered_set<const CoreIrType *> &active_types) const;
    std::string format_value(const CoreIrValue *value) const;
    std::string format_constant(const CoreIrConstant *constant) const;
    std::string format_instruction(const CoreIrInstruction &instruction) const;
    void append_global(std::string &output, const CoreIrGlobal &global) const;
    void append_function(std::string &output,
                         const CoreIrFunction &function) const;
    void append_block(std::string &output,
                      const CoreIrBasicBlock &basic_block) const;

  public:
    std::string print_module(const CoreIrModule &module) const;
};

} // namespace sysycc
