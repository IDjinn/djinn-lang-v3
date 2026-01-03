//
// Created by Luke on 02/01/2026.
//

#include <gtest/gtest.h>

#include "../DjinnCompiler.h"
#include "../generator/GeneratorScope.h"
#include "../generator/Mangler.h"
#include "../parser/ast/Generic.h"


// === struct Array<T> { data: *T; size: i32; } ===

class GenericArrayTest : public ::testing::Test {
protected:
    GeneratorScope globalScope;

    void SetUp() override {
        GenericStructDef arrayDef;
        arrayDef.name = "Array";
        arrayDef.params.add(GenericParam("T"));
        arrayDef.fields.push_back({"data", Type::pointer(Type::struct_type("T"))});
        arrayDef.fields.push_back({"size", Type::integer(32, true)});

        globalScope.define_generic_struct("Array", std::move(arrayDef));
    }

    void monomorphize_array(const Type &elementType) {
        const auto *def = globalScope.lookup_generic_struct("Array");
        ASSERT_NE(def, nullptr);

        GenericArgs args;
        args.add(elementType);
        const GenericContext ctx = GenericContext::create(def->params, args);

        std::unordered_map<std::string, unsigned> fieldIndices;
        std::vector<Type> substitutedFields;

        unsigned idx = 0;
        for (const auto &[name, type]: def->fields) {
            Type substituted = ctx.substitute(type);
            substitutedFields.push_back(substituted);
            fieldIndices[name] = idx++;
        }

        const std::vector<Type> typeArgs = {elementType};
        globalScope.define_monomorphized_struct("Array", typeArgs, nullptr, fieldIndices);
    }
};

TEST_F(GenericArrayTest, TemplateIsRegistered) {
    EXPECT_TRUE(globalScope.has_generic_struct("Array"));

    const auto *def = globalScope.lookup_generic_struct("Array");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->name, "Array");
    EXPECT_EQ(def->params.size(), 1);
    EXPECT_EQ(def->params.params[0].name, "T");
    EXPECT_EQ(def->fields.size(), 2);
}

TEST_F(GenericArrayTest, MonomorphizeArrayOfI32) {
    const Type i32Type = Type::integer(32, true);
    monomorphize_array(i32Type);

    const std::vector<Type> typeArgs = {i32Type};
    const std::string mangledName = Mangler::mangle_generic_struct("Array", typeArgs);
    EXPECT_EQ(mangledName, "_ZN5ArrayIiE");

    EXPECT_TRUE(globalScope.has_struct_in_current_scope(mangledName));

    const auto *indices = globalScope.lookup_field_indices(mangledName);
    ASSERT_NE(indices, nullptr);
    EXPECT_EQ(indices->at("data"), 0);
    EXPECT_EQ(indices->at("size"), 1);
}

TEST_F(GenericArrayTest, MonomorphizeArrayOfString) {
    const Type stringType = Type::stringed();
    monomorphize_array(stringType);

    const std::vector<Type> typeArgs = {stringType};
    const std::string mangledName = Mangler::mangle_generic_struct("Array", typeArgs);
    EXPECT_EQ(mangledName, "_ZN5ArrayIPcE");

    EXPECT_TRUE(globalScope.has_struct_in_current_scope(mangledName));
}

TEST_F(GenericArrayTest, MonomorphizeArrayOfStruct) {
    const Type pointType = Type::struct_type("Point");
    monomorphize_array(pointType);

    const std::vector<Type> typeArgs = {pointType};
    const std::string mangledName = Mangler::mangle_generic_struct("Array", typeArgs);
    EXPECT_EQ(mangledName, "_ZN5ArrayI5PointE");

    EXPECT_TRUE(globalScope.has_struct_in_current_scope(mangledName));
}

TEST_F(GenericArrayTest, MultipleInstantiationsAreSeparate) {
    const Type i32Type = Type::integer(32, true);
    const Type i64Type = Type::integer(64, true);

    monomorphize_array(i32Type);
    monomorphize_array(i64Type);

    const std::vector<Type> i32Args = {i32Type};
    const std::vector<Type> i64Args = {i64Type};

    const std::string i32Mangled = Mangler::mangle_generic_struct("Array", i32Args);
    const std::string i64Mangled = Mangler::mangle_generic_struct("Array", i64Args);

    EXPECT_NE(i32Mangled, i64Mangled);
    EXPECT_TRUE(globalScope.has_struct_in_current_scope(i32Mangled));
    EXPECT_TRUE(globalScope.has_struct_in_current_scope(i64Mangled));
}

// === struct Map<K, V> { keys: Array<K>; values: Array<V>; } ===

class GenericMapTest : public ::testing::Test {
protected:
    GeneratorScope globalScope;

