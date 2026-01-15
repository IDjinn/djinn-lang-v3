//
// Created by Luke on 02/01/2026.
//

#include <gtest/gtest.h>
#include "../generator/GeneratorScope.h"


TEST(GeneratorScope, CreateRootScope) {
    const GeneratorScope scope;
    EXPECT_EQ(scope.parent, nullptr);
}

TEST(GeneratorScope, CreateChildScope) {
    const auto parent = std::make_shared<GeneratorScope>();
    const GeneratorScope child(parent);
    EXPECT_EQ(child.parent, parent);
}

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