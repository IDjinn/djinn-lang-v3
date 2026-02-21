//
// Created by Luke on 06/12/2025.
//

#include "parser.h"

#include <cctype>
#include <ranges>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/CommandLine.h>
#include <cassert>


#define PARSER_ERROR(code, msg, location) do { \
_diagnostics.emitAndPrint(Diagnostic(Severity::Error, code, msg, location)); \
throw CompileError(code, msg); \
} while (false);

#define PARSER_WARNING(code, msg, location) do { \
_diagnostics.emitAndPrint(Diagnostic(Severity::Warning, code, msg, location)); \
throw CompileError(code, msg); \
} while (false);

auto makeSourceIdentifier = [](const Token& token)
{
    return SourceIdentifier(token.value,
                            SourceLocation(token.position.fileId, token.position.line, token.position.column,
                                           token.value.length()));
};

auto isPrimitiveType = [](const std::string& name)
{
    if (name == "void" || name == "auto") return true;
    if (name.starts_with('f') && string_to_type_kind.contains(name)) return true;
    if ((name.starts_with('i') || name.starts_with('u')) && name.length() > 1)
    {
        return std::ranges::all_of(name.substr(1), [](const unsigned char c) { return std::isdigit(c); });
    }
    return false;
};

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics) : tokens(std::move(tokens)),
                                                                           _diagnostics(diagnostics)
{
}

void Parser::registerKnownType(const std::string& name)
{
    currentScope->define_struct(name, Type::struct_type(name));
}


void Parser::pushScope()
{
    currentScope = std::make_shared<Scope>(currentScope);
}

void Parser::popScope()
{
    if (!currentScope->parent)
    {
        PARSER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "tentativa de sair do escopo global", SourceLocation{});
    }
    currentScope = currentScope->parent;
}

Token& Parser::peek()
{
    return tokens[current];
}

Token& Parser::previous()
{
    return tokens[current - 1];
}