    void SetUp() override {
        // Array<T>
        GenericStructDef arrayDef;
        arrayDef.name = "Array";
        arrayDef.params.add(GenericParam("T"));
        arrayDef.fields.push_back({"data", Type::pointer(Type::struct_type("T"))});
        globalScope.define_generic_struct("Array", std::move(arrayDef));

        // Map<K, V>
        GenericStructDef mapDef;
        mapDef.name = "Map";
        mapDef.params.add(GenericParam("K"));
        mapDef.params.add(GenericParam("V"));
        mapDef.fields.push_back({"keys", Type::pointer(Type::struct_type("K"))});
        mapDef.fields.push_back({"values", Type::pointer(Type::struct_type("V"))});
        globalScope.define_generic_struct("Map", std::move(mapDef));
    }
};

TEST_F(GenericMapTest, MapTemplateRegistered) {
    EXPECT_TRUE(globalScope.has_generic_struct("Map"));

    const auto *def = globalScope.lookup_generic_struct("Map");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->params.size(), 2);
    EXPECT_EQ(def->params.params[0].name, "K");
    EXPECT_EQ(def->params.params[1].name, "V");
}

TEST_F(GenericMapTest, MonomorphizeMapStringToI32) {
    // Map<string, i32>
    const auto *def = globalScope.lookup_generic_struct("Map");
    ASSERT_NE(def, nullptr);

    GenericArgs args;
    args.add(Type::stringed());
    args.add(Type::integer(32, true));

    const GenericContext ctx = GenericContext::create(def->params, args);

    // Substitui campos
    const Type keysField = ctx.substitute(def->fields[0].second);
    const Type valuesField = ctx.substitute(def->fields[1].second);

    // keys: *K -> *string
    EXPECT_EQ(keysField.kind, TypeKind::POINTER);
    EXPECT_EQ(keysField.elementType->kind, TypeKind::STRING);

    // values: *V -> *i32
    EXPECT_EQ(valuesField.kind, TypeKind::POINTER);
    EXPECT_EQ(valuesField.elementType->kind, TypeKind::INTEGER);
}

// === template<T> T identity(T x) { return x; } ===

class GenericFunctionTest : public ::testing::Test {
protected:
    GeneratorScope globalScope;

    void SetUp() override {
        // template<T> T identity(T x)
        GenericFunctionDef identityDef;
        identityDef.name = "identity";
        identityDef.params.add(GenericParam("T"));
        identityDef.returnType = Type::struct_type("T");
        identityDef.parameters.push_back({"x", Type::struct_type("T")});

        globalScope.define_generic_function("identity", std::move(identityDef));

        // template<T> T max(T a, T b)
        GenericFunctionDef maxDef;
        maxDef.name = "max";
        maxDef.params.add(GenericParam("T"));
        maxDef.returnType = Type::struct_type("T");
        maxDef.parameters.push_back({"a", Type::struct_type("T")});
        maxDef.parameters.push_back({"b", Type::struct_type("T")});

        globalScope.define_generic_function("max", std::move(maxDef));
    }
};

TEST_F(GenericFunctionTest, IdentityTemplateRegistered) {
    EXPECT_TRUE(globalScope.has_generic_function("identity"));

    const auto *def = globalScope.lookup_generic_function("identity");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->params.size(), 1);
    EXPECT_EQ(def->parameters.size(), 1);
}

TEST_F(GenericFunctionTest, MonomorphizeIdentityI32) {
    const auto *def = globalScope.lookup_generic_function("identity");
    ASSERT_NE(def, nullptr);

    GenericArgs args;
    args.add(Type::integer(32, true));

    GenericContext ctx = GenericContext::create(def->params, args);

    Type returnType = ctx.substitute(def->returnType);
    Type paramType = ctx.substitute(def->parameters[0].second);

    EXPECT_EQ(returnType.kind, TypeKind::INTEGER);
    EXPECT_EQ(returnType.size, 32);
    EXPECT_EQ(paramType.kind, TypeKind::INTEGER);
    EXPECT_EQ(paramType.size, 32);

    std::vector<Type> paramTypes = {paramType};
    std::string mangledName = Mangler::mangle_function("identity", paramTypes);
    EXPECT_EQ(mangledName, "_Z8identityi");
}

TEST_F(GenericFunctionTest, MonomorphizeMaxF64) {
    const auto *def = globalScope.lookup_generic_function("max");
    ASSERT_NE(def, nullptr);

    GenericArgs args;
    args.add(Type::floated(64));

    GenericContext ctx = GenericContext::create(def->params, args);

    Type returnType = ctx.substitute(def->returnType);
    Type param1 = ctx.substitute(def->parameters[0].second);
    Type param2 = ctx.substitute(def->parameters[1].second);

    EXPECT_EQ(returnType.kind, TypeKind::F64);
    EXPECT_EQ(param1.kind, TypeKind::F64);
    EXPECT_EQ(param2.kind, TypeKind::F64);

    std::vector<Type> paramTypes = {param1, param2};
    std::string mangledName = Mangler::mangle_function("max", paramTypes);
    EXPECT_EQ(mangledName, "_Z3maxdd");
}

class GenericDefaultTypeTest : public ::testing::Test {
protected:
    GeneratorScope globalScope;

