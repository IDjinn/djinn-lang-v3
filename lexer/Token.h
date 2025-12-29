//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_TOKEN_H
#define DJINN_TOKEN_H
#include <cstdint>
#include <string>

#include "./TokenType.h"


struct Position {
    uint32_t line, column, index;
};

struct Token {
    Position position;
    TokenType type;
    std::string value;
};


#endif //DJINN_TOKEN_H