Token& Parser::advance()
{
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::check(const std::vector<TokenType>& types)
{
    return std::ranges::any_of(types, [this](const TokenType type) { return check(type); });
}

bool Parser::check(const TokenType type)
{
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(const TokenType type)
{
    if (check(type))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::isType()
{
    return isType(peek());
}

bool Parser::isType(const Token& token) const
{
    if (token.type == TokenType::AUTO || token.type == TokenType::VOID)
    {
        return true;
    }

    if (token.type != TokenType::IDENTIFIER) return false;

    // Built-in float types
    if (token.value.starts_with('f'))
    {
        return string_to_type_kind.contains(token.value);
    }

    // Built-in integer types (i32, u64, etc.)
    if (token.value.starts_with('i') || token.value.starts_with('u'))
    {
        const auto bits = token.value.substr(1);
        return !bits.empty() && std::ranges::all_of(bits, [](const unsigned char c) { return std::isdigit(c); });
    }

    // Already declared struct in current scope
    if (currentScope->has_struct_declared(token.value))
    {
        return true;
    }

    return token.value == "void" || token.value == "auto";
}

Token& Parser::expect(const std::string& message, const TokenType type)
{
    if (check(type)) return advance();
    const auto& token = previous();
    const uint32_t col = token.position.column + token.value.length();
    PARSER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, message,
                 SourceLocation(token.position.fileId, token.position.line, col, 1));
}

Token& Parser::expect(const std::string& message, const std::vector<TokenType>& types)
{
    for (auto& type : types)
    {
        if (check(type)) return advance();
    }
    const auto& token = previous();
    const uint32_t col = token.position.column + token.value.length();
    PARSER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, message,
                 SourceLocation(token.position.fileId, token.position.line, col, 1));
}

bool Parser::isAtEnd()
{
    return peek().type == TokenType::END_OF_FILE;
}

void Parser::synchronize()
{
    while (!isAtEnd())
    {
        const auto type = peek().type;
        if (type == TokenType::IMPORT || type == TokenType::EXTERN ||
            type == TokenType::NAMESPACE || type == TokenType::ENUM ||
            type == TokenType::INTERFACE || type == TokenType::STRUCT ||
            type == TokenType::LBRACKET)
        {
            return;
        }
        // If we hit a type-like token followed by an identifier, likely a function decl
        if (isType()) return;
        advance();
    }
}

std::unique_ptr<Type> Parser::parse_type()
{
    const auto identifier = expect("Expected a type", {
                                       TokenType::IDENTIFIER, TokenType::VOID, TokenType::AUTO
                                   });

    std::unique_ptr<Type> baseType;

    if (identifier.value.starts_with('f') && string_to_type_kind.contains(identifier.value))
    {
        const size_t bits = std::stol(identifier.value.substr(1));
        baseType = std::make_unique<Type>(Type::floating(bits));
    }
    else if (identifier.value.starts_with('i') && identifier.value.length() > 1 &&
        std::ranges::all_of(identifier.value.substr(1), [](const unsigned char c) { return std::isdigit(c); }))
    {
        const auto bits = std::stol(identifier.value.substr(1));
        baseType = std::make_unique<Type>(Type::integer(bits, true));
    }
    else if (identifier.value.starts_with('u') && identifier.value.length() > 1 &&
        std::ranges::all_of(identifier.value.substr(1), [](const unsigned char c) { return std::isdigit(c); }))
    {
        const auto bits = std::stol(identifier.value.substr(1));
        baseType = std::make_unique<Type>(Type::integer(bits, false));
    }
    else if (identifier.value == "void" || identifier.type == TokenType::VOID)
    {
        baseType = std::make_unique<Type>(Type::voided());
    }
    else if (identifier.value == "auto" || identifier.type == TokenType::AUTO)
    {
        baseType = std::make_unique<Type>(Type::auto_type());
    }
    else
    {
        // Accept any identifier as a potential custom type
        // The Binder will validate that the type actually exists
        baseType = std::make_unique<Type>(Type::struct_type(identifier.value));
    }

    // Parse generic arguments: Array<i32>, Map<string, i32>
    if (baseType->kind == TypeKind::STRUCT && match(TokenType::LESS))
    {
        std::vector<Type> genericArgs;
        do
        {
            auto argType = parse_type();
            genericArgs.push_back(std::move(*argType));
        }
        while (match(TokenType::COMMA));
        expect("Esperado '>' após argumentos genéricos", TokenType::GREATER);
        baseType->genericArgs = std::move(genericArgs);
    }

    while (match(TokenType::STAR))
    {
        baseType = std::make_unique<Type>(Type::pointer(std::move(*baseType)));
    }

    if (match(TokenType::LBRACKET))
    {
        expect("Expected ']' after '['", TokenType::RBRACKET);
        return std::make_unique<Type>(Type::array(std::move(*baseType)));
    }

    return baseType;
}

std::vector<VisibilityModifier> Parser::parse_modifiers()
{
    std::vector<VisibilityModifier> modifiers;
    while (true)
    {
        if (match(TokenType::PUBLIC))
        {
            modifiers.push_back(VisibilityModifier::PUBLIC);
        }
        else if (match(TokenType::PRIVATE))
        {
            modifiers.push_back(VisibilityModifier::PRIVATE);
        }
        else if (match(TokenType::STATIC))
        {
            modifiers.push_back(VisibilityModifier::STATIC);
        }
        else
        {
            break;
        }
    }
    return modifiers;
}

std::unique_ptr<StructMethodDeclaration> Parser::parse_method(const bool allowBody)
{
    auto method = std::make_unique<StructMethodDeclaration>();

    // Parse modifiers: public, private, static
    method->modifiers = parse_modifiers();

    // Parse return type
    method->returnType = parse_type();

    // Parse method name
    const Token& methodNameToken = expect("Esperado nome do método", TokenType::IDENTIFIER);
    method->name = makeSourceIdentifier(methodNameToken);

    // Parse generic parameters: method<T>()
    if (match(TokenType::LESS))
    {
        do
        {
            const Token& paramName = expect("Esperado nome do parâmetro genérico", TokenType::IDENTIFIER);
            method->genericParams.add(GenericParam(makeSourceIdentifier(paramName)));
        }
        while (match(TokenType::COMMA));
        expect("Esperado '>' após parâmetros genéricos", TokenType::GREATER);
    }

    // Parse parameters
    expect("Esperado '('", TokenType::LPAREN);
    if (!check(TokenType::RPAREN))
    {
        do
        {
            if (match(TokenType::DOT_DOT_DOT))
            {
                method->isVariadic = true;
                break; // variadic marker comes after parameters
            }
            auto paramType = parse_type();
            const Token& paramNameToken = expect("Esperado nome do parâmetro", TokenType::IDENTIFIER);
            bool isMutable = match(TokenType::MUT);
            method->parameters.emplace_back(std::move(paramType), makeSourceIdentifier(paramNameToken), isMutable);
        }
        while (match(TokenType::COMMA));
    }
    expect("Esperado ')'", TokenType::RPAREN);

    if (!allowBody)
    {
        // Interface method - just a semicolon
        expect("Esperado ';' após assinatura do método", TokenType::SEMICOLON);
        return method;
    }

    // Add parameters to scope before parsing body
    pushScope();
    for (const auto& param : method->parameters)
    {
        currentScope->define_variable(param.name.token_name, *param.type);
    }

    // Parse body: { ... } or => expr;
    if (match(TokenType::ARROW))
    {
        // Expression body: => expr;
        method->expression = parse_expression();
        expect("Esperado ';' após expressão", TokenType::SEMICOLON);

        // Check if expression is variadic forwarding: => funcCall(args, ...)
        if (auto* call = dynamic_cast<FunctionCall*>(method->expression.get()))
        {
            if (call->hasVariadicForward && method->isVariadic)
            {
                method->variadicForwardTarget = call->name.token_name;
            }
        }
    }
    else if (check(TokenType::LBRACE))
    {
        // Block body: { ... }
        method->body = parse_block();

        // Check if body is single return with variadic forwarding: { return funcCall(args, ...); }
        if (method->body && method->body->statements.size() == 1)
        {
            if (auto* retStmt = dynamic_cast<ReturnStatement*>(method->body->statements[0].get()))
            {
                if (retStmt->value)
                {
                    if (auto* call = dynamic_cast<FunctionCall*>(retStmt->value.get()))
                    {
                        if (call->hasVariadicForward && method->isVariadic)
                        {
                            method->variadicForwardTarget = call->name.token_name;
                        }
                    }
                }
            }
        }
    }
    else
    {
        // Abstract method in struct (just declaration)
        expect("Esperado ';' após declaração do método", TokenType::SEMICOLON);
    }

    popScope();
    return method;
}

std::unique_ptr<InterfaceDeclaration> Parser::parse_interface()
{
    std::vector<AttributeUsageDeclaration> attributes;
    if (match(TokenType::LBRACKET))
    {
        attributes = this->parse_attributes();
    }

    expect("Esperado 'interface'", TokenType::INTERFACE);
    const Token& nameToken = expect("Esperado nome da interface", TokenType::IDENTIFIER);

    auto iface = std::make_unique<InterfaceDeclaration>();
    iface->name = makeSourceIdentifier(nameToken);

    // Parse generic parameters: interface IComparable<T> { ... }
    if (match(TokenType::LESS))
    {
        do
        {
            const Token& paramName = expect("Esperado nome do parâmetro genérico", TokenType::IDENTIFIER);
            iface->genericParams.add(GenericParam(makeSourceIdentifier(paramName)));
        }
        while (match(TokenType::COMMA));
        expect("Esperado '>' após parâmetros genéricos", TokenType::GREATER);
    }

    // Register generic param names as types within interface scope
    if (!iface->genericParams.empty())
    {
        for (const auto& param : iface->genericParams.params)
        {
            currentScope->define_struct(param.name.token_name, Type::struct_type(param.name.token_name));
        }
    }

    expect("Esperado '{'", TokenType::LBRACE);

    // Parse method signatures (no body)
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        iface->methods.push_back(parse_method(false));
    }

    expect("Esperado '}'", TokenType::RBRACE);

    return iface;
}

