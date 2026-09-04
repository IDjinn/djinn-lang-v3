//
// Created by Luke on 06/12/2025.
//

#include "./Lexer.h"
#include <unordered_map>

#include "../utils/Logger.h"

static std::unordered_map<std::string, TokenType> keywords = {
    {"void", TokenType::VOID},
    {"return", TokenType::RETURN},
    {"auto", TokenType::AUTO},
    {"extern", TokenType::EXTERN},
    {"struct", TokenType::STRUCT},
    {"namespace", TokenType::NAMESPACE},
    {"import", TokenType::IMPORT},
    {"mut", TokenType::MUT},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"for", TokenType::FOR},
    {"while", TokenType::WHILE},
    {"do", TokenType::DO},
    {"switch", TokenType::SWITCH},
    {"case", TokenType::CASE},
    {"default", TokenType::DEFAULT},
    {"break", TokenType::BREAK},
    {"continue", TokenType::CONTINUE},
    {"interface", TokenType::INTERFACE},
    {"public", TokenType::PUBLIC},
    {"private", TokenType::PRIVATE},
    {"static", TokenType::STATIC},
    {"this", TokenType::THIS},
    {"enum", TokenType::ENUM},
    {"new", TokenType::NEW},
    {"impl", TokenType::IMPL},
    {"where", TokenType::WHERE},
    {"async", TokenType::ASYNC},
    {"await", TokenType::AWAIT},
    {"yield", TokenType::YIELD},
    {"spawn", TokenType::SPAWN},
    {"in", TokenType::IN},
    {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"operator", TokenType::OPERATOR},
    {"const", TokenType::CONST},
    {"constexpr", TokenType::CONST_EXPR},
    {"consteval", TokenType::CONST_EVAL},
    {"macro", TokenType::MACRO},
    {"is", TokenType::IS},
    {"null", TokenType::NULL_KW},
    {"throw", TokenType::THROW},
    {"throws", TokenType::THROWS},
    {"try", TokenType::TRY},
    {"catch", TokenType::CATCH},
    {"finally", TokenType::FINALLY},
    {"require", TokenType::REQUIRE},
    {"ensure", TokenType::ENSURE},
};

Lexer::Lexer(std::string source, std::string fileId)
    : source(std::move(source)), fileId(std::move(fileId))
{
}

char Lexer::peek() const
{
    if (pos >= source.size()) return '\0';
    return source[pos];
}

char Lexer::peekNext() const
{
    if (pos + 1 >= source.size()) return '\0';
    return source[pos + 1];
}

char Lexer::advance()
{
    const char c = peek();
    pos++;
    if (c == '\n')
    {
        line++;
        column = 1;
    }
    else
    {
        column++;
    }
    return c;
}

void Lexer::skip_whitespace()
{
    while (pos < source.size())
    {
        if (const char c = peek(); c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            advance();
        }
        else if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '/')
        {
            while (peek() != '\n' && peek() != '\0') advance();
        }
        else
        {
            break;
        }
    }
}

Token Lexer::make_token(const TokenType type, const std::string& value) const
{
    return Token{{fileId, line, column, static_cast<uint32_t>(pos)}, type, value};
}

Token Lexer::make_token(const TokenType type, const std::string& value, uint32_t startLine,
                        uint32_t startColumn) const
{
    return Token{{fileId, startLine, startColumn, static_cast<uint32_t>(value.length())}, type, value};
}

Token Lexer::read_string()
{
    const uint32_t startLine = line;
    const uint32_t startColumn = column;

    // Block string: """...""" — multi-line, no escape processing for backslashes,
    // closing terminator is """. Inner double quotes are literal.
    if (pos + 2 < source.size() && source[pos + 1] == '"' && source[pos + 2] == '"')
    {
        advance(); // first "
        advance(); // second "
        advance(); // third "
        std::string value;
        while (pos < source.size())
        {
            if (peek() == '"' && pos + 2 < source.size() && source[pos + 1] == '"' && source[pos + 2] == '"')
            {
                advance();
                advance();
                advance();
                return make_token(TokenType::STRING_LITERAL, value, startLine, startColumn);
            }
            value += advance();
        }
        return make_token(TokenType::STRING_LITERAL, value, startLine, startColumn);
    }

    advance();
    std::string value;
    while (peek() != '"' && peek() != '\0')
    {
        if (peek() == '\\')
        {
            advance();
            const char escaped = advance();
            switch (escaped)
            {
            case 'n': value += '\n';
                break;
            case 't': value += '\t';
                break;
            case '\\': value += '\\';
                break;
            case '"': value += '"';
                break;
            default: value += escaped;
            }
        }
        else
        {
            value += advance();
        }
    }
    if (peek() == '"') advance();
    return make_token(TokenType::STRING_LITERAL, value, startLine, startColumn);
}

bool is_valid_number_token_slice(char c, size_t index, char prev)
{
    if (isdigit(c))
        return true;

    if ((c == '_' || c == '\'') && index > 0 && isdigit(prev))
        return true;

    return false;
}

