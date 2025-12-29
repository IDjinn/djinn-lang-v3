//
// Created by Luke on 06/12/2025.
//

#include "parser.h"

#include <llvm/IR/Intrinsics.h>

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {
}

Token &Parser::peek() {
    return tokens[current];
}

Token &Parser::previous() {
    return tokens[current - 1];
}

Token &Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::check(const std::vector<TokenType> &types) {
    return std::ranges::any_of(types, [this](const TokenType type) { return check(type); });
}

bool Parser::check(const TokenType type) {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(const TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::isType() {
    return isType(peek());
}

bool Parser::isType(const Token &token) {
    if (token.type == TokenType::STRING || token.type == TokenType::AUTO || token.type == TokenType::VOID) {
        return true;
    }

    if (token.type != TokenType::IDENTIFIER) return false;

    if (token.value.starts_with('f')) {
        return string_to_type_kind.contains(token.value);
    }

    if (token.value.starts_with('i') || token.value.starts_with('u')) {
        const auto bits = token.value.substr(1);
        return std::ranges::all_of(bits, [](const char c) { return isalnum(c); });
    }

    return token.value == "void" || token.value == "string" || token.value == "auto";
}

Token &Parser::expect(const std::string &message, const TokenType type) {
    if (check(type)) return advance();
    throw std::runtime_error(message + " na linha " + std::to_string(peek().position.line));
}

Token &Parser::expect(const std::string &message, const std::vector<TokenType> &types) {
    for (auto &type: types) {
        if (check(type)) return advance();
    }
    throw std::runtime_error(message);
}

bool Parser::isAtEnd() {
    return peek().type == TokenType::END_OF_FILE;
}

std::unique_ptr<Type> Parser::parse_type() {
    const auto identifier = expect("Expected a type", {TokenType::IDENTIFIER, TokenType::VOID, TokenType::STRING, TokenType::AUTO});

    std::unique_ptr<Type> baseType;

    if (identifier.value.starts_with('f')) {
        const size_t bits = std::stol(identifier.value.substr(1));
        baseType = std::make_unique<Type>(Type::floated(bits));
    } else if (identifier.value.starts_with('i')) {
        const auto bits = std::stol(identifier.value.substr(1));
        baseType = std::make_unique<Type>(Type::integer(bits, true));
    } else if (identifier.value.starts_with('u')) {
        const auto bits = std::stol(identifier.value.substr(1));
        baseType = std::make_unique<Type>(Type::integer(bits, false));
    } else if (identifier.value == "void" || identifier.type == TokenType::VOID) {
        baseType = std::make_unique<Type>(Type::voided());
    } else if (identifier.value == "string" || identifier.type == TokenType::STRING) {
        baseType = std::make_unique<Type>(Type::stringed());
    } else if (identifier.value == "auto" || identifier.type == TokenType::AUTO) {
        baseType = std::make_unique<Type>(Type::autod());
    } else {
        throw std::exception(("invalid type kind with identifier " + identifier.value).c_str());
    }

    // Verifica se é um array: tipo[]
    if (match(TokenType::LBRACKET)) {
        expect("Expected ']' after '['", TokenType::RBRACKET);
        return std::make_unique<Type>(Type::array(std::move(*baseType)));
    }

    return baseType;
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>();
    while (!isAtEnd()) {
        program->functions.push_back(parse_function());
    }
    return program;
}

std::unique_ptr<FunctionDeclaration> Parser::parse_function() {
    auto returnType = this->parse_type();

    Token &name = expect("Esperado nome da função", TokenType::IDENTIFIER);
    auto params = parse_parameters();
    auto body = parse_block();

    return std::make_unique<FunctionDeclaration>(std::move(returnType), name.value, params, std::move(body));
}

std::vector<Parameter> Parser::parse_parameters() {
    auto parameters = std::vector<Parameter>();
    expect("Esperado '('", TokenType::LPAREN);

    if (!check(TokenType::RPAREN)) {
        do {
            auto type = this->parse_type();
            Token &paramName = expect("Esperado nome do parâmetro", TokenType::IDENTIFIER);
            parameters.emplace_back(std::move(type), paramName.value);
        } while (match(TokenType::COMMA));
    }

    expect("Esperado ')'", TokenType::RPAREN);
    return parameters;
}

std::unique_ptr<Block> Parser::parse_block() {
    expect("Esperado '{'", TokenType::LBRACE);

    auto block = std::make_unique<Block>();
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        block->statements.push_back(parse_statement());
    }

    expect("Esperado '}'", TokenType::RBRACE);
    return block;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    if (match(TokenType::RETURN)) {
        std::unique_ptr<Expression> value = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            value = parse_expression();
        }
        expect("Esperado ';' após return", TokenType::SEMICOLON);
        return std::make_unique<ReturnStatement>(std::move(value));
    }

    // Expression statement
    auto expr = parse_expression();
    expect("Esperado ';'", TokenType::SEMICOLON);
    return std::make_unique<ExpressionStatement>(std::move(expr));
}

