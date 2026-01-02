//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_brace_initializer(const BraceInitializer &expr) {
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