Token Lexer::read_number()
{
    const uint32_t startLine = line;
    const uint32_t startColumn = column;

    std::string value;

    // One trailing suffix char: sign (u/i) or overflow mode (w/t/c/s)
    const auto readSuffix = [this, &value](const bool allowCHexDigit = true)
    {
        const char c = peek();
        if (c == 'u' || c == 'i' || c == 'w' || c == 't' || c == 's' || (allowCHexDigit && c == 'c'))
        {
            value += advance();
        }
    };

    // Check for hex (0x) or binary (0b) prefix
    if (peek() == '0' && (peekNext() == 'x' || peekNext() == 'X'))
    {
        value += advance(); // '0'
        value += advance(); // 'x'
        while (isxdigit(peek()) || peek() == '_' || peek() == '\'')
        {
            value += advance();
        }
        readSuffix(false); // 'c' is a hex digit, never a suffix here
        return make_token(TokenType::INTEGER_LITERAL, value, startLine, startColumn);
    }

    if (peek() == '0' && (peekNext() == 'b' || peekNext() == 'B'))
    {
        value += advance(); // '0'
        value += advance(); // 'b'
        while (peek() == '0' || peek() == '1' || peek() == '_' || peek() == '\'')
        {
            value += advance();
        }
        readSuffix();
        return make_token(TokenType::INTEGER_LITERAL, value, startLine, startColumn);
    }

    while (isdigit(peek()) || peek() == '_' || peek() == '\'')
    {
        value += advance();
    }

    bool isFloat = false;

    if (peek() == '.' && isdigit(peekNext()))
    {
        isFloat = true;
        value += advance();

        while (isdigit(peek()) || peek() == '_' || peek() == '\'')
        {
            value += advance();
        }
    }

    // 1e9 or 1.5e9
    if (peek() == 'e' || peek() == 'E')
    {
        isFloat = true;
        value += advance();

        if (peek() == '+' || peek() == '-')
        {
            value += advance();
        }

        if (!isdigit(peek()))
        {
            LOG_ERROR("invalid exponent");
            throw std::runtime_error("invalid exponent");
        }

        while (isdigit(peek()))
        {
            value += advance();
        }
    }

    if (isFloat)
    {
        return make_token(TokenType::FLOAT_LITERAL, value, startLine, startColumn);
    }

    readSuffix();

    return make_token(TokenType::INTEGER_LITERAL, value, startLine, startColumn);
}

