//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_TOKENTYPE_H
#define DJINN_TOKENTYPE_H

enum class TokenType
{
    UNKNOWN = 0,
    END_OF_FILE,

    EXTERN,
    RETURN,
    STRUCT,
    NAMESPACE,
    IMPORT,
    MUT,

    // CONTROL FLOW //
    IF,
    ELSE,
    FOR,
    WHILE,
    DO,
    SWITCH,
    CASE,
    DEFAULT,
    BREAK,
    CONTINUE,
    INTERFACE,
    PUBLIC,
    PRIVATE,
    STATIC,
    THIS,
    // -------- //

    // VALUE TYPES //
    INT,
    UINT,
    VOID,
    FLOAT,
    STRING,
    AUTO,
    FALSE, // true
    TRUE, // false
    // -------- //

    IDENTIFIER,
    STRING_LITERAL,
    INTEGER_LITERAL,
    FLOAT_LITERAL,

    // -------- //
    LPAREN, // (
    RPAREN, // )
    LBRACE, // {
    RBRACE, // }
    LBRACKET, // [
    RBRACKET, // ]
    SEMICOLON, // ;
    COMMA, // ,
    // -------- //

    // OPERADORES ARITMÉTICOS //
    PLUS, // +
    MINUS, // -
    STAR, // *
    SLASH, // /
    PERCENT, // %
    // -------- //

    // OPERADORES DE COMPARAÇÃO //
    EQUAL_EQUAL, // ==
    BANG_EQUAL, // !=
    LESS, // <
    LESS_EQUAL, // <=
    GREATER, // >
    GREATER_EQUAL, // >=
    // -------- //

    // OPERADORES LÓGICOS //
    BANG, // !
    AND_AND, // &&
    OR_OR, // ||
    AMPERSAND, // & (address-of)
    // -------- //

    // ATRIBUIÇÃO //
    EQUAL, // =
    ARROW, // =>
    THIN_ARROW, // ->
    // -------- //
    COLON, // :
    COLON_COLON, // ::
    DOT, // .
    DOT_DOT, // ..
    DOT_DOT_DOT, // ...
    AT, // @

    // SPECIAL TYPES
    ENUM, // enum
    NEW, // new
    IMPL, // impl
    WHERE, // where (for generics)
    ASYNC, // async
    AWAIT, // await
    YIELD, // yield
    SPAWN, // spawn (threading)

    // COMPILE TIME
    CONST_EXPR, // constexpr
    CONST_EVAL // consteval
};


#endif //DJINN_TOKENTYPE_H