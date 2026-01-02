//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_string_literal(const StringLiteral &expr) {
    return builder->CreateGlobalString(expr.value);
}