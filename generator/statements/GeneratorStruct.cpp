//
// Created by Luke on 02/01/2026.
//
#include "../Generator.h"

void Generator::generate_struct(const StructDeclaration &struct_declaration) {
    std::vector<llvm::Type *> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto &field: struct_declaration.fields) {
        fieldTypes.push_back(generate_type(*field.type));
        fieldIndices[field.name] = idx++;
    }

    llvm::StructType *structType = llvm::StructType::create(
        *context,
        fieldTypes,
        struct_declaration.name
    );

    currentScope->define_struct(struct_declaration.name, structType, std::move(fieldIndices));

    // ~destructor() //
    {
        const auto method_name = struct_declaration.name + "__destroy";
        const auto destroy_function = generate_function(method_name, Type::voided(), {});
        currentScope->define_method(struct_declaration.name, method_name, destroy_function);
    }
    // constructor() //
    {
        const auto method_name = struct_declaration.name + "__constructor";
        const auto destroy_function = generate_function(method_name, Type::voided(), {});
        currentScope->define_method(struct_declaration.name, method_name, destroy_function);
    }
}
