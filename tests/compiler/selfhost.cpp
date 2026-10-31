#include <filesystem>
#include <gtest/gtest.h>
#include "DjinnCompiler.h"
#include <algorithm>

TEST(SelfHost, Stage1CompilesWithStage0 )
{
    const std::filesystem::path projectDir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "djinnc";
    ASSERT_TRUE(std::filesystem::exists(projectDir / "src" / "main.djinn"));

    CompilerOptions options;
    options.generateBinary = false;
    options.noCache = true;
    options.nativeLibraries = {"-lLLVM-C"};

    const auto result = DjinnCompiler::compileFromDirectory(projectDir, options);
    EXPECT_FALSE(std::ranges::any_of(
     result.diagnostics,
     [](const Diagnostic& diagnostic) -> bool {
         return diagnostic.severity == Severity::Error;
     }
 ));
}
