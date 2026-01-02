//
// Created by Luke on 02/01/2026.
//
#include "../Generator.h"

void Generator::generate_struct(const StructDeclaration &structDecl) {
    std::vector<llvm::Type *> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto &field: structDecl.fields) {
        fieldTypes.push_back(generate_type(*field.type));
        fieldIndices[field.name] = idx++;
    }

    llvm::StructType *structType = llvm::StructType::create(
        *context,
        fieldTypes,
        structDecl.name
    );

    currentScope->define_struct(structDecl.name, structType, std::move(fieldIndices));
}