std::unique_ptr<StructDeclaration> Parser::parse_struct()
{
    std::vector<AttributeUsageDeclaration> attributes = this->parse_attributes();
    expect("Esperado 'struct'", TokenType::STRUCT);
    SourceIdentifier name = match(TokenType::IDENTIFIER)
                                ? makeSourceIdentifier(previous())
                                : SourceIdentifier(Type::generate_struct_name());

    // Parse generic parameters: struct Array<T, U> { ... }
    GenericParams genericParams;
    if (match(TokenType::LESS))
    {
        do
        {
            const Token& paramName = expect("Esperado nome do parâmetro genérico", TokenType::IDENTIFIER);
            genericParams.add(GenericParam(makeSourceIdentifier(paramName)));
        }
        while (match(TokenType::COMMA));
        expect("Esperado '>' após parâmetros genéricos", TokenType::GREATER);
    }

    // Parse base type or implements: struct Size : i32; or struct Foo : IBar { ... }
    std::vector<std::string> implements;
    std::unique_ptr<Type> baseType;
    if (match(TokenType::COLON))
    {
        // Check if it's a primitive type (transparent type inheritance)
        if (isType() && !check(TokenType::INTERFACE))
        {
            const size_t saved = current;
            auto potentialType = parse_type();

            // If it's a primitive type, use as baseType
            if (potentialType->kind != TypeKind::STRUCT)
            {
                baseType = std::move(potentialType);
            }
            else
            {
                // It's a struct/interface, treat as implements
                current = saved;
                do
                {
                    match(TokenType::INTERFACE);
                    implements.push_back(expect("Esperado nome do tipo base", TokenType::IDENTIFIER).value);
                }
                while (match(TokenType::COMMA));
            }
        }
        else
        {
            do
            {
                match(TokenType::INTERFACE);
                implements.push_back(expect("Esperado nome do tipo base", TokenType::IDENTIFIER).value);
            }
            while (match(TokenType::COMMA));
        }
    }

    currentScope->define_struct(name.token_name, Type::struct_type(name.token_name));

    // Register generic param names as types within struct scope for field parsing
    if (!genericParams.empty())
    {
        for (const auto& param : genericParams.params)
        {
            currentScope->define_struct(param.name.token_name, Type::struct_type(param.name.token_name));
        }
    }

    std::vector<StructField> fields;
    std::vector<StructProperty> structDecl_properties;
    std::vector<std::unique_ptr<StructMethodDeclaration>> methods;
    if (!match(TokenType::LBRACE))
    {
        expect("expected semi colon for no-body struct", TokenType::SEMICOLON);
    }
    else
    {
        while (!check(TokenType::RBRACE) && !isAtEnd())
        {
            // Check for modifiers first (indicates a method)
            if (check(TokenType::PUBLIC) || check(TokenType::PRIVATE) || check(TokenType::STATIC))
            {
                methods.push_back(parse_method(true));
                continue;
            }

            // Save position to distinguish field from method
            const size_t saved = current;

            const auto isMutable = match(TokenType::MUT);
            auto fieldType = parse_type();

            // Check for constructor: StructName(args) - type matches struct name followed by (
            if (fieldType->kind == TypeKind::STRUCT &&
                fieldType->structName == name.token_name &&
                check(TokenType::LPAREN))
            {
                // This is a constructor!
                auto ctor = std::make_unique<StructMethodDeclaration>();
                ctor->returnType = std::make_unique<Type>(Type::voided()); // void return type placeholder
                ctor->name = SourceIdentifier(fieldType->structName, fieldType->location);
                ctor->isConstructorMethod = true;

                // Parse parameters
                expect("Esperado '('", TokenType::LPAREN);
                if (!check(TokenType::RPAREN))
                {
                    do
                    {
                        auto paramType = parse_type();
                        const Token& paramNameToken = expect("Esperado nome do parâmetro", TokenType::IDENTIFIER);
                        bool paramMutable = match(TokenType::MUT);
                        ctor->parameters.emplace_back(std::move(paramType), makeSourceIdentifier(paramNameToken),
                                                      paramMutable);
                    }
                    while (match(TokenType::COMMA));
                }
                expect("Esperado ')'", TokenType::RPAREN);

                // Parse body
                if (check(TokenType::LBRACE))
                {
                    ctor->body = parse_block();
                }
                else
                {
                    expect("Esperado '{' para corpo do constructor", TokenType::LBRACE);
                }

                methods.push_back(std::move(ctor));
                continue;
            }

            if (!check(TokenType::IDENTIFIER))
            {
                current = saved;
                methods.push_back(parse_method(true));
                continue;
            }

            const Token& memberNameToken = expect("Esperado nome", TokenType::IDENTIFIER);
            SourceIdentifier memberName = makeSourceIdentifier(memberNameToken);

            // Check if this is a method (has parenthesis), property (has brace), or field (has semicolon)
            if (check(TokenType::LPAREN) || check(TokenType::LESS))
            {
                // It's a method - backtrack and parse as method
                current = saved;
                methods.push_back(parse_method(true));
            }
            else if (check(TokenType::LBRACE))
            {
                // It's a property: T name { get; set; }
                auto prop = parse_property(std::move(fieldType), std::move(memberName));
                structDecl_properties.push_back(std::move(prop));
            }
            else
            {
                // It's a field
                expect("Esperado ';' após campo", TokenType::SEMICOLON);
                fields.emplace_back(std::move(fieldType), std::move(memberName));
            }
        }

        expect("Esperado '}'", TokenType::RBRACE);
    }

    // Auto-properties generate fields with the same name
    // The property metadata controls access (get/set permissions)
    for (const auto& prop : structDecl_properties)
    {
        if (prop.isAutoProperty())
        {
            fields.emplace_back(std::make_unique<Type>(*prop.type), prop.name);
        }
    }

    auto structDecl = std::make_unique<StructDeclaration>(std::move(name), std::move(genericParams), std::move(fields));
    structDecl->properties = std::move(structDecl_properties);
    structDecl->methods = std::move(methods);
    structDecl->implements = std::move(implements);
    structDecl->baseType = std::move(baseType);
    structDecl->attributes = std::move(attributes);
    return structDecl;
}

// Parse property: T name { get; set; } or T name { get { ... } set { ... } }
StructProperty Parser::parse_property(std::unique_ptr<Type> type, SourceIdentifier name)
{
    StructProperty prop(std::move(type), std::move(name));

    expect("Esperado '{' para property", TokenType::LBRACE);

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        // Check for 'get' or 'set'
        if (peek().value == "get")
        {
            advance(); // consume 'get'
            prop.hasGetter = true;

            if (match(TokenType::SEMICOLON))
            {
                // Auto-implemented getter: get;
                // getterBody stays null
            }
            else if (match(TokenType::ARROW))
            {
                // Expression body: get => expr;
                prop.getterExpr = parse_expression();
                expect("Esperado ';' após expressão do getter", TokenType::SEMICOLON);
            }
            else if (check(TokenType::LBRACE))
            {
                // Block body: get { ... }
                prop.getterBody = parse_block();
            }
            else
            {
                throw std::runtime_error("Esperado ';', '=>' ou '{' após 'get'");
            }
        }
        else if (peek().value == "set")
        {
            advance(); // consume 'set'
            prop.hasSetter = true;

            if (match(TokenType::SEMICOLON))
            {
                // Auto-implemented setter: set;
                // setterBody stays null
            }
            else if (match(TokenType::ARROW))
            {
                // Expression body: set => expr;
                prop.setterExpr = parse_expression();
                expect("Esperado ';' após expressão do setter", TokenType::SEMICOLON);
            }
            else if (check(TokenType::LBRACE))
            {
                // Block body: set { ... }
                prop.setterBody = parse_block();
            }
            else
            {
                throw std::runtime_error("Esperado ';', '=>' ou '{' após 'set'");
            }
        }
        else
        {
            throw std::runtime_error("Esperado 'get' ou 'set' em property, encontrado: " + peek().value);
        }
    }

    expect("Esperado '}' após property", TokenType::RBRACE);

    if (!prop.hasGetter && !prop.hasSetter)
    {
        throw std::runtime_error("Property deve ter pelo menos 'get' ou 'set'");
    }

    return prop;
}

