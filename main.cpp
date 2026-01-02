#include <fstream>
#include <iostream>
#include <thread>

#include "lexer/Lexer.h"
#include "parser/parser.h"
#include "codegen/CodeGen.h"
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

    printf("===SOURCE===\n%s\n\n", source.c_str());

    DiagnosticEngine diagnostics(source);

    try {
        Lexer lexer(source);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        const auto program = parser.parse();
        std::cout << "===PROGRAM===\n";
        program->print(std::cout, 2);
        std::cout << "\n\n";

        CodeGen codegen;
        codegen.generate(*program);
        codegen.optimize();
        const auto result = codegen.print();
        printf("===LLVM RESULT===\n%s\n\n", result.c_str());

        std::ofstream output;
        output.open("output.ll");
        output << result;
        output.close();

        system("clang output.ll -o output.exe");
        system("output.exe");
    } catch (const CompileError &e) {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr();
        return 1;
    }

    return 0;
}
