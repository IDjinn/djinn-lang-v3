//
// Created by Luke on 06/12/2025.
//

#include "parser.h"

#include <llvm/IR/Intrinsics.h>

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {
}

void Parser::pushScope() {
    currentScope = std::make_shared<Scope>(currentScope);
}

void Parser::popScope() {
    if (!currentScope->parent) {
        throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN, "tentativa de sair do escopo global");
    }
    currentScope = currentScope->parent;
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

    if (currentScope->has_struct_in_current_scope(token.value)) {
        return true;
    }

    return token.value == "void" || token.value == "string" || token.value == "auto";
}

Token &Parser::expect(const std::string &message, const TokenType type) {
    if (check(type)) return advance();
    const auto &token = previous();
    const uint32_t col = token.position.column + token.value.length();
    throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN, message,
                       SourceLocation(token.position.line, col, 1));
}

Token &Parser::expect(const std::string &message, const std::vector<TokenType> &types) {
    for (auto &type: types) {
        if (check(type)) return advance();
    }
    const auto &token = previous();
    uint32_t col = token.position.column + token.value.length();
    throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN, message,
                       SourceLocation(token.position.line, col, 1));
}

bool Parser::isAtEnd() {
    return peek().type == TokenType::END_OF_FILE;
}

std::unique_ptr<Type> Parser::parse_type() {
    const auto identifier = expect("Expected a type", {
                                       TokenType::IDENTIFIER, TokenType::VOID, TokenType::STRING, TokenType::AUTO
                                   });

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
    } else if (this->currentScope->has_struct_in_current_scope(identifier.value)) {
        baseType = std::make_unique<Type>(Type::struct_type(identifier.value));
    } else {
        throw CompileError(DiagnosticCode::EXPECTED_TYPE, "tipo inválido: " + identifier.value,
                           SourceLocation(identifier.position.line, identifier.position.column, identifier.value.length()));
    }

    if (match(TokenType::LBRACKET)) {
        expect("Expected ']' after '['", TokenType::RBRACKET);
        return std::make_unique<Type>(Type::array(std::move(*baseType)));
    }

    return baseType;
}

std::unique_ptr<StructDeclaration> Parser::parse_struct() {
    expect("Esperado 'struct'", TokenType::STRUCT);
    const auto name = match(TokenType::IDENTIFIER) ? previous().value : Type::generate_struct_name();

    currentScope->define_struct(name, Type::struct_type(name));

    expect("Esperado '{'", TokenType::LBRACE);

    std::vector<StructField> fields;
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        auto fieldType = parse_type();
        Token &fieldName = expect("Esperado nome do campo", TokenType::IDENTIFIER);
        expect("Esperado ';' após campo", TokenType::SEMICOLON);
        fields.emplace_back(std::move(fieldType), fieldName.value);
    }

    expect("Esperado '}'", TokenType::RBRACE);

    return std::make_unique<StructDeclaration>(name, std::move(fields));
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>();
    while (!isAtEnd()) {
        if (check(TokenType::STRUCT)) {
            auto structDecl = parse_struct();

            // struct { ... } func() { ... }
            if (!check(TokenType::IDENTIFIER)) {
                program->structs.push_back(std::move(structDecl));
            } else {
                auto returnType = std::make_unique<Type>(Type::struct_type(structDecl->name));
                program->structs.push_back(std::move(structDecl));
                program->functions.push_back(parse_function_with_type(std::move(returnType)));
            }
        } else {
            program->functions.push_back(parse_function());
        }
    }
    return program;
}

std::unique_ptr<FunctionDeclaration> Parser::parse_function() {
    auto returnType = this->parse_type();
    return parse_function_with_type(std::move(returnType));
}

std::unique_ptr<FunctionDeclaration> Parser::parse_function_with_type(std::unique_ptr<Type> returnType) {
    Token &name = expect("Esperado nome da função", TokenType::IDENTIFIER);

    pushScope();

    auto params = parse_parameters();

    for (const auto &param: params) {
        currentScope->define_variable(param.name, *param.type);
    }

    auto body = parse_block();

    popScope();

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

    pushScope();

    auto block = std::make_unique<Block>();
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        block->statements.push_back(parse_statement());
    }

    popScope();

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

    return parse_postfix();
}

std::unique_ptr<Expression> Parser::parse_postfix() {
    auto expr = parse_primary();

    while (true) {
        if (match(TokenType::DOT)) {
            Token &fieldName = expect("Esperado nome do campo após '.'", TokenType::IDENTIFIER);
            expr = std::make_unique<FieldAccess>(std::move(expr), fieldName.value);
        } else {
            break;
        }
    }

    return expr;
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

    if (check(TokenType::STRING) || check(TokenType::AUTO) ||
        (check(TokenType::IDENTIFIER) && isType(peek()))) {
        const auto identifier = advance();

        bool isArray = false;
        if (match(TokenType::LBRACKET)) {
            expect("Expected ']' after '['", TokenType::RBRACKET);
            isArray = true;
        }

        if (check(TokenType::IDENTIFIER)) {
            // type + identifier, should be variable creation
            const auto varName = advance().value;
            Type varType = currentScope->has_struct_declared(identifier.value)
                               ? Type::struct_type(identifier.value)
                               : Type::fromToken(identifier);
            if (isArray) {
                varType = Type::array(std::move(varType));
            }

            currentScope->define_variable(varName, varType);
            if (match(TokenType::EQUAL)) {
                auto value = parse_expression();
                return std::make_unique<VariableInit>(std::move(varType), varName, std::move(value));
            }
            return std::make_unique<VariableDeclaration>(std::move(varType), varName);
        }

        if (!isType(identifier)) {
            return std::make_unique<Identifier>(identifier.value);
        }
    }

    if (check(TokenType::IDENTIFIER)) {
        const auto identifier = advance();
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

    if (check(TokenType::LBRACE)) {
        return parse_brace_initializer();
    }

    if (match(TokenType::LPAREN)) {
        auto expr = parse_expression();
        expect("Esperado ')' após expressão", TokenType::RPAREN);
        return expr;
    }

    const auto &tok = peek();
    throw CompileError(DiagnosticCode::EXPECTED_EXPRESSION, "expressão inesperada",
                       SourceLocation(tok.position.line, tok.position.column, tok.value.empty() ? 1 : tok.value.length()));
}

std::unique_ptr<Expression> Parser::parse_brace_initializer() {
    expect("Esperado '{'", TokenType::LBRACE);

    std::vector<InitializerElement> elements;

    if (!check(TokenType::RBRACE)) {
        do {
            if (match(TokenType::DOT)) {
                // Designated initializer: .field = value
                Token &fieldName = expect("Esperado nome do campo", TokenType::IDENTIFIER);
                expect("Esperado '=' após nome do campo", TokenType::EQUAL);
                auto value = parse_expression();
                elements.emplace_back(fieldName.value, std::move(value));
            } else {
                // Positional initializer: value
                auto value = parse_expression();
                elements.emplace_back(std::move(value));
            }
        } while (match(TokenType::COMMA));
    }

    expect("Esperado '}'", TokenType::RBRACE);

    return std::make_unique<BraceInitializer>(std::move(elements));
}
