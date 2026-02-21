//
// Created by Luke on 02/01/2026.
//

#include <gtest/gtest.h>
#include "../generator/Mangler.h"

// === Testes de mangle_type ===

TEST(ManglerType, Integer8Signed) {
    const Type t = Type::integer(8, true);
    EXPECT_EQ(Mangler::mangle_type(t), "a");
}

TEST(ManglerType, Integer16Signed) {
    const Type t = Type::integer(16, true);
    EXPECT_EQ(Mangler::mangle_type(t), "s");
}

TEST(ManglerType, Integer32Signed) {
    const Type t = Type::integer(32, true);
    EXPECT_EQ(Mangler::mangle_type(t), "i");
}

TEST(ManglerType, Integer64Signed) {
    const Type t = Type::integer(64, true);
    EXPECT_EQ(Mangler::mangle_type(t), "l");
}

TEST(ManglerType, Integer32Unsigned) {
    const Type t = Type::integer(32, false);
    EXPECT_EQ(Mangler::mangle_type(t), "j");
}

TEST(ManglerType, Integer64Unsigned) {
    const Type t = Type::integer(64, false);
    EXPECT_EQ(Mangler::mangle_type(t), "m");
}

TEST(ManglerType, Void) {
    const Type t = Type::voided();
    EXPECT_EQ(Mangler::mangle_type(t), "v");
}

TEST(ManglerType, Float32) {
    const Type t = Type::floating(32);
    EXPECT_EQ(Mangler::mangle_type(t), "f");
}

TEST(ManglerType, Float64) {
    const Type t = Type::floating(64);
    EXPECT_EQ(Mangler::mangle_type(t), "d");
}

TEST(ManglerType, String) {
    const Type t = Type::struct_type("str");
    EXPECT_EQ(Mangler::mangle_type(t), "3str");
}

TEST(ManglerType, Pointer) {
    const Type t = Type::pointer(Type::integer(32, true));
    EXPECT_EQ(Mangler::mangle_type(t), "Pi");
}

TEST(ManglerType, PointerToPointer) {
    const Type t = Type::pointer(Type::pointer(Type::integer(8, true)));
    EXPECT_EQ(Mangler::mangle_type(t), "PPa");
}

TEST(ManglerType, Array) {
    const Type t = Type::array(Type::integer(32, true));
    EXPECT_EQ(Mangler::mangle_type(t), "Ai");
}

TEST(ManglerType, Struct) {
    const Type t = Type::struct_type("MyStruct");
    EXPECT_EQ(Mangler::mangle_type(t), "8MyStruct");
}

TEST(ManglerType, StructShortName) {
    const Type t = Type::struct_type("Vec");
    EXPECT_EQ(Mangler::mangle_type(t), "3Vec");
}

TEST(ManglerFunction, SimpleName) {
    EXPECT_EQ(Mangler::mangle_function("foo"), "_Z3foo");
}

TEST(ManglerFunction, LongerName) {
    EXPECT_EQ(Mangler::mangle_function("calculate"), "_Z9calculate");
}

TEST(ManglerFunction, SingleCharName) {
    EXPECT_EQ(Mangler::mangle_function("f"), "_Z1f");
}

TEST(ManglerFunction, WithIntParam) {
    const std::vector<Type> params = {Type::integer(32, true)};
    EXPECT_EQ(Mangler::mangle_function("foo", params), "_Z3fooi");
}

TEST(ManglerFunction, WithMultipleParams) {
    const std::vector<Type> params = {
        Type::integer(32, true),
        Type::integer(64, true),
        Type::floating(64)
    };
    EXPECT_EQ(Mangler::mangle_function("bar", params), "_Z3barild");
}

TEST(ManglerFunction, WithStructParam) {
    const std::vector<Type> params = {Type::struct_type("Point")};
    EXPECT_EQ(Mangler::mangle_function("move", params), "_Z4move5Point");
}

