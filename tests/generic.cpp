//
// Created by Luke on 02/01/2026.
//

#include <gtest/gtest.h>
#include "../parser/ast/Generic.h"

// === Testes de GenericParam ===

TEST(GenericParam, BasicConstruction) {
    const GenericParam param("T");
    EXPECT_EQ(param.name, "T");
    EXPECT_EQ(param.defaultType, nullptr);
    EXPECT_EQ(param.constraint, nullptr);
}

TEST(GenericParam, WithDefaultType) {
    const GenericParam param("T", std::make_unique<Type>(Type::integer(32, true)));
    EXPECT_EQ(param.name, "T");
    EXPECT_NE(param.defaultType, nullptr);
    EXPECT_EQ(param.defaultType->kind, TypeKind::INTEGER);
    EXPECT_EQ(param.defaultType->size, 32);
}

TEST(GenericParam, CopyConstruction) {
    const GenericParam original("T", std::make_unique<Type>(Type::integer(32, true)));
    const GenericParam copy(original);

    EXPECT_EQ(copy.name, "T");
    EXPECT_NE(copy.defaultType, nullptr);
    EXPECT_EQ(copy.defaultType->kind, TypeKind::INTEGER);
    EXPECT_NE(copy.defaultType.get(), original.defaultType.get());
}

TEST(GenericParam, MoveConstruction) {
    GenericParam original("T", std::make_unique<Type>(Type::integer(32, true)));
    const GenericParam moved(std::move(original));

    EXPECT_EQ(moved.name, "T");
    EXPECT_NE(moved.defaultType, nullptr);
}

TEST(GenericParams, Empty) {
    const GenericParams params;
    EXPECT_TRUE(params.empty());
    EXPECT_EQ(params.size(), 0);
}

TEST(GenericParams, AddSingleParam) {
    GenericParams params;
    params.add(GenericParam("T"));

    EXPECT_FALSE(params.empty());
    EXPECT_EQ(params.size(), 1);
    EXPECT_EQ(params.params[0].name, "T");
}

TEST(GenericParams, AddMultipleParams) {
    GenericParams params;
    params.add(GenericParam("T"));
    params.add(GenericParam("U"));
    params.add(GenericParam("V"));

    EXPECT_EQ(params.size(), 3);
    EXPECT_EQ(params.params[0].name, "T");
    EXPECT_EQ(params.params[1].name, "U");
    EXPECT_EQ(params.params[2].name, "V");
}

TEST(GenericParams, FindExisting) {
    GenericParams params;
    params.add(GenericParam("T"));
    params.add(GenericParam("U"));

    const GenericParam *found = params.find("U");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->name, "U");
}

TEST(GenericParams, FindNonExisting) {
    GenericParams params;
    params.add(GenericParam("T"));

    const GenericParam *found = params.find("X");
    EXPECT_EQ(found, nullptr);
}

// === Testes de GenericArgs ===

TEST(GenericArgs, Empty) {
    const GenericArgs args;
    EXPECT_TRUE(args.empty());
    EXPECT_EQ(args.size(), 0);
}

TEST(GenericArgs, AddSingleArg) {
    GenericArgs args;
    args.add(Type::integer(32, true));

    EXPECT_FALSE(args.empty());
    EXPECT_EQ(args.size(), 1);
    EXPECT_EQ(args.args[0].kind, TypeKind::INTEGER);
}

TEST(GenericArgs, AddMultipleArgs) {
    GenericArgs args;
    args.add(Type::integer(32, true));
    args.add(Type::string());
    args.add(Type::floating(64));

    EXPECT_EQ(args.size(), 3);
    EXPECT_EQ(args.args[0].kind, TypeKind::INTEGER);
    EXPECT_EQ(args.args[1].kind, TypeKind::STRING);
    EXPECT_EQ(args.args[2].kind, TypeKind::F64);
}

TEST(GenericContext, BindAndResolve) {
    GenericContext ctx;
    ctx.bind("T", Type::integer(32, true));

    const Type *resolved = ctx.resolve("T");
    EXPECT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->kind, TypeKind::INTEGER);
    EXPECT_EQ(resolved->size, 32);
}

TEST(GenericContext, ResolveNonExisting) {
    GenericContext ctx;
    ctx.bind("T", Type::integer(32, true));

    const Type *resolved = ctx.resolve("U");
    EXPECT_EQ(resolved, nullptr);
}

TEST(GenericContext, SubstituteSimpleType) {
    GenericContext ctx;
    ctx.bind("T", Type::integer(32, true));

    const Type genericType = Type::struct_type("T");
    const Type substituted = ctx.substitute(genericType);

    EXPECT_EQ(substituted.kind, TypeKind::INTEGER);
    EXPECT_EQ(substituted.size, 32);
}

TEST(GenericContext, SubstituteNonGenericType) {
    GenericContext ctx;
    ctx.bind("T", Type::integer(32, true));

    const Type concreteType = Type::floating(64);
    const Type substituted = ctx.substitute(concreteType);
    EXPECT_EQ(substituted.kind, TypeKind::F64);
}

