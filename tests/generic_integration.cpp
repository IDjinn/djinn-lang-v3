//
// Created by Luke on 02/01/2026.
//

#include <gtest/gtest.h>

#include "../DjinnCompiler.h"
#include "../generator/GeneratorScope.h"
#include "../generator/Mangler.h"


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