TEST(ManglerFunction, WithPointerParam) {
    const std::vector<Type> params = {Type::pointer(Type::integer(8, true))};
    EXPECT_EQ(Mangler::mangle_function("print", params), "_Z5printPa");
}

TEST(ManglerMethod, SimpleMethod) {
    EXPECT_EQ(Mangler::mangle_method("List", "push"), "_ZN4List4pushE");
}

TEST(ManglerMethod, MethodWithParams) {
    const std::vector<Type> params = {Type::integer(32, true)};
    EXPECT_EQ(Mangler::mangle_method("List", "push", params), "_ZN4List4pushEi");
}

TEST(ManglerMethod, MethodWithMultipleParams) {
    const std::vector<Type> params = {
        Type::integer(32, true),
        Type::struct_type("str")
    };
    EXPECT_EQ(Mangler::mangle_method("Map", "insert", params), "_ZN3Map6insertEi3str");
}

TEST(ManglerMethod, GetterMethod) {
    EXPECT_EQ(Mangler::mangle_method("Vector", "size"), "_ZN6Vector4sizeE");
}

TEST(ManglerGeneric, StructSingleTypeArg) {
    const std::vector<Type> typeArgs = {Type::integer(32, true)};
    EXPECT_EQ(Mangler::mangle_generic_struct("List", typeArgs), "_ZN4ListIiE");
}

TEST(ManglerGeneric, StructMultipleTypeArgs) {
    const std::vector<Type> typeArgs = {
        Type::integer(32, true),
        Type::struct_type("str")
    };
    EXPECT_EQ(Mangler::mangle_generic_struct("Map", typeArgs), "_ZN3MapIi3strE");
}

TEST(ManglerGeneric, StructWithStructTypeArg) {
    const std::vector<Type> typeArgs = {Type::struct_type("Point")};
    EXPECT_EQ(Mangler::mangle_generic_struct("List", typeArgs), "_ZN4ListI5PointE");
}

TEST(ManglerGeneric, NestedGeneric) {
    const std::vector<Type> innerArgs = {Type::integer(32, true)};
    const std::string innerMangled = Mangler::mangle_generic_struct("List", innerArgs);
    EXPECT_EQ(innerMangled, "_ZN4ListIiE");
}

TEST(ManglerGenericMethod, SimpleMethod) {
    const std::vector<Type> typeArgs = {Type::integer(32, true)};
    const std::vector<Type> params = {Type::integer(32, true)};
    EXPECT_EQ(
        Mangler::mangle_generic_method("List", typeArgs, "push", params),
        "_ZN4ListIiE4pushEi"
    );
}

TEST(ManglerGenericMethod, MethodNoParams) {
    const std::vector<Type> typeArgs = {Type::integer(32, true)};
    const std::vector<Type> params = {};
    EXPECT_EQ(
        Mangler::mangle_generic_method("List", typeArgs, "size", params),
        "_ZN4ListIiE4sizeE"
    );
}

TEST(ManglerDemangle, SimpleFunction) {
    EXPECT_EQ(Mangler::demangle("_Z3foo"), "foo");
}

TEST(ManglerDemangle, LongerFunction) {
    EXPECT_EQ(Mangler::demangle("_Z9calculate"), "calculate");
}

TEST(ManglerDemangle, Method) {
    const std::string result = Mangler::demangle("_ZN4List4pushE");
    EXPECT_EQ(result, "List::push");
}

TEST(ManglerDemangle, NestedMethod) {
    const std::string result = Mangler::demangle("_ZN6Vector4sizeE");
    EXPECT_EQ(result, "Vector::size");
}

TEST(ManglerDemangle, NonMangled) {
    EXPECT_EQ(Mangler::demangle("printf"), "printf");
}

TEST(ManglerDemangle, GenericStruct) {
    const std::string result = Mangler::demangle("_ZN4ListIiE");
    EXPECT_TRUE(result.find("List") != std::string::npos);
}