TEST(GenericContext, SubstitutePointerType) {
    GenericContext ctx;
    ctx.bind("T", Type::integer(32, true));

    // Pointer<T> -> Pointer<i32>
    const Type pointerToT = Type::pointer(Type::struct_type("T"));
    const Type substituted = ctx.substitute(pointerToT);

    EXPECT_EQ(substituted.kind, TypeKind::POINTER);
    EXPECT_NE(substituted.elementType, nullptr);
    EXPECT_EQ(substituted.elementType->kind, TypeKind::INTEGER);
}

TEST(GenericContext, SubstituteArrayType) {
    GenericContext ctx;
    ctx.bind("T", Type::string());

    // Array<T> -> Array<string>
    const Type arrayOfT = Type::array(Type::struct_type("T"));
    const Type substituted = ctx.substitute(arrayOfT);

    EXPECT_EQ(substituted.kind, TypeKind::ARRAY);
    EXPECT_NE(substituted.elementType, nullptr);
    EXPECT_EQ(substituted.elementType->kind, TypeKind::STRING);
}

TEST(GenericContext, SubstituteUnknownParam) {
    GenericContext ctx;
    ctx.bind("T", Type::integer(32, true));

    // U não está no contexto, permanece como struct "U"
    const Type unknownType = Type::struct_type("U");
    const Type substituted = ctx.substitute(unknownType);

    EXPECT_EQ(substituted.kind, TypeKind::STRUCT);
    EXPECT_EQ(substituted.structName, "U");
}

TEST(GenericContext, CreateFromParamsAndArgs) {
    GenericParams params;
    params.add(GenericParam("T"));
    params.add(GenericParam("U"));

    GenericArgs args;
    args.add(Type::integer(32, true));
    args.add(Type::string());

    const GenericContext ctx = GenericContext::create(params, args);

    const Type *t = ctx.resolve("T");
    const Type *u = ctx.resolve("U");

    EXPECT_NE(t, nullptr);
    EXPECT_EQ(t->kind, TypeKind::INTEGER);

    EXPECT_NE(u, nullptr);
    EXPECT_EQ(u->kind, TypeKind::STRING);
}

TEST(GenericContext, CreateWithDefaultType) {
    GenericParams params;
    params.add(GenericParam("T"));
    params.add(GenericParam("U", std::make_unique<Type>(Type::integer(64, true))));

    GenericArgs args;
    args.add(Type::integer(32, true));

    const GenericContext ctx = GenericContext::create(params, args);

    const Type *t = ctx.resolve("T");
    const Type *u = ctx.resolve("U");

    EXPECT_NE(t, nullptr);
    EXPECT_EQ(t->kind, TypeKind::INTEGER);
    EXPECT_EQ(t->size, 32);

    EXPECT_NE(u, nullptr);
    EXPECT_EQ(u->kind, TypeKind::INTEGER);
    EXPECT_EQ(u->size, 64);
}

TEST(GenericContext, CreatePartialArgs) {
    GenericParams params;
    params.add(GenericParam("T"));
    params.add(GenericParam("U"));
    params.add(GenericParam("V"));

    GenericArgs args;
    args.add(Type::integer(32, true));
    const GenericContext ctx = GenericContext::create(params, args);

    EXPECT_NE(ctx.resolve("T"), nullptr);
    EXPECT_EQ(ctx.resolve("U"), nullptr);
    EXPECT_EQ(ctx.resolve("V"), nullptr);
}


TEST(GenericIntegration, MonomorphizeListOfInt) {
    GenericParams params;
    params.add(GenericParam("T"));

    GenericArgs args;
    args.add(Type::integer(32, true));

    const GenericContext ctx = GenericContext::create(params, args);

    const Type fieldType = Type::struct_type("T");
    const Type substitutedFieldType = ctx.substitute(fieldType);

    EXPECT_EQ(substitutedFieldType.kind, TypeKind::INTEGER);
    EXPECT_EQ(substitutedFieldType.size, 32);
}

TEST(GenericIntegration, MonomorphizeMapOfStringToInt) {
    GenericParams params;
    params.add(GenericParam("K"));
    params.add(GenericParam("V"));

    GenericArgs args;
    args.add(Type::string());
    args.add(Type::integer(32, true));

    const GenericContext ctx = GenericContext::create(params, args);

    const Type keyType = ctx.substitute(Type::struct_type("K"));
    const Type valueType = ctx.substitute(Type::struct_type("V"));

    EXPECT_EQ(keyType.kind, TypeKind::STRING);
    EXPECT_EQ(valueType.kind, TypeKind::INTEGER);
}

TEST(GenericIntegration, NestedPointer) {
    GenericParams params;
    params.add(GenericParam("T"));

    GenericArgs args;
    args.add(Type::struct_type("MyData"));

    const GenericContext ctx = GenericContext::create(params, args);

    // next: *T
    const Type pointerField = Type::pointer(Type::struct_type("T"));
    const Type substituted = ctx.substitute(pointerField);

    EXPECT_EQ(substituted.kind, TypeKind::POINTER);
    EXPECT_EQ(substituted.elementType->kind, TypeKind::STRUCT);
    EXPECT_EQ(substituted.elementType->structName, "MyData");
}