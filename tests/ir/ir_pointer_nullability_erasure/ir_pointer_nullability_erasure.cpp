#include <cassert>
#include <string>

#include "backend/ir/llvm/llvm_ir_backend.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

using namespace sysycc;

int main() {
    BuiltinSemanticType char_type("char");
    PointerSemanticType nullable_char_ptr_type(
        &char_type, PointerNullabilityKind::Nullable);
    PointerSemanticType nonnull_char_ptr_type(
        &char_type, PointerNullabilityKind::Nonnull);
    PointerSemanticType unspecified_char_ptr_type(
        &char_type, PointerNullabilityKind::NullUnspecified);

    LlvmIrBackend backend;
    backend.begin_module();
    BuiltinSemanticType void_type("void");
    backend.declare_function("pointer_nullability_probe", &void_type,
                             {&nullable_char_ptr_type, &nonnull_char_ptr_type,
                              &unspecified_char_ptr_type},
                             false, false);
    backend.end_module();

    const std::string ir_text = backend.get_output_text();
    assert(ir_text.find("declare void @pointer_nullability_probe(ptr, ptr, ptr)") !=
           std::string::npos);
    assert(ir_text.find("_Nullable") == std::string::npos);
    assert(ir_text.find("_Nonnull") == std::string::npos);
    assert(ir_text.find("_Null_unspecified") == std::string::npos);
    return 0;
}
