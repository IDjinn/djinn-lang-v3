//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_PARSER_H
#define DJINN_PARSER_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include "../lexer/Token.h"
#include "../lexer/TokenType.h"
#include "AST.h"

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, DiagnosticEngine &diagnostics);

    std::unique_ptr<Program> parse(const std::string &program_name);

private:
    std::vector<Token> tokens;
    size_t current = 0;

    std::shared_ptr<Scope> currentScope = std::make_shared<Scope>();
    DiagnosticEngine &_diagnostics;

    void pushScope();

    void popScope();

    Token &peek();

    Token &previous();

    Token &advance();

    bool check(const std::vector<TokenType> &types);

    bool check(TokenType type);

    bool match(TokenType type);

    bool isType();

    bool isType(const Token &token) const;

    Token &expect(const std::string &message, TokenType type);

    Token &expect(const std::string &message, const std::vector<TokenType> &types);

    bool isAtEnd();

    void synchronize();

    std::unique_ptr<Type> parse_type();

    std::unique_ptr<StructDeclaration> parse_struct();

    std::vector<AttributeUsageDeclaration> parse_attributes();

    std::unique_ptr<InterfaceDeclaration> parse_interface();

    std::unique_ptr<StructMethodDeclaration> parse_method(bool allowBody = true);

    StructProperty parse_property(std::unique_ptr<Type> type, SourceIdentifier name);

    std::vector<VisibilityModifier> parse_modifiers();

    std::unique_ptr<FunctionDeclaration> parse_function();

    std::unique_ptr<FunctionDeclaration> parse_function_with_type(std::unique_ptr<Type> returnType);

    std::vector<Parameter> parse_parameters();

    std::unique_ptr<Block> parse_block();

    std::unique_ptr<Statement> parse_statement();

    std::unique_ptr<IfStatement> parse_if_statement();

    std::unique_ptr<ForStatement> parse_for_statement();

    std::unique_ptr<WhileStatement> parse_while_statement();

    std::unique_ptr<DoWhileStatement> parse_do_while_statement();

    std::unique_ptr<SwitchStatement> parse_switch_statement();

    std::unique_ptr<Expression> parse_expression();

    std::unique_ptr<Expression> parse_or();

    std::unique_ptr<Expression> parse_and();

    std::unique_ptr<Expression> parse_equality();

    std::unique_ptr<Expression> parse_comparison();

    std::unique_ptr<Expression> parse_term();

    std::unique_ptr<Expression> parse_factor();

    std::unique_ptr<Expression> parse_unary();

    std::unique_ptr<Expression> parse_postfix();

    std::unique_ptr<Expression> parse_primary();

    std::unique_ptr<Expression> parse_brace_initializer();

    std::unique_ptr<Expression> parse_array_literal();

    std::unique_ptr<Expression> parse_typed_array_literal(const Token &typeToken);

    std::unique_ptr<Expression> parse_switch_expression();

    std::unique_ptr<ExternFunctionDeclaration> parse_extern_function(const std::string &abi);

    void parse_extern(Program *program);

    std::unique_ptr<NamespaceDeclaration> parse_namespace();

    std::unique_ptr<EnumDeclaration> parse_enum();

    QualifiedName parse_qualified_name();

    std::unique_ptr<ImportDeclaration> parse_import();
};

#endif //DJINN_PARSER_H