Token Lexer::read_identifier()
{
    const uint32_t startLine = line;
    const uint32_t startColumn = column;
    std::string value;
    while (isalnum(peek()) || peek() == '_')
    {
        value += advance();
    }

    if (const auto it = keywords.find(value); it != keywords.end())
    {
        return make_token(it->second, value, startLine, startColumn);
    }
    return make_token(TokenType::IDENTIFIER, value, startLine, startColumn);
}

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;

    while (pos < source.size())
    {
        skip_whitespace();
        if (pos >= source.size()) break;

        const char c = peek();

        if (c == '"')
        {
            tokens.push_back(read_string());
        }
        else if (isdigit(c))
        {
            tokens.push_back(read_number());
        }
        else if (isalpha(c) || c == '_')
        {
            tokens.push_back(read_identifier());
        }
        else
        {
            switch (c)
            {
            case '(': tokens.push_back(make_token(TokenType::LPAREN, "("));
                advance();
                break;
            case ')': tokens.push_back(make_token(TokenType::RPAREN, ")"));
                advance();
                break;
            case '{': tokens.push_back(make_token(TokenType::LBRACE, "{"));
                advance();
                break;
            case '}': tokens.push_back(make_token(TokenType::RBRACE, "}"));
                advance();
                break;
            case '[': tokens.push_back(make_token(TokenType::LBRACKET, "["));
                advance();
                break;
            case ']': tokens.push_back(make_token(TokenType::RBRACKET, "]"));
                advance();
                break;
            case ';': tokens.push_back(make_token(TokenType::SEMICOLON, ";"));
                advance();
                break;
            case ',': tokens.push_back(make_token(TokenType::COMMA, ","));
                advance();
                break;
            case ':':
                if (peekNext() == ':')
                {
                    tokens.push_back(make_token(TokenType::COLON_COLON, "::"));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::COLON, ":"));
                    advance();
                }
                break;
            case '.':
                if (pos + 2 < source.size() && source[pos + 1] == '.' && source[pos + 2] == '.')
                {
                    tokens.push_back(make_token(TokenType::DOT_DOT_DOT, "..."));
                    advance();
                    advance();
                    advance();
                }
                else if (pos + 2 < source.size() && source[pos + 1] == '.' && source[pos + 2] == '=')
                {
                    tokens.push_back(make_token(TokenType::DOT_DOT_EQUAL, "..="));
                    advance();
                    advance();
                    advance();
                }
                else if (peekNext() == '.')
                {
                    tokens.push_back(make_token(TokenType::DOT_DOT, ".."));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::DOT, "."));
                    advance();
                }
                break;
            case '+':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::PLUS_EQUAL, "+="));
                    advance();
                    advance();
                }
                else if (peekNext() == '+')
                {
                    tokens.push_back(make_token(TokenType::PLUS_PLUS, "++"));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::PLUS, "+"));
                    advance();
                }
                break;
            case '-':
                if (peekNext() == '>')
                {
                    tokens.push_back(make_token(TokenType::THIN_ARROW, "->"));
                    advance();
                    advance();
                }
                else if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::MINUS_EQUAL, "-="));
                    advance();
                    advance();
                }
                else if (peekNext() == '-')
                {
                    tokens.push_back(make_token(TokenType::MINUS_MINUS, "--"));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::MINUS, "-"));
                    advance();
                }
                break;
            case '*':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::STAR_EQUAL, "*="));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::STAR, "*"));
                    advance();
                }
                break;
            case '/':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::SLASH_EQUAL, "/="));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::SLASH, "/"));
                    advance();
                }
                break;
            case '@': tokens.push_back(make_token(TokenType::AT, "@"));
                advance();
                break;
            case '%':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::PERCENT_EQUAL, "%="));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::PERCENT, "%"));
                    advance();
                }
                break;
            case '=':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::EQUAL_EQUAL, "=="));
                    advance();
                    advance();
                }
                else if (peekNext() == '>')
                {
                    tokens.push_back(make_token(TokenType::ARROW, "=>"));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::EQUAL, "="));
                    advance();
                }
                break;
            case '!':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::BANG_EQUAL, "!="));
                    advance();
                    advance();
                }
                else if (peekNext() == '.')
                {
                    tokens.push_back(make_token(TokenType::BANG_DOT, "!."));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::BANG, "!"));
                    advance();
                }
                break;
            case '?':
                if (peekNext() == '?')
                {
                    advance();
                    advance();
                    if (peek() == '=')
                    {
                        tokens.push_back(make_token(TokenType::QUESTION_QUESTION_EQUAL, "??="));
                        advance();
                    }
                    else
                    {
                        tokens.push_back(make_token(TokenType::QUESTION_QUESTION, "??"));
                    }
                }
                else if (peekNext() == '.')
                {
                    tokens.push_back(make_token(TokenType::QUESTION_DOT, "?."));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::QUESTION, "?"));
                    advance();
                }
                break;
            case '<':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::LESS_EQUAL, "<="));
                    advance();
                    advance();
                }
                else if (peekNext() == '<')
                {
                    advance();
                    advance(); // consume <<
                    if (peek() == '=')
                    {
                        tokens.push_back(make_token(TokenType::LESS_LESS_EQUAL, "<<="));
                        advance();
                    }
                    else
                    {
                        tokens.push_back(make_token(TokenType::LESS_LESS, "<<"));
                    }
                }
                else
                {
                    tokens.push_back(make_token(TokenType::LESS, "<"));
                    advance();
                }
                break;
            case '>':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::GREATER_EQUAL, ">="));
                    advance();
                    advance();
                }
                else if (peekNext() == '>')
                {
                    advance();
                    advance(); // consume >>
                    if (peek() == '=')
                    {
                        tokens.push_back(make_token(TokenType::GREATER_GREATER_EQUAL, ">>="));
                        advance();
                    }
                    else
                    {
                        tokens.push_back(make_token(TokenType::GREATER_GREATER, ">>"));
                    }
                }
                else
                {
                    tokens.push_back(make_token(TokenType::GREATER, ">"));
                    advance();
                }
                break;
            case '&':
                if (peekNext() == '&')
                {
                    tokens.push_back(make_token(TokenType::AND_AND, "&&"));
                    advance();
                    advance();
                }
                else if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::AMPERSAND_EQUAL, "&="));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::AMPERSAND, "&"));
                    advance();
                }
                break;
            case '|':
                if (peekNext() == '|')
                {
                    tokens.push_back(make_token(TokenType::OR_OR, "||"));
                    advance();
                    advance();
                }
                else if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::PIPE_EQUAL, "|="));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::PIPE, "|"));
                    advance();
                }
                break;
            case '^':
                if (peekNext() == '=')
                {
                    tokens.push_back(make_token(TokenType::CARET_EQUAL, "^="));
                    advance();
                    advance();
                }
                else
                {
                    tokens.push_back(make_token(TokenType::CARET, "^"));
                    advance();
                }
                break;
            case '~': tokens.push_back(make_token(TokenType::TILDE, "~"));
                advance();
                break;
            default:
                tokens.push_back(make_token(TokenType::UNKNOWN, std::string(1, c)));
                advance();
                break;
            }
        }
    }

    tokens.push_back(make_token(TokenType::END_OF_FILE, ""));
    return tokens;
}