std::vector<AttributeUsageDeclaration> Parser::parse_attributes()
{
    std::vector<AttributeUsageDeclaration> attributes;
    while (check(TokenType::LBRACKET))
    {
        expect("Esperado '[' no uso de Atributos", TokenType::LBRACKET);
        const Token& identifier = expect("Esperado nome do atributo", TokenType::IDENTIFIER);
        attributes.emplace_back(makeSourceIdentifier(identifier));
        expect("Esperado ']' no uso de Atributos", TokenType::RBRACKET);
    }

    return attributes;
}

std::unique_ptr<Program> Parser::parse(const std::string& program_name)
{
    auto program = std::make_unique<Program>(program_name);

    while (!isAtEnd())
    {
        try
        {
            if (check(TokenType::IMPORT))
            {
                program->imports.push_back(parse_import());
            }
            else if (check(TokenType::EXTERN))
            {
                parse_extern(program.get());
            }
            else if (check(TokenType::NAMESPACE))
            {
                const size_t saved = current;
                advance();

                if (check(TokenType::IDENTIFIER))
                {
                    const auto qualified_name = parse_qualified_name();

                    if (match(TokenType::SEMICOLON))
                    {
                        program->fileNamespace = qualified_name.toString();
                        continue;
                    }
                    current = saved;
                }
                else
                {
                    current = saved;
                }
                program->namespaces.push_back(parse_namespace());
            }
            else if (check(TokenType::ENUM))
            {
                program->enums.push_back(parse_enum());
            }
            else if (check(TokenType::INTERFACE))
            {
                program->interfaces.push_back(parse_interface());
            }
            else if (check(TokenType::IMPL))
            {
                program->impls.push_back(parse_impl());
            }
            else if (check(TokenType::LBRACKET) || check(TokenType::STRUCT))
            {
                auto structDecl = parse_struct();

                // struct { ... } func() { ... }
                if (!check(TokenType::IDENTIFIER) || isType())
                {
                    program->structs.push_back(std::move(structDecl));
                }
                else
                {
                    auto returnType = std::make_unique<Type>(Type::struct_type(structDecl->name.token_name));
                    program->structs.push_back(std::move(structDecl));
                    program->functions.push_back(parse_function_with_type(std::move(returnType)));
                }
            }
            else
            {
                program->functions.push_back(parse_function());
            }
        }
        catch (const CompileError&)
        {
            synchronize();
        }
    }

    return program;
}

std::unique_ptr<FunctionDeclaration> Parser::parse_function()
{
    auto returnType = this->parse_type();
    return parse_function_with_type(std::move(returnType));
}

std::unique_ptr<FunctionDeclaration> Parser::parse_function_with_type(std::unique_ptr<Type> returnType)
{
    const Token& nameToken = expect("Esperado nome da função", TokenType::IDENTIFIER);

    pushScope();

    auto params = parse_parameters();

    for (const auto& param : params)
    {
        currentScope->define_variable(param.name.token_name, *param.type);
    }

    auto body = parse_block();

    popScope();

    return std::make_unique<FunctionDeclaration>(std::move(returnType), makeSourceIdentifier(nameToken), params,
                                                 std::move(body));
}

std::vector<Parameter> Parser::parse_parameters()
{
    auto parameters = std::vector<Parameter>();
    expect("Esperado '('", TokenType::LPAREN);

    if (!check(TokenType::RPAREN))
    {
        do
        {
            auto type = this->parse_type();
            const Token& paramNameToken = expect("Esperado nome do parâmetro", TokenType::IDENTIFIER);
            parameters.emplace_back(std::move(type), makeSourceIdentifier(paramNameToken), match(TokenType::MUT));
        }
        while (match(TokenType::COMMA));
    }

    expect("Esperado ')'", TokenType::RPAREN);
    return parameters;
}

std::unique_ptr<Block> Parser::parse_block()
{
    expect("Esperado '{'", TokenType::LBRACE);

    pushScope();

    auto block = std::make_unique<Block>();
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        block->statements.push_back(parse_statement());
    }

    popScope();

    expect("Esperado '}'", TokenType::RBRACE);
    return block;
}

std::unique_ptr<Statement> Parser::parse_statement()
{
    if (match(TokenType::RETURN))
    {
        std::unique_ptr<Expression> value = nullptr;
        if (!check(TokenType::SEMICOLON))
        {
            value = parse_expression();
        }
        expect("Esperado ';' após return", TokenType::SEMICOLON);
        return std::make_unique<ReturnStatement>(std::move(value));
    }

    if (check(TokenType::IF))
    {
        return parse_if_statement();
    }

    if (check(TokenType::FOR))
    {
        return parse_for_statement();
    }

    if (check(TokenType::WHILE))
    {
        return parse_while_statement();
    }

    if (check(TokenType::DO))
    {
        return parse_do_while_statement();
    }

    if (check(TokenType::SWITCH))
    {
        // Look ahead to distinguish between switch statement and switch expression
        // Switch statement: switch (expr) { case ... }
        // Switch expression: switch expr { Variant -> ... }
        const size_t saved = current;
        advance(); // consume 'switch'
        if (check(TokenType::LPAREN))
        {
            // It's a switch statement: switch (expr)
            current = saved;
            return parse_switch_statement();
        }
        // It's a switch expression used as statement
        current = saved;
        // Fall through to expression statement handling
    }

    if (match(TokenType::BREAK))
    {
        expect("Esperado ';' após break", TokenType::SEMICOLON);
        return std::make_unique<BreakStatement>();
    }

    if (match(TokenType::CONTINUE))
    {
        expect("Esperado ';' após continue", TokenType::SEMICOLON);
        return std::make_unique<ContinueStatement>();
    }

    // Bare block: { ... }
    if (check(TokenType::LBRACE))
    {
        return parse_block();
    }

    // Expression statement
    auto expr = parse_expression();
    expect("Esperado ';'", TokenType::SEMICOLON);
    return std::make_unique<ExpressionStatement>(std::move(expr));
}

std::unique_ptr<IfStatement> Parser::parse_if_statement()
{
    expect("Esperado 'if'", TokenType::IF);
    expect("Esperado '(' após if", TokenType::LPAREN);
    auto condition = parse_expression();
    expect("Esperado ')' após condição", TokenType::RPAREN);

    auto thenBranch = parse_block();

    std::unique_ptr<Block> elseBranch = nullptr;
    if (match(TokenType::ELSE))
    {
        if (check(TokenType::IF))
        {
            // else if - wrap in a block
            elseBranch = std::make_unique<Block>();
            elseBranch->statements.push_back(parse_if_statement());
        }
        else
        {
            elseBranch = parse_block();
        }
    }

    auto stmt = std::make_unique<IfStatement>();
    stmt->condition = std::move(condition);
    stmt->thenBranch = std::move(thenBranch);
    stmt->elseBranch = std::move(elseBranch);
    return stmt;
}