    void SetUp() override {
        GenericStructDef vectorDef;
        vectorDef.name = "Vector";
        vectorDef.params.add(GenericParam("T"));
        vectorDef.params.add(GenericParam("Allocator",
                                          std::make_unique<Type>(Type::struct_type("DefaultAllocator"))));
        vectorDef.fields.push_back({"data", Type::pointer(Type::struct_type("T"))});
        vectorDef.fields.push_back({"alloc", Type::struct_type("Allocator")});

        globalScope.define_generic_struct("Vector", std::move(vectorDef));
    }
};

TEST_F(GenericDefaultTypeTest, VectorWithDefaultAllocator) {
    const auto *def = globalScope.lookup_generic_struct("Vector");
    ASSERT_NE(def, nullptr);

    GenericArgs args;
    args.add(Type::integer(32, true));
    const GenericContext ctx = GenericContext::create(def->params, args);

    const Type dataField = ctx.substitute(def->fields[0].second);
    const Type allocField = ctx.substitute(def->fields[1].second);

    EXPECT_EQ(dataField.kind, TypeKind::POINTER);
    EXPECT_EQ(dataField.elementType->kind, TypeKind::INTEGER);
    EXPECT_EQ(allocField.kind, TypeKind::STRUCT);
    EXPECT_EQ(allocField.structName, "DefaultAllocator");
}

TEST_F(GenericDefaultTypeTest, VectorWithCustomAllocator) {
    const auto *def = globalScope.lookup_generic_struct("Vector");
    ASSERT_NE(def, nullptr);

    // Vector<i32, PoolAllocator>
    GenericArgs args;
    args.add(Type::integer(32, true));
    args.add(Type::struct_type("PoolAllocator"));

    const GenericContext ctx = GenericContext::create(def->params, args);

    const Type allocField = ctx.substitute(def->fields[1].second);

    EXPECT_EQ(allocField.kind, TypeKind::STRUCT);
    EXPECT_EQ(allocField.structName, "PoolAllocator");
}

TEST(GenericScopeHierarchy, LocalGenericShadowsGlobal) {
    const auto global = std::make_shared<GeneratorScope>();

    GenericStructDef globalArray;
    globalArray.name = "Array";
    globalArray.params.add(GenericParam("T"));
    globalArray.fields.push_back({"data", Type::pointer(Type::struct_type("T"))});
    global->define_generic_struct("Array", std::move(globalArray));

    GeneratorScope funcScope(global);

    GenericStructDef localArray;
    localArray.name = "Array";
    localArray.params.add(GenericParam("T"));
    localArray.fields.push_back({"data", Type::pointer(Type::struct_type("T"))});
    localArray.fields.push_back({"size", Type::integer(32, true)});
    funcScope.define_generic_struct("Array", std::move(localArray));

    const auto *localDef = funcScope.lookup_generic_struct("Array");
    ASSERT_NE(localDef, nullptr);
    EXPECT_EQ(localDef->fields.size(), 2);

    const auto *globalDef = global->lookup_generic_struct("Array");
    ASSERT_NE(globalDef, nullptr);
    EXPECT_EQ(globalDef->fields.size(), 1);
}


TEST(GenericFullFlow, CompleteArrayUsage) {
    GeneratorScope scope;

    GenericStructDef arrayDef;
    arrayDef.name = "Array";
    arrayDef.params.add(GenericParam("T"));
    arrayDef.fields.push_back({"data", Type::pointer(Type::struct_type("T"))});
    arrayDef.fields.push_back({"size", Type::integer(32, true)});
    scope.define_generic_struct("Array", std::move(arrayDef));

    const auto *def = scope.lookup_generic_struct("Array");
    ASSERT_NE(def, nullptr);

    GenericArgs args;
    args.add(Type::integer(32, true));
    GenericContext ctx = GenericContext::create(def->params, args);

    std::vector<Type> concreteFields;
    std::unordered_map<std::string, unsigned> fieldIndices;
    unsigned idx = 0;
    for (const auto &[name, type]: def->fields) {
        concreteFields.push_back(ctx.substitute(type));
        fieldIndices[name] = idx++;
    }

    std::vector<Type> typeArgs = {Type::integer(32, true)};
    scope.define_monomorphized_struct("Array", typeArgs, nullptr, fieldIndices);

    std::string mangledName = Mangler::mangle_generic_struct("Array", typeArgs);

    EXPECT_TRUE(scope.has_struct_in_current_scope(mangledName));
    EXPECT_EQ(concreteFields[0].kind, TypeKind::POINTER);
    EXPECT_EQ(concreteFields[0].elementType->kind, TypeKind::INTEGER);
    EXPECT_EQ(concreteFields[1].kind, TypeKind::INTEGER);

    const auto *indices = scope.lookup_field_indices(mangledName);
    ASSERT_NE(indices, nullptr);
    EXPECT_EQ(indices->at("data"), 0);
    EXPECT_EQ(indices->at("size"), 1);

    std::string demangled = Mangler::demangle(mangledName);
    EXPECT_TRUE(demangled.find("Array") != std::string::npos);
}


TEST(FullCompilation, StructGeneric) {
    const std::string source = R"(
        struct Array<T> { T data; i32 size; }

        void main() {
            Array<i32> arr;
            return;
        }
)";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 0);
}
