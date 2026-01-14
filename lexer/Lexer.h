//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_LEXER_H
#define DJINN_LEXER_H

#include <string>
#include <vector>
#include "./Token.h"

class Lexer {
public:
    explicit Lexer(std::string source, std::string fileId = "main");

    std::vector<Token> tokenize();

private:
    std::string source;
    std::string fileId;
    size_t pos = 0;
    uint32_t line = 1;
    uint32_t column = 1;

    char peek() const;

    char peekNext() const;

    char advance();

    void skip_whitespace();

    Token make_token(TokenType type, const std::string &value) const;

    Token read_string();

    Token read_number();

    Token read_identifier();
};

#endif //DJINN_LEXER_H