std::unique_ptr<ForStatement> Parser::parse_for_statement()
{
    expect("Esperado 'for'", TokenType::FOR);
    expect("Esperado '(' após for", TokenType::LPAREN);

    pushScope();

    std::unique_ptr<Expression> initializer = nullptr;
    if (!check(TokenType::SEMICOLON))
    {
        initializer = parse_expression();
    }
    expect("Esperado ';' após inicializador", TokenType::SEMICOLON);

    std::unique_ptr<Expression> condition = nullptr;
    if (!check(TokenType::SEMICOLON))
    {
        condition = parse_expression();
    }
    expect("Esperado ';' após condição", TokenType::SEMICOLON);

    std::unique_ptr<Expression> postfix = nullptr;
    if (!check(TokenType::RPAREN))
    {
        postfix = parse_expression();
    }
    expect("Esperado ')' após incremento", TokenType::RPAREN);

    auto body = parse_block();

    popScope();

    auto stmt = std::make_unique<ForStatement>();
    stmt->initializer = std::move(initializer);
    stmt->condition = std::move(condition);
    stmt->postfix = std::move(postfix);
    stmt->body = std::move(body);
    return stmt;
}

std::unique_ptr<WhileStatement> Parser::parse_while_statement()
{
    expect("Esperado 'while'", TokenType::WHILE);
    expect("Esperado '(' após while", TokenType::LPAREN);
    auto condition = parse_expression();
    expect("Esperado ')' após condição", TokenType::RPAREN);

    auto body = parse_block();

    auto stmt = std::make_unique<WhileStatement>();
    stmt->condition = std::move(condition);
    stmt->body = std::move(body);
    return stmt;
}

std::unique_ptr<DoWhileStatement> Parser::parse_do_while_statement()
{
    expect("Esperado 'do'", TokenType::DO);

    auto body = parse_block();

    expect("Esperado 'while' após corpo do do-while", TokenType::WHILE);
    expect("Esperado '(' após while", TokenType::LPAREN);
    auto condition = parse_expression();
    expect("Esperado ')' após condição", TokenType::RPAREN);
    expect("Esperado ';' após do-while", TokenType::SEMICOLON);

    auto stmt = std::make_unique<DoWhileStatement>();
    stmt->body = std::move(body);
    stmt->condition = std::move(condition);
    return stmt;
}

std::unique_ptr<SwitchStatement> Parser::parse_switch_statement()
{
    expect("Esperado 'switch'", TokenType::SWITCH);
    expect("Esperado '(' após switch", TokenType::LPAREN);
    auto value = parse_expression();
    expect("Esperado ')' após expressão", TokenType::RPAREN);
    expect("Esperado '{'", TokenType::LBRACE);

    std::vector<std::unique_ptr<SwitchCaseStatement>> cases;

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        auto caseStmt = std::make_unique<SwitchCaseStatement>();

        if (match(TokenType::CASE))
        {
            caseStmt->expression = parse_expression();
            expect("Esperado ':' após case", TokenType::COLON);
        }
        else if (match(TokenType::DEFAULT))
        {
            caseStmt->expression = nullptr; // default case
            expect("Esperado ':' após default", TokenType::COLON);
        }
        else
        {
            const auto& tok = peek();
            PARSER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "esperado 'case' ou 'default'",
                         SourceLocation(tok.position.fileId, tok.position.line, tok.position.column,
                             tok.value.length()));
        }

        // Parse case body statements until next case/default/rbrace
        auto caseBody = std::make_unique<Block>();
        while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) &&
            !check(TokenType::RBRACE) && !isAtEnd())
        {
            caseBody->statements.push_back(parse_statement());
        }
        caseStmt->body = std::move(caseBody);

        cases.push_back(std::move(caseStmt));
    }

    expect("Esperado '}'", TokenType::RBRACE);

    auto stmt = std::make_unique<SwitchStatement>();
    stmt->value = std::move(value);
    stmt->cases = std::move(cases);
    return stmt;
}

std::unique_ptr<Expression> Parser::parse_expression()
{
    return parse_or();
}

