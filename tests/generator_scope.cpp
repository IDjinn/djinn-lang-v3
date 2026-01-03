//
// Created by Luke on 02/01/2026.
//

#include <gtest/gtest.h>
#include "../generator/GeneratorScope.h"

// === Testes básicos de escopo ===

TEST(GeneratorScope, CreateRootScope) {
    const GeneratorScope scope;
    EXPECT_EQ(scope.parent, nullptr);
}

TEST(GeneratorScope, CreateChildScope) {
    const auto parent = std::make_shared<GeneratorScope>();
    const GeneratorScope child(parent);
    EXPECT_EQ(child.parent, parent);
}

// === Testes de GenericStructDef ===

TEST(GeneratorScope, DefineGenericStruct) {
    GeneratorScope scope;

    GenericStructDef def;
    def.name = "List";
    def.params.add(GenericParam("T"));
    def.fields.push_back({"data", Type::struct_type("T")});

    scope.define_generic_struct("List", std::move(def));

    EXPECT_TRUE(scope.has_generic_struct("List"));
}

TEST(GeneratorScope, LookupGenericStruct) {
    GeneratorScope scope;

    GenericStructDef def;
    def.name = "List";
    def.params.add(GenericParam("T"));

    scope.define_generic_struct("List", std::move(def));

    const GenericStructDef *found = scope.lookup_generic_struct("List");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->name, "List");
    EXPECT_EQ(found->params.size(), 1);
}

TEST(GeneratorScope, LookupGenericStructNotFound) {
    const GeneratorScope scope;
    const GenericStructDef *found = scope.lookup_generic_struct("Unknown");
    EXPECT_EQ(found, nullptr);
}

TEST(GeneratorScope, LookupGenericStructInParent) {
    const auto parent = std::make_shared<GeneratorScope>();

    GenericStructDef def;
    def.name = "List";
    def.params.add(GenericParam("T"));
    parent->define_generic_struct("List", std::move(def));

    const GeneratorScope child(parent);

    const GenericStructDef *found = child.lookup_generic_struct("List");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->name, "List");
}

TEST(GeneratorScope, GenericStructShadowing) {
    const auto parent = std::make_shared<GeneratorScope>();

    GenericStructDef parentDef;
    parentDef.name = "List";
    parentDef.params.add(GenericParam("T"));
    parent->define_generic_struct("List", std::move(parentDef));

    GeneratorScope child(parent);

    GenericStructDef childDef;
    childDef.name = "List";
    childDef.params.add(GenericParam("T"));
    childDef.params.add(GenericParam("U"));
    child.define_generic_struct("List", std::move(childDef));

    // Child deve encontrar sua própria definição
    const GenericStructDef *found = child.lookup_generic_struct("List");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->params.size(), 2); // Child tem 2 params
}

// === Testes de GenericFunctionDef ===

TEST(GeneratorScope, DefineGenericFunction) {
    GeneratorScope scope;

    GenericFunctionDef def;
    def.name = "identity";
    def.params.add(GenericParam("T"));
    def.returnType = Type::struct_type("T");
    def.parameters.push_back({"value", Type::struct_type("T")});

    scope.define_generic_function("identity", std::move(def));

    EXPECT_TRUE(scope.has_generic_function("identity"));
}

TEST(GeneratorScope, LookupGenericFunction) {
    GeneratorScope scope;

    GenericFunctionDef def;
    def.name = "swap";
    def.params.add(GenericParam("T"));

    scope.define_generic_function("swap", std::move(def));

    const GenericFunctionDef *found = scope.lookup_generic_function("swap");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->name, "swap");
}

TEST(GeneratorScope, LookupGenericFunctionInParent) {
    const auto parent = std::make_shared<GeneratorScope>();

    GenericFunctionDef def;
    def.name = "max";
    def.params.add(GenericParam("T"));
    parent->define_generic_function("max", std::move(def));

    const GeneratorScope child(parent);

    const GenericFunctionDef *found = child.lookup_generic_function("max");
    EXPECT_NE(found, nullptr);
}

// === Testes de Monomorphização ===

TEST(GeneratorScope, DefineMonomorphizedStruct) {
    GeneratorScope scope;

    const std::vector<Type> typeArgs = {Type::integer(32, true)};

    // Simula registrar List<i32>
    // Como não temos LLVM aqui, passamos nullptr
    scope.define_monomorphized_struct("List", typeArgs, nullptr, {{"data", 0}});

    // O nome mangled deve ser encontrado
    const std::string mangledName = Mangler::mangle_generic_struct("List", typeArgs);
    EXPECT_TRUE(scope.has_struct_in_current_scope(mangledName));
}

