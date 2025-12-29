//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_PARSER_H
#define DJINN_PARSER_H

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include "../lexer/Token.h"
#include "../lexer/TokenType.h"
#include "AST.h"

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::unique_ptr<Program> parse();

private:
    std::vector<Token> tokens;
    size_t current = 0;

    Token& peek();
    Token& previous();
    Token& advance();
    bool check(const std::vector<TokenType> &types);
    bool check(TokenType type);
    bool match(TokenType type);
    bool isType();

    static bool isType(const Token& token);
    Token& expect(const std::string& message, TokenType type);
    Token& expect(const std::string& message, const std::vector<TokenType> &types);
    bool isAtEnd();

    std::unique_ptr<Type> parse_type();
    std::unique_ptr<FunctionDeclaration> parse_function();

    std::vector<Parameter> parse_parameters();
    std::unique_ptr<Block> parse_block();
    std::unique_ptr<Statement> parse_statement();

    std::unique_ptr<Expression> parse_expression();
    std::unique_ptr<Expression> parse_or();
    std::unique_ptr<Expression> parse_and();
    std::unique_ptr<Expression> parse_equality();
    std::unique_ptr<Expression> parse_comparison();
    std::unique_ptr<Expression> parse_term();
    std::unique_ptr<Expression> parse_factor();
    std::unique_ptr<Expression> parse_unary();
    std::unique_ptr<Expression> parse_primary();
};

#endif //DJINN_PARSER_H
