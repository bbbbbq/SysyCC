#include <cassert>
#include <string>

#include "backend/ir/llvm/llvm_ir_backend.hpp"
#include "frontend/dialects/registries/semantic_feature_registry.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"

using namespace sysycc;
using namespace sysycc::detail;

int main() {
    SemanticFeatureRegistry enabled_features;
    enabled_features.add_feature(SemanticFeature::QualifiedPointerConversions);
    ConversionChecker checker(&enabled_features, nullptr);

    BuiltinSemanticType char_type("char");
    PointerSemanticType char_ptr_type(&char_type);
    QualifiedSemanticType restrict_char_ptr_type(false, false, true,
                                                 &char_ptr_type);

    assert(!checker.is_same_type(&restrict_char_ptr_type, &char_ptr_type));
    assert(checker.is_assignable_type(&restrict_char_ptr_type, &char_ptr_type));

    LlvmIrBackend backend;
    backend.begin_module();
    BuiltinSemanticType void_type("void");
    backend.declare_function("qualified_pointer_probe", &void_type,
                             {&restrict_char_ptr_type}, false, false);
    backend.end_module();

    const std::string ir_text = backend.get_output_text();
    assert(ir_text.find("declare void @qualified_pointer_probe(ptr)") !=
           std::string::npos);
    assert(ir_text.find("restrict") == std::string::npos);
    return 0;
}
