#include <cassert>
#include <memory>
#include <string>
#include <string_view>

#include "frontend/dialects/core/dialect.hpp"
#include "frontend/dialects/core/dialect_manager.hpp"

using namespace sysycc;

namespace {

class FirstProbeOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "first-probe-owner";
    }

    void contribute_preprocess_probe_handlers(
        PreprocessProbeHandlerRegistry &registry) const override {
        registry.add_handler(PreprocessProbeHandlerKind::ClangBuiltinProbes,
                             "first-probe-owner");
    }
};

class SecondProbeOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "second-probe-owner";
    }

    void contribute_preprocess_probe_handlers(
        PreprocessProbeHandlerRegistry &registry) const override {
        registry.add_handler(PreprocessProbeHandlerKind::ClangBuiltinProbes,
                             "second-probe-owner");
    }
};

class FirstAttributeOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "first-attribute-owner";
    }

    void contribute_attribute_semantic_handlers(
        AttributeSemanticHandlerRegistry &registry) const override {
        registry.add_handler(
            AttributeSemanticHandlerKind::GnuFunctionAttributes,
            "first-attribute-owner");
    }
};

class FirstDirectiveOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "first-directive-owner";
    }

    void contribute_preprocess_directive_handlers(
        PreprocessDirectiveHandlerRegistry &registry) const override {
        registry.add_handler(
            PreprocessDirectiveHandlerKind::ClangWarningDirective,
            "first-directive-owner");
    }
};

class FirstIrOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "first-ir-owner";
    }

    void contribute_ir_extension_lowering_handlers(
        IrExtensionLoweringRegistry &registry) const override {
        registry.add_handler(
            IrExtensionLoweringHandlerKind::GnuFunctionAttributes,
            "first-ir-owner");
    }
};

class SecondIrOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "second-ir-owner";
    }

    void contribute_ir_extension_lowering_handlers(
        IrExtensionLoweringRegistry &registry) const override {
        registry.add_handler(
            IrExtensionLoweringHandlerKind::GnuFunctionAttributes,
            "second-ir-owner");
    }
};

class FirstBuiltinTypeOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "first-builtin-type-owner";
    }

    void contribute_builtin_type_semantic_handlers(
        BuiltinTypeSemanticHandlerRegistry &registry) const override {
        registry.add_handler(
            BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes,
            "first-builtin-type-owner");
    }
};

class SecondBuiltinTypeOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "second-builtin-type-owner";
    }

    void contribute_builtin_type_semantic_handlers(
        BuiltinTypeSemanticHandlerRegistry &registry) const override {
        registry.add_handler(
            BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes,
            "second-builtin-type-owner");
    }
};

class SecondDirectiveOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "second-directive-owner";
    }

    void contribute_preprocess_directive_handlers(
        PreprocessDirectiveHandlerRegistry &registry) const override {
        registry.add_handler(
            PreprocessDirectiveHandlerKind::ClangWarningDirective,
            "second-directive-owner");
    }
};

class SecondAttributeOwnerDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "second-attribute-owner";
    }

    void contribute_attribute_semantic_handlers(
        AttributeSemanticHandlerRegistry &registry) const override {
        registry.add_handler(
            AttributeSemanticHandlerKind::GnuFunctionAttributes,
            "second-attribute-owner");
    }
};

} // namespace

int main() {
    DialectManager dialect_manager;
    dialect_manager.register_dialect(std::make_unique<FirstProbeOwnerDialect>());
    dialect_manager.register_dialect(
        std::make_unique<SecondProbeOwnerDialect>());
    dialect_manager.register_dialect(
        std::make_unique<FirstAttributeOwnerDialect>());
    dialect_manager.register_dialect(
        std::make_unique<SecondAttributeOwnerDialect>());
    dialect_manager.register_dialect(
        std::make_unique<FirstDirectiveOwnerDialect>());
    dialect_manager.register_dialect(
        std::make_unique<SecondDirectiveOwnerDialect>());
    dialect_manager.register_dialect(std::make_unique<FirstIrOwnerDialect>());
    dialect_manager.register_dialect(std::make_unique<SecondIrOwnerDialect>());
    dialect_manager.register_dialect(
        std::make_unique<FirstBuiltinTypeOwnerDialect>());
    dialect_manager.register_dialect(
        std::make_unique<SecondBuiltinTypeOwnerDialect>());

    const auto &probe_handler_registry =
        dialect_manager.get_preprocess_probe_handler_registry();
    assert(probe_handler_registry.has_handler(
        PreprocessProbeHandlerKind::ClangBuiltinProbes));
    assert(probe_handler_registry.get_owner_name(
               PreprocessProbeHandlerKind::ClangBuiltinProbes) ==
           "first-probe-owner");
    assert(probe_handler_registry.get_registration_errors().size() == 1);

    const auto &attribute_handler_registry =
        dialect_manager.get_attribute_semantic_handler_registry();
    assert(attribute_handler_registry.has_handler(
        AttributeSemanticHandlerKind::GnuFunctionAttributes));
    assert(attribute_handler_registry.get_owner_name(
               AttributeSemanticHandlerKind::GnuFunctionAttributes) ==
           "first-attribute-owner");
    assert(attribute_handler_registry.get_registration_errors().size() == 1);

    const auto &directive_handler_registry =
        dialect_manager.get_preprocess_directive_handler_registry();
    assert(directive_handler_registry.has_handler(
        PreprocessDirectiveHandlerKind::ClangWarningDirective));
    assert(directive_handler_registry.get_owner_name(
               PreprocessDirectiveHandlerKind::ClangWarningDirective) ==
           "first-directive-owner");
    assert(directive_handler_registry.get_registration_errors().size() == 1);

    const auto &ir_extension_lowering_registry =
        dialect_manager.get_ir_extension_lowering_registry();
    assert(ir_extension_lowering_registry.has_handler(
        IrExtensionLoweringHandlerKind::GnuFunctionAttributes));
    assert(ir_extension_lowering_registry.get_owner_name(
               IrExtensionLoweringHandlerKind::GnuFunctionAttributes) ==
           "first-ir-owner");
    assert(ir_extension_lowering_registry.get_registration_errors().size() == 1);

    const auto &builtin_type_handler_registry =
        dialect_manager.get_builtin_type_semantic_handler_registry();
    assert(builtin_type_handler_registry.has_handler(
        BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes));
    assert(builtin_type_handler_registry.get_owner_name(
               BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes) ==
           "first-builtin-type-owner");
    assert(builtin_type_handler_registry.get_registration_errors().size() == 1);

    const auto &registration_errors = dialect_manager.get_registration_errors();
    assert(registration_errors.size() == 5);
    assert(registration_errors[0].find("second-probe-owner") !=
           std::string::npos);
    assert(registration_errors[1].find("second-attribute-owner") !=
           std::string::npos);
    assert(registration_errors[2].find("second-directive-owner") !=
           std::string::npos);
    assert(registration_errors[3].find("second-ir-owner") !=
           std::string::npos);
    assert(registration_errors[4].find("second-builtin-type-owner") !=
           std::string::npos);

    return 0;
}
