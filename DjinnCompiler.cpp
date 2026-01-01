//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

#include <fstream>
#include <iostream>

#include "codegen/CodeGen.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"

CompilerResult DjinnCompiler::run(const std::string &source) {
    Lexer lexer(source);
    const auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto program = parser.parse();
    // program->print(std::cout, 2);
    // std::cout << "\n\n";

    CodeGen codegen;
    codegen.generate(*program);
    codegen.optimize();
    const auto result = codegen.print();

    std::ofstream output;
    output.open("output.ll");
    output << result;
    output.close();

    system("clang output.ll -o output.exe");
    const auto returnCode = system("output.exe");
    return {
        .returnCode =  returnCode,
        .tokens = tokens,
        .program = std::move(program),
        .ir = result,
    };
}
