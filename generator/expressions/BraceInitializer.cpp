//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_brace_initializer(const BraceInitializer &expr) {
    // Typed struct literal (e.g. `Packet { .size = 42 }`) in a general
    // expression context: build the struct value from the named type
    if (!expr.structTypeName.empty()) {
        const auto resolved = currentScope->resolve_alias(expr.structTypeName);
        auto *structType = currentScope->get_llvm_struct(resolved);
        if (!structType) {
            if (auto *declared = llvm::dyn_cast<llvm::StructType>(generate_type(Type::struct_type(resolved)))) {
                structType = declared;
            }
        }
        if (!structType) {
            throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                               "struct não encontrada: " + expr.structTypeName);
        }
        return generate_brace_init_for_struct(expr, structType, resolved);
    }

    if (expr.elements.empty()) {
        return nullptr;
    }

    bool hasDesignated = false;
    bool hasPositional = false;
    for (const auto &elem: expr.elements) {
        if (elem.isDesignated()) {
            hasDesignated = true;
        } else {
            hasPositional = true;
        }
    }

    if (hasDesignated && hasPositional) {
        throw CompileError(DiagnosticCode::MIXED_INITIALIZERS,
                           "não é possível misturar inicializadores designados e posicionais");
    }

    std::vector<llvm::Value *> values;
    for (const auto &elem: expr.elements) {
        llvm::Value *val = generate_expression(*elem.value);
        if (!val) return nullptr;
        values.push_back(val);
    }

    if (values.size() == 1 && !hasDesignated) {
        return values[0];
    }

    return values[0];
}