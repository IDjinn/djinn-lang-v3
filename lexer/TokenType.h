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
    PLUS_PLUS, // ++
    MINUS_MINUS, // --
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
    AMPERSAND, // & (address-of / bitwise-and)
    // -------- //

    // OPERADORES BITWISE //
    PIPE, // |
    CARET, // ^
    TILDE, // ~
    LESS_LESS, // <<
    GREATER_GREATER, // >>
    // -------- //

    // ATRIBUIÇÃO //
    EQUAL, // =
    ARROW, // =>
    THIN_ARROW, // ->
    PLUS_EQUAL, // +=
    MINUS_EQUAL, // -=
    STAR_EQUAL, // *=
    SLASH_EQUAL, // /=
    PERCENT_EQUAL, // %=
    AMPERSAND_EQUAL, // &=
    PIPE_EQUAL, // |=
    CARET_EQUAL, // ^=
    LESS_LESS_EQUAL, // <<=
    GREATER_GREATER_EQUAL, // >>=
    // -------- //
    COLON, // :
    COLON_COLON, // ::
    DOT, // .
    DOT_DOT, // ..
    DOT_DOT_EQUAL, // ..=
    DOT_DOT_DOT, // ...
    AT, // @

    // SPECIAL TYPES
    ENUM, // enum
    NEW, // new
    IMPL, // impl
    WHERE, // where (for generics)
    IN, // in
    ASYNC, // async
    AWAIT, // await
    YIELD, // yield
    SPAWN, // spawn (threading)

    // OPERATOR OVERLOADING
    OPERATOR, // operator

    // COMPILE TIME
    CONST_EXPR, // constexpr
    CONST_EVAL, // consteval
    CONST, // constant literal

    // MACROS
    MACRO, // macro

    // PATTERN MATCHING
    IS, // is (type check for object)
};


#endif //DJINN_TOKENTYPE_H