std::unique_ptr<Expression> Parser::parse_or()
{
    auto left = parse_and();

    while (match(TokenType::OR_OR))
    {
        TokenType op = previous().type;
        auto right = parse_and();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_and()
{
    auto left = parse_equality();

    while (match(TokenType::AND_AND))
    {
        TokenType op = previous().type;
        auto right = parse_equality();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_equality()
{
    auto left = parse_comparison();

    while (match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL))
    {
        TokenType op = previous().type;
        auto right = parse_comparison();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_comparison()
{
    auto left = parse_term();

    while (match(TokenType::LESS) || match(TokenType::LESS_EQUAL) ||
        match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL))
    {
        TokenType op = previous().type;
        auto right = parse_term();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_term()
{
    auto left = parse_factor();

    while (match(TokenType::PLUS) || match(TokenType::MINUS))
    {
        TokenType op = previous().type;
        auto right = parse_factor();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_factor()
{
    auto left = parse_unary();

    while (match(TokenType::STAR) || match(TokenType::SLASH) || match(TokenType::PERCENT))
    {
        TokenType op = previous().type;
        auto right = parse_unary();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_unary()
{
    if (match(TokenType::NEW))
    {
        auto expr = parse_postfix();
        // new [1, 2, 3] or new i32[1, 2, 3] — heap-allocated array literal
        if (auto* arrayLit = dynamic_cast<ArrayLiteral*>(expr.get()))
        {
            arrayLit->isHeap = true;
            return expr;
        }
        // The postfix expression should be a FunctionCall (constructor call)
        if (auto* funcCall = dynamic_cast<FunctionCall*>(expr.get()))
        {
            // Release the unique_ptr and re-wrap as FunctionCall unique_ptr
            expr.release();
            auto call = std::unique_ptr<FunctionCall>(funcCall);
            return std::make_unique<NewExpression>(std::move(call));
        }
        throw std::runtime_error("Expected constructor call or array literal after 'new'");
    }

    if (match(TokenType::BANG) || match(TokenType::MINUS) ||
        match(TokenType::AMPERSAND) || match(TokenType::STAR))
    {
        TokenType op = previous().type;
        auto operand = parse_unary();
        return std::make_unique<UnaryExpression>(op, std::move(operand));
    }

    return parse_postfix();
}

std::unique_ptr<Expression> Parser::parse_postfix()
{
    auto expr = parse_primary();

    while (true)
    {
        if (match(TokenType::DOT))
        {
            // Accept identifier or keywords as field/method names after '.'
            SourceIdentifier memberName;
            if (check(TokenType::IDENTIFIER))
            {
                memberName = makeSourceIdentifier(advance());
            }
            else if (check(TokenType::BREAK) || check(TokenType::CONTINUE) || check(TokenType::RETURN) ||
                check(TokenType::IF) || check(TokenType::ELSE) || check(TokenType::FOR) ||
                check(TokenType::WHILE) || check(TokenType::DO) || check(TokenType::SWITCH) ||
                check(TokenType::CASE) || check(TokenType::DEFAULT) || check(TokenType::STATIC) ||
                check(TokenType::PUBLIC) || check(TokenType::PRIVATE))
            {
                memberName = makeSourceIdentifier(advance());
            }
            else
            {
                throw std::runtime_error("Esperado nome do campo após '.'");
            }

            // Check if this is a method call: object.method(args)
            if (match(TokenType::LPAREN))
            {
                std::vector<std::unique_ptr<Expression>> args;
                bool hasVariadicForward = false;
                if (!check(TokenType::RPAREN))
                {
                    do
                    {
                        auto arg = parse_expression();
                        if (dynamic_cast<VariadicForward*>(arg.get()))
                        {
                            hasVariadicForward = true;
                        }
                        else
                        {
                            args.push_back(std::move(arg));
                        }
                    }
                    while (match(TokenType::COMMA));
                }
                expect("Esperado ')' após argumentos", TokenType::RPAREN);
                auto call = std::make_unique<FunctionCall>(std::move(memberName), std::move(args), std::move(expr));
                call->hasVariadicForward = hasVariadicForward;
                expr = std::move(call);
            }
            // Check if this is a field assignment (this.field = value)
            else if (match(TokenType::EQUAL))
            {
                auto value = parse_expression();
                return std::make_unique<FieldAssignment>(std::move(expr), std::move(memberName), std::move(value));
            }
            // Otherwise it's a field access
            else
            {
                expr = std::make_unique<FieldAccess>(std::move(expr), std::move(memberName));
            }
        }
        else if (match(TokenType::LBRACKET))
        {
            auto index = parse_expression();
            expect("Esperado ']' após índice", TokenType::RBRACKET);

            if (match(TokenType::EQUAL))
            {
                auto value = parse_expression();
                expr = std::make_unique<IndexAssignment>(std::move(expr), std::move(index), std::move(value));
            }
            else
            {
                expr = std::make_unique<IndexAccess>(std::move(expr), std::move(index));
            }
        }
        else
        {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parse_primary()
{
    // Switch expression: switch expr { Variant -> result, ... }
    if (check(TokenType::SWITCH))
    {
        return parse_switch_expression();
    }

    // Variadic forwarding: ... (forwards variadic arguments)
    if (match(TokenType::DOT_DOT_DOT))
    {
        return std::make_unique<VariadicForward>();
    }

    if (match(TokenType::INTEGER_LITERAL))
    {
        const auto value = previous().value;
        if (value.ends_with("i"))
        {
            // Signed integer suffix (e.g., 10i)
            return std::make_unique<IntegerLiteral>(value.substr(0, value.length() - 1), true);
        }
        if (value.ends_with("u"))
        {
            // Unsigned integer suffix (e.g., 10u)
            return std::make_unique<IntegerLiteral>(value.substr(0, value.length() - 1), false);
        }
        // Default: signed integer
        return std::make_unique<IntegerLiteral>(value, true);
    }

    if (match(TokenType::FLOAT_LITERAL))
    {
        return std::make_unique<FloatLiteral>(previous().value);
    }

    if (match(TokenType::STRING_LITERAL))
    {
        return std::make_unique<StringLiteral>(previous().value);
    }

    // Handle 'this' keyword as a simple identifier
    if (match(TokenType::THIS))
    {
        return std::make_unique<Identifier>(makeSourceIdentifier(previous()));
    }

    if (check(TokenType::AUTO) || check(TokenType::MUT) || check(TokenType::VOID) || check(TokenType::IDENTIFIER))
    {
        // Step 1: Consume modifiers (auto, mut)
        const auto isAuto = match(TokenType::AUTO);
        auto isMutable = match(TokenType::MUT);

        // Step 2: Consume the identifier and classify it
        const Token& firstToken = advance();
        const bool isPrimitive = isPrimitiveType(firstToken.value);
        const bool isDeclaredStruct = currentScope->has_struct_declared(firstToken.value);
        const bool isKnownType = isPrimitive || isDeclaredStruct;
        const auto existingVar = currentScope->lookup_variable(firstToken.value);

        // Step 3: Parse type modifiers — generics, pointers, arrays
        std::vector<Type> genericArgs;
        if ((isDeclaredStruct || (!isPrimitive && !existingVar)) && match(TokenType::LESS))
        {
            do
            {
                genericArgs.push_back(std::move(*parse_type()));
            }
            while (match(TokenType::COMMA));
            expect("Esperado '>' após argumentos genéricos", TokenType::GREATER);
        }

        int pointerDepth = 0;
        if (isKnownType)
        {
            while (match(TokenType::STAR))
            {
                pointerDepth++;
            }
        }

        bool isArray = false;
        if (isKnownType && match(TokenType::LBRACKET))
        {
            if (check(TokenType::RBRACKET))
            {
                // i32[] — array type suffix
                advance(); // consume ]
                isArray = true;
            }
            else
            {
                // i32[1, 2, 3] — typed array literal
                return parse_typed_array_literal(firstToken);
            }
        }

        if (!isMutable && match(TokenType::MUT))
        {
            isMutable = true;
        }

        // Step 4: Decide if this is a variable declaration or an expression
        const bool isTypeInference = isAuto && !isKnownType && !existingVar &&
            (check(TokenType::EQUAL) || check(TokenType::SEMICOLON));
        const bool isExplicitType = !existingVar && check(TokenType::IDENTIFIER);

        // Step 5a: Variable declaration — build type and emit VariableDeclaration/VariableInit
        if (isTypeInference || isExplicitType)
        {
            SourceIdentifier varName = isTypeInference
                                           ? makeSourceIdentifier(firstToken)
                                           : makeSourceIdentifier(advance());

            Type varType = isTypeInference
                               ? Type::auto_type()
                               : (isDeclaredStruct || !isPrimitive
                                      ? Type::struct_type(firstToken.value)
                                      : Type::fromToken(firstToken));

            if (!genericArgs.empty()) varType.genericArgs = std::move(genericArgs);
            for (int i = 0; i < pointerDepth; i++)
            {
                varType = Type::pointer(std::move(varType));
            }
            if (isArray) varType = Type::array(std::move(varType));

            currentScope->define_variable(varName.token_name, varType);

            if (match(TokenType::EQUAL))
            {
                return std::make_unique<VariableInit>(std::move(varType), std::move(varName),
                                                      parse_expression(), isMutable);
            }
            return std::make_unique<VariableDeclaration>(std::move(varType), std::move(varName), isMutable);
        }

        // Step 5b: Expression — qualified name, function call, assignment, or identifier
        std::string qualified_name = firstToken.value;
        while (match(TokenType::COLON_COLON))
        {
            const Token& part = expect("Esperado identifier após '::'", TokenType::IDENTIFIER);
            qualified_name += "::" + part.value;
        }

        const auto location = SourceLocation(
            firstToken.position.fileId,
            firstToken.position.line,
            firstToken.position.column,
            qualified_name.length()
        );
        SourceIdentifier nameIdentifier(qualified_name, location);

        // Function call: name(args) or name<T>(args)
        if (match(TokenType::LPAREN))
        {
            std::vector<std::unique_ptr<Expression>> args;
            bool hasVariadicForward = false;

            if (!check(TokenType::RPAREN))
            {
                do
                {
                    auto arg = parse_expression();
                    if (dynamic_cast<VariadicForward*>(arg.get()))
                    {
                        hasVariadicForward = true;
                    }
                    else
                    {
                        args.push_back(std::move(arg));
                    }
                }
                while (match(TokenType::COMMA));
            }

            expect("Esperado ')' após argumentos", TokenType::RPAREN);

            std::unique_ptr<FunctionCall> call;
            if (!genericArgs.empty())
            {
                call = std::make_unique<FunctionCall>(std::move(nameIdentifier), std::move(genericArgs),
                                                      std::move(args));
            }
            else
            {
                call = std::make_unique<FunctionCall>(std::move(nameIdentifier), std::move(args));
            }
            call->hasVariadicForward = hasVariadicForward;
            return call;
        }

        // Assignment: name = expr
        if (match(TokenType::EQUAL))
        {
            auto value = parse_expression();
            return std::make_unique<Assignment>(std::move(nameIdentifier), std::move(value));
        }

        // Simple identifier reference
        return std::make_unique<Identifier>(std::move(nameIdentifier));
    }

    // Array literal: [1, 2, 3]
    if (check(TokenType::LBRACKET))
    {
        return parse_array_literal();
    }

    if (check(TokenType::LBRACE))
    {
        return parse_brace_initializer();
    }

    if (match(TokenType::LPAREN))
    {
        auto expr = parse_expression();
        expect("Esperado ')' após expressão", TokenType::RPAREN);
        return expr;
    }

    const auto& tok = peek();
    PARSER_ERROR(DiagnosticCode::EXPECTED_EXPRESSION, "expressão inesperada",
                 SourceLocation(tok.position.fileId, tok.position.line, tok.position.column,
                     tok.value.empty() ? 1 : tok.value.length()));
}

std::unique_ptr<Expression> Parser::parse_brace_initializer()
{
    expect("Esperado '{'", TokenType::LBRACE);

    std::vector<InitializerElement> elements;

    if (!check(TokenType::RBRACE))
    {
        do
        {
            if (match(TokenType::DOT))
            {
                // Designated initializer: .field = value
                const Token& fieldNameToken = expect("Esperado nome do campo", TokenType::IDENTIFIER);
                expect("Esperado '=' após nome do campo", TokenType::EQUAL);
                auto value = parse_expression();
                elements.emplace_back(makeSourceIdentifier(fieldNameToken), std::move(value));
            }
            else
            {
                // Positional initializer: value
                auto value = parse_expression();
                elements.emplace_back(std::move(value));
            }
        }
        while (match(TokenType::COMMA));
    }

    expect("Esperado '}'", TokenType::RBRACE);

    return std::make_unique<BraceInitializer>(std::move(elements));
}

std::unique_ptr<Expression> Parser::parse_array_literal()
{
    expect("Esperado '['", TokenType::LBRACKET);

    std::vector<std::unique_ptr<Expression>> elements;
    if (!check(TokenType::RBRACKET))
    {
        do
        {
            elements.push_back(parse_expression());
        }
        while (match(TokenType::COMMA));
    }

    expect("Esperado ']'", TokenType::RBRACKET);

    return std::make_unique<ArrayLiteral>(std::move(elements));
}

std::unique_ptr<Expression> Parser::parse_typed_array_literal(const Token& typeToken)
{
    // '[' already consumed by caller, parse elements
    std::vector<std::unique_ptr<Expression>> elements;
    if (!check(TokenType::RBRACKET))
    {
        do
        {
            elements.push_back(parse_expression());
        }
        while (match(TokenType::COMMA));
    }

    expect("Esperado ']'", TokenType::RBRACKET);

    Type elemType = Type::fromToken(typeToken);
    if (elemType.kind == TypeKind::VOID && typeToken.value != "void")
    {
        // Not a primitive — treat as struct type
        elemType = Type::struct_type(typeToken.value);
    }

    return std::make_unique<ArrayLiteral>(std::move(elements), std::move(elemType));
}

std::unique_ptr<Expression> Parser::parse_switch_expression()
{
    expect("Esperado 'switch'", TokenType::SWITCH);

    // Parse the value expression (without parentheses, unlike switch statement)
    auto value = parse_expression();

    expect("Esperado '{'", TokenType::LBRACE);

    std::vector<SwitchArm> arms;

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        // Parse variant name
        const Token& variantToken = expect("Esperado nome da variante", TokenType::IDENTIFIER);
        SourceIdentifier variantName = makeSourceIdentifier(variantToken);

        // Check for optional binding (e.g., "Value val" or just "Empty")
        std::optional<SourceIdentifier> binding;
        if (check(TokenType::IDENTIFIER))
        {
            const Token& bindingToken = advance();
            binding = makeSourceIdentifier(bindingToken);
        }

        // Expect ->
        expect("Esperado '->' após padrão", TokenType::THIN_ARROW);

        // Parse result expression
        auto result = parse_expression();

        arms.emplace_back(std::move(variantName), std::move(binding), std::move(result));

        // Optional comma between arms
        if (!check(TokenType::RBRACE))
        {
            match(TokenType::COMMA);
        }
    }

    expect("Esperado '}'", TokenType::RBRACE);

    return std::make_unique<SwitchExpression>(std::move(value), std::move(arms));
}

std::unique_ptr<ExternFunctionDeclaration> Parser::parse_extern_function(const std::string& abi)
{
    std::unique_ptr<Type> returnType = parse_type();
    const Token& nameToken = expect("Esperado nome", TokenType::IDENTIFIER);

    expect("Esperado '('", TokenType::LPAREN);

    std::vector<Parameter> parameters;
    do
    {
        if (!check(TokenType::RPAREN) && !check(TokenType::DOT_DOT_DOT))
        {
            auto type = parse_type();
            const Token& paramNameToken = expect("Esperado nome do parâmetro", TokenType::IDENTIFIER);
            parameters.emplace_back(std::move(type), makeSourceIdentifier(paramNameToken), match(TokenType::MUT));
        }
    }
    while (match(TokenType::COMMA));


    const bool isVariadic = match(TokenType::DOT_DOT_DOT);

    expect("Esperado ')'", TokenType::RPAREN);
    expect("Esperado ';'", TokenType::SEMICOLON);

    auto decl = std::make_unique<ExternFunctionDeclaration>();
    decl->name = makeSourceIdentifier(nameToken);
    decl->returnType = std::move(returnType);
    decl->parameters = std::move(parameters);
    decl->isVariadic = isVariadic;
    decl->abi = abi;

    return decl;
}

void Parser::parse_extern(Program* program)
{
    expect("Esperado 'extern'", TokenType::EXTERN);

    std::string abi = "C";
    if (match(TokenType::STRING_LITERAL))
    {
        abi = previous().value;
    }

    if (match(TokenType::LBRACE))
    {
        while (!check(TokenType::RBRACE) && !isAtEnd())
        {
            program->externFunctions.push_back(parse_extern_function(abi));
        }
        expect("Esperado '}'", TokenType::RBRACE);
    }
    else
    {
        program->externFunctions.push_back(parse_extern_function(abi));
    }
}

std::unique_ptr<NamespaceDeclaration> Parser::parse_namespace()
{
    expect("Esperado 'namespace'", TokenType::NAMESPACE);
    const Token& nameToken = expect("Esperado nome do namespace", TokenType::IDENTIFIER);

    auto ns = std::make_unique<NamespaceDeclaration>(makeSourceIdentifier(nameToken));

    expect("Esperado '{'", TokenType::LBRACE);

    pushScope();

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        if (check(TokenType::NAMESPACE))
        {
            ns->namespaces.push_back(parse_namespace());
        }
        else if (check(TokenType::STRUCT))
        {
            auto structDecl = parse_struct();

            if (!check(TokenType::IDENTIFIER) || isType())
            {
                ns->structs.push_back(std::move(structDecl));
            }
            else
            {
                auto returnType = std::make_unique<Type>(Type::struct_type(structDecl->name.token_name));
                ns->structs.push_back(std::move(structDecl));
                ns->functions.push_back(parse_function_with_type(std::move(returnType)));
            }
        }
        else
        {
            ns->functions.push_back(parse_function());
        }
    }

    popScope();

    expect("Esperado '}'", TokenType::RBRACE);

    return ns;
}

std::unique_ptr<EnumDeclaration> Parser::parse_enum()
{
    expect("Esperado enum keyword", TokenType::ENUM);

    const Token& identifierToken = expect("Esperado identifier", TokenType::IDENTIFIER);

    // Parse generic params: enum Result<T, E> { ... }
    GenericParams genericParams;
    if (match(TokenType::LESS))
    {
        do
        {
            const Token& paramName = expect("Esperado nome do parâmetro genérico", TokenType::IDENTIFIER);
            genericParams.add(GenericParam(makeSourceIdentifier(paramName)));
        }
        while (match(TokenType::COMMA));
        expect("Esperado '>' após parâmetros genéricos", TokenType::GREATER);
    }

    // Register generic param names as types so they can be used in variant types
    if (!genericParams.empty())
    {
        for (const auto& param : genericParams.params)
        {
            currentScope->define_struct(param.name.token_name, Type::struct_type(param.name.token_name));
        }
    }

    if (check(TokenType::LBRACE))
    {
        expect("Esperado { para declaração de enum", TokenType::LBRACE);

        std::vector<EnumValueDeclaration> values;
        while (check(TokenType::IDENTIFIER))
        {
            const Token& enum_key = expect("Esperado identifier", TokenType::IDENTIFIER);
            std::vector<Type> associated_types;

            // Parse associated types: Ok(T) or Ok(T, U)
            if (match(TokenType::LPAREN) && !match(TokenType::RPAREN))
            {
                do
                {
                    associated_types.emplace_back(*parse_type());
                }
                while (match(TokenType::COMMA));

                expect("Esperado ) após tipos associados", TokenType::RPAREN);
            }

            values.emplace_back(makeSourceIdentifier(enum_key), associated_types);

            // Optional comma between variants
            if (!check(TokenType::RBRACE))
            {
                match(TokenType::COMMA);
            }
        }

        expect("Esperado } após declaração de enum", TokenType::RBRACE);
        return std::make_unique<EnumDeclaration>(makeSourceIdentifier(identifierToken), std::move(genericParams),
                                                 values);
    }
    return std::make_unique<EnumDeclaration>(makeSourceIdentifier(identifierToken), std::move(genericParams),
                                             std::vector<EnumValueDeclaration>{});
}

QualifiedName Parser::parse_qualified_name()
{
    QualifiedName qname;

    const Token& first = expect("Esperado identificador", TokenType::IDENTIFIER);
    qname.addPart(first.value);

    while (match(TokenType::COLON_COLON))
    {
        const Token& part = expect("Esperado identificador após '::'", TokenType::IDENTIFIER);
        qname.addPart(part.value);
    }

    return qname;
}

std::unique_ptr<ImportDeclaration> Parser::parse_import()
{
    expect("Esperado 'import'", TokenType::IMPORT);

    auto qname = parse_qualified_name();

    expect("Esperado ';' após import", TokenType::SEMICOLON);

    return std::make_unique<ImportDeclaration>(std::move(qname));
}

std::unique_ptr<ImplDeclaration> Parser::parse_impl()
{
    expect("Esperado 'impl'", TokenType::IMPL);

    auto impl = std::make_unique<ImplDeclaration>();

    // Parse the first type (could be the target type or interface name)
    auto firstType = parse_type();

    // Check for "impl Interface for Type" form
    if (check(TokenType::FOR))
    {
        advance(); // consume 'for'
        impl->interfaceName = firstType->kind == TypeKind::STRUCT
                                  ? firstType->structName
                                  : firstType->toHumanString();
        impl->targetType = parse_type();
    }
    else
    {
        // "impl Type { methods }" form
        impl->targetType = std::move(firstType);
    }

    expect("Esperado '{' após tipo no impl", TokenType::LBRACE);

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        impl->methods.push_back(parse_method(true));
    }

    expect("Esperado '}' no impl", TokenType::RBRACE);

    return impl;
}
