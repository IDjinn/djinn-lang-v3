#include <fstream>
#include <iostream>
#include <thread>

#include "DjinnCompiler.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"
#include "generator/Generator.h"
#include "diagnostics/Diagnostic.h"

int main(int argc, char *argv[]) {
    const std::string source = R"(
        struct Result {
            i32 value;
        }

        void main() {
            Result result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {false, ""});
    printf(result.ir.c_str());
    return 0;
}