TEST(GeneratorScope, LookupMonomorphizedStruct) {
    GeneratorScope scope;

    const std::vector<Type> typeArgs = {Type::integer(32, true)};
    scope.define_monomorphized_struct("List", typeArgs, nullptr, {});

    llvm::StructType *found = scope.lookup_monomorphized_struct("List", typeArgs);
    // Será nullptr pois definimos nullptr, mas o lookup funciona
    EXPECT_EQ(found, nullptr); // Esperado pois definimos nullptr

    // Verifica que a chave está correta
    const std::string mangledName = Mangler::mangle_generic_struct("List", typeArgs);
    EXPECT_TRUE(scope.structTypes.contains(mangledName));
}

TEST(GeneratorScope, MonomorphizedStructNotFound) {
    const GeneratorScope scope;

    const std::vector<Type> typeArgs = {Type::integer(32, true)};
    llvm::StructType *found = scope.lookup_monomorphized_struct("List", typeArgs);
    EXPECT_EQ(found, nullptr);
}

// === Testes de Mangled Functions ===

TEST(GeneratorScope, DefineMangledFunction) {
    GeneratorScope scope;

    const std::vector<Type> paramTypes = {Type::integer(32, true), Type::integer(32, true)};
    scope.define_mangled_function("add", paramTypes, nullptr);

    const std::string mangledName = Mangler::mangle_function("add", paramTypes);
    EXPECT_TRUE(scope.has_local_function_in_current_scope(mangledName));
}

TEST(GeneratorScope, DefineMangledMethod) {
    GeneratorScope scope;

    const std::vector<Type> paramTypes = {Type::integer(32, true)};
    scope.define_mangled_method("List", "push", paramTypes, nullptr);

    const std::string mangledName = Mangler::mangle_method("List", "push", paramTypes);
    EXPECT_TRUE(scope.has_method_in_current_scope("List", mangledName));
}

// === Testes de hierarquia de escopo com generics ===

TEST(GeneratorScope, CompleteHierarchyTest) {
    // Escopo global
    auto global = std::make_shared<GeneratorScope>();

    GenericStructDef listDef;
    listDef.name = "List";
    listDef.params.add(GenericParam("T"));
    global->define_generic_struct("List", std::move(listDef));

    GenericFunctionDef maxDef;
    maxDef.name = "max";
    maxDef.params.add(GenericParam("T"));
    global->define_generic_function("max", std::move(maxDef));

    // Escopo de função
    auto funcScope = std::make_shared<GeneratorScope>(global);

    // Função pode acessar generics globais
    EXPECT_TRUE(funcScope->has_generic_struct("List"));
    EXPECT_TRUE(funcScope->has_generic_function("max"));

    // Define generic local
    GenericStructDef localDef;
    localDef.name = "LocalList";
    localDef.params.add(GenericParam("T"));
    funcScope->define_generic_struct("LocalList", std::move(localDef));

    // Escopo de bloco interno
    GeneratorScope blockScope(funcScope);

    // Bloco pode acessar todos
    EXPECT_TRUE(blockScope.has_generic_struct("List"));
    EXPECT_TRUE(blockScope.has_generic_struct("LocalList"));
    EXPECT_TRUE(blockScope.has_generic_function("max"));

    // Global não pode acessar local
    EXPECT_FALSE(global->has_generic_struct("LocalList"));
}

// === Teste de campo field indices ===

TEST(GeneratorScope, MonomorphizedStructFieldIndices) {
    GeneratorScope scope;

    const std::vector<Type> typeArgs = {Type::integer(32, true)};
    const std::unordered_map<std::string, unsigned> fieldIndices = {
        {"data", 0},
        {"size", 1},
        {"capacity", 2}
    };

    scope.define_monomorphized_struct("List", typeArgs, nullptr, fieldIndices);

    const std::string mangledName = Mangler::mangle_generic_struct("List", typeArgs);
    const auto *indices = scope.lookup_field_indices(mangledName);

    EXPECT_NE(indices, nullptr);
    EXPECT_EQ(indices->at("data"), 0);
    EXPECT_EQ(indices->at("size"), 1);
    EXPECT_EQ(indices->at("capacity"), 2);
}