std::unique_ptr<Expression> Parser::parse_expression() {
    return parse_or();
}

std::unique_ptr<Expression> Parser::parse_or() {
    auto left = parse_and();

    while (match(TokenType::OR_OR)) {
        TokenType op = previous().type;
        auto right = parse_and();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_and() {
    auto left = parse_equality();

    while (match(TokenType::AND_AND)) {
        TokenType op = previous().type;
        auto right = parse_equality();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_equality() {
    auto left = parse_comparison();

    while (match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL)) {
        TokenType op = previous().type;
        auto right = parse_comparison();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_comparison() {
    auto left = parse_term();

    while (match(TokenType::LESS) || match(TokenType::LESS_EQUAL) ||
           match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL)) {
        TokenType op = previous().type;
        auto right = parse_term();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_term() {
    auto left = parse_factor();

    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        TokenType op = previous().type;
        auto right = parse_factor();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_factor() {
    auto left = parse_unary();

    while (match(TokenType::STAR) || match(TokenType::SLASH) || match(TokenType::PERCENT)) {
        TokenType op = previous().type;
        auto right = parse_unary();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_unary() {
    if (match(TokenType::BANG) || match(TokenType::MINUS)) {
        TokenType op = previous().type;
        auto operand = parse_unary();
        return std::make_unique<UnaryExpression>(op, std::move(operand));
    }

    return parse_primary();
}

std::unique_ptr<Expression> Parser::parse_primary() {
    if (match(TokenType::INTEGER_LITERAL)) {
        const auto value = previous().value;
        if (value.ends_with("i") || value.ends_with("u")) {
            return std::make_unique<IntegerLiteral>(value.substr(0, value.length() - 2), true);
        }
        return std::make_unique<IntegerLiteral>(value, true);
    }

    if (match(TokenType::STRING_LITERAL)) {
        return std::make_unique<StringLiteral>(previous().value);
    }

    // Verifica se é um tipo (incluindo STRING, AUTO como tokens)
    if (check(TokenType::STRING) || check(TokenType::AUTO) ||
        (check(TokenType::IDENTIFIER) && isType(peek()))) {
        const auto identifier = advance();

        // Verifica se é array: tipo[]
        bool isArray = false;
        if (match(TokenType::LBRACKET)) {
            expect("Expected ']' after '['", TokenType::RBRACKET);
            isArray = true;
        }

        if (check(TokenType::IDENTIFIER)) { // type + identifier, should be variable creation
            const auto varName = advance().value;
            Type varType = Type::fromToken(identifier);
            if (isArray) {
                varType = Type::array(std::move(varType));
            }
            if (match(TokenType::EQUAL)) {
                auto value = parse_expression();
                return std::make_unique<VariableInit>(std::move(varType), varName, std::move(value));
            }
            return std::make_unique<VariableDeclaration>(std::move(varType), varName);
        }

        // Se não é declaração de variável mas é um tipo, pode ser erro ou uso especial
        // Retorna como identificador se não for um tipo conhecido
        if (!isType(identifier)) {
            return std::make_unique<Identifier>(identifier.value);
        }
    }

    if (check(TokenType::IDENTIFIER)) {
        const auto identifier = advance();

        // if (match(TokenType::SEMICOLON)) {
        //     return std::make_unique<VariableDeclaration>(Type::autod(), identifier.value);
        // }

        std::string name = identifier.value;

        if (match(TokenType::LPAREN)) {
            std::vector<std::unique_ptr<Expression> > args;

            if (!check(TokenType::RPAREN)) {
                do {
                    args.push_back(parse_expression());
                } while (match(TokenType::COMMA));
            }

            expect("Esperado ')' após argumentos", TokenType::RPAREN);
            return std::make_unique<FunctionCall>(name, std::move(args));
        }

        if (match(TokenType::EQUAL)) {
            auto value = parse_expression();
            return std::make_unique<Assignment>(name, std::move(value));
        }

        return std::make_unique<Identifier>(name);
    }

    // Expressão agrupada: (expr)
    if (match(TokenType::LPAREN)) {
        auto expr = parse_expression();
        expect("Esperado ')' após expressão", TokenType::RPAREN);
        return expr;
    }

    throw std::runtime_error("Expressão inesperada na linha " + std::to_string(peek().position.line));
}
