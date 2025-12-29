//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_TOKENTYPE_H
#define DJINN_TOKENTYPE_H


enum class TokenType {
    UNKNOWN = 0,
    END_OF_FILE,

    RETURN,
    TYPE,

    // VALUE TYPES //
    INT,
    UINT,
    VOID,
    FLOAT,
    STRING,
    AUTO,
    // -------- //

    IDENTIFIER,
    STRING_LITERAL,
    INTEGER_LITERAL,

    // -------- //
    LPAREN,      // (
    RPAREN,      // )
    LBRACE,      // {
    RBRACE,      // }
    LBRACKET,    // [
    RBRACKET,    // ]
    SEMICOLON,   // ;
    COMMA,       // ,
    // -------- //

    // OPERADORES ARITMÉTICOS //
    PLUS,        // +
    MINUS,       // -
    STAR,        // *
    SLASH,       // /
    PERCENT,     // %
    // -------- //

    // OPERADORES DE COMPARAÇÃO //
    EQUAL_EQUAL,   // ==
    BANG_EQUAL,    // !=
    LESS,          // <
    LESS_EQUAL,    // <=
    GREATER,       // >
    GREATER_EQUAL, // >=
    // -------- //

    // OPERADORES LÓGICOS //
    BANG,          // !
    AND_AND,       // &&
    OR_OR,         // ||
    // -------- //

    // ATRIBUIÇÃO //
    EQUAL,         // =
    // -------- //
};


#endif //DJINN_TOKENTYPE_H