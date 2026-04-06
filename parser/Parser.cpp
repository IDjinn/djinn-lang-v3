//
// Created by Luke on 06/12/2025.
//

#include "parser.h"

#include <cctype>
#include <ranges>
#include <sstream>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/CommandLine.h>
#include <cassert>

#include "../utils/Logger.h"


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
    // Handle pending > from >> split
    if (type == TokenType::GREATER && pendingGreater > 0) return true;
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(const TokenType type)
{
    // Handle pending > from >> split
    if (type == TokenType::GREATER && pendingGreater > 0)
    {
        pendingGreater--;
        return true;
    }
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
    // Check pending > from a previous >> split
    if (type == TokenType::GREATER && pendingGreater > 0)
    {
        pendingGreater--;
        return previous(); // return the same token position (it was the >>)
    }

    if (check(type)) return advance();

    // Split >> into two > tokens when expecting > (for nested generics like Array<Array<i32>>)
    if (type == TokenType::GREATER && check(TokenType::GREATER_GREATER))
    {
        pendingGreater++; // leave one > pending for the next expect
        return advance(); // consume the >> token
    }

    const auto& token = previous();
    const uint32_t col = token.position.column + token.value.length();
    PARSER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, message+ ", recieved '" + tokenTypeToHumanString(peek().type) + "'",
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
    PARSER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, message+ ", recieved '" + tokenTypeToHumanString(peek().type) + "'",
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

    // Parse async modifier
    if (match(TokenType::ASYNC))
    {
        method->isAsync = true;
    }

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

    // Parse where clause: void sort<T>(array<T> arr) where T : IComparable { ... }
    while (check(TokenType::WHERE))
    {
        parse_where_clause(method->genericParams);
    }

    // Parse parameters
    expect("Esperado '('", TokenType::LPAREN);
    if (!check(TokenType::RPAREN))
    {
        do
        {
            if (match(TokenType::DOT_DOT_DOT))
            {
                const auto identifier = expect("Expected variadic args identifier name", TokenType::IDENTIFIER);
                method->variadic = std::make_unique<SourceIdentifier>(makeSourceIdentifier(identifier));
                break;
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
    }
    else if (check(TokenType::LBRACE))
    {
        // Block body: { ... }
        method->body = parse_block();
    }
    else
    {
        // Abstract method in struct (just declaration)
        expect("Esperado ';' após declaração do método", TokenType::SEMICOLON);
    }

    popScope();
    LOG_DEBUG("[parser] method declared: '%s' with %zu params%s",
              method->name.token_name.c_str(), method->parameters.size(),
              method->isConstructorMethod ? " (constructor)" : "");
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

    // Parse where clause: interface IContainer<T> where T : IEquatable { ... }
    while (check(TokenType::WHERE))
    {
        parse_where_clause(iface->genericParams);
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

    // Parse method signatures (no body) or operator signatures
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        if (match(TokenType::OPERATOR))
        {
            iface->methods.push_back(parse_operator(false));
        }
        else
        {
            iface->methods.push_back(parse_method(false));
        }
    }

    expect("Esperado '}'", TokenType::RBRACE);

    return iface;
}

std::unique_ptr<StructDeclaration> Parser::parse_struct()
{
    std::vector<AttributeUsageDeclaration> attributes = this->parse_attributes();
    if (check(TokenType::STRUCT))
        expect("Esperado 'struct'", TokenType::STRUCT);

    SourceIdentifier name = match(TokenType::IDENTIFIER)
                                ? makeSourceIdentifier(previous())
                                : SourceIdentifier(Type::generate_struct_name(),
                                                   SourceLocation(peek().position.fileId, peek().position.line,
                                                                  peek().position.column, 0));

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

    // Parse where clause: struct Array<T> where T : IEquatable { ... }
    while (check(TokenType::WHERE))
    {
        parse_where_clause(genericParams);
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

    // Also parse where clauses after implements: struct Foo<T> : IBar where T : IEquatable { ... }
    while (check(TokenType::WHERE))
    {
        parse_where_clause(genericParams);
    }

    currentScope->define_struct(name.token_name, Type::struct_type(name.token_name));
    LOG_DEBUG("[parser] struct declared: '%s'%s", name.token_name.c_str(),
              genericParams.empty() ? "" : " (generic)");

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
            // Parse attributes before methods: [force-inline] public void foo() { ... }
            auto methodAttributes = parse_attributes();

            // Check for operator declaration
            if (match(TokenType::OPERATOR))
            {
                auto op = parse_operator(true);
                op->attributes = std::move(methodAttributes);
                methods.push_back(std::move(op));
                continue;
            }

            // Check for modifiers — could be a method OR a const field
            if (check(TokenType::PUBLIC) || check(TokenType::PRIVATE) || check(TokenType::STATIC))
            {
                // Peek ahead: if modifiers are followed by CONST, it's a const field
                const size_t modSaved = current;
                auto fieldModifiers = parse_modifiers();
                if (check(TokenType::CONST))
                {
                    // It's a const field with access modifiers: public const i32 X = 10;
                    advance(); // consume CONST
                    auto constFieldType = parse_type();
                    const Token& constFieldNameToken = expect("expected field name", TokenType::IDENTIFIER);
                    SourceIdentifier constFieldName = makeSourceIdentifier(constFieldNameToken);
                    expect("expected '=' for const field initializer", TokenType::EQUAL);
                    auto constInitializer = parse_expression();
                    expect("expected ';' after const field", TokenType::SEMICOLON);
                    fields.emplace_back(std::move(constFieldType), std::move(constFieldName),
                                        true, std::move(constInitializer), std::move(fieldModifiers));
                    continue;
                }
                // Not a const field — backtrack and parse as method
                current = modSaved;
                auto m = parse_method(true);
                m->attributes = std::move(methodAttributes);
                methods.push_back(std::move(m));
                continue;
            }

            // Save position to distinguish field from method
            const size_t saved = current;

            auto isMutable = match(TokenType::MUT);
            auto isConstant = match(TokenType::CONST);
            isMutable |= match(TokenType::MUT);
            isConstant |= match(TokenType::CONST); // hacky for non ordered modifiers
            if (isMutable && isConstant)
            {
                PARSER_ERROR(DiagnosticCode::INVALID_MODIFIERS, "field cannot be mutable and constant at same time!",
                             SourceLocation(peek().position.fileId, peek().position.line, peek().position.column, 1));
                continue;
                // TODO: WHEN GENERATE THIS ERROR, WE NEED SKIP TOKENS STRUCT CLOSE OR VALID STRUCT PARSING POINT
            }

            auto fieldType = parse_type();

            // Check for constructor: StructName(args) - type matches struct name followed by (
            if (fieldType->kind == TypeKind::STRUCT &&
                fieldType->structName == name.token_name &&
                check(TokenType::LPAREN))
            {
                // This is a constructor!
                auto ctor = std::make_unique<StructMethodDeclaration>();
                ctor->returnType = std::make_unique<Type>(Type::voided()); // void return type placeholder
                ctor->name = SourceIdentifier(fieldType->structName, name.location);
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
            else if (isConstant && check(TokenType::EQUAL))
            {
                // Const field without modifiers: const i32 X = 10;
                advance(); // consume '='
                auto constInitializer = parse_expression();
                expect("expected ';' after const field", TokenType::SEMICOLON);
                fields.emplace_back(std::move(fieldType), std::move(memberName),
                                    true, std::move(constInitializer));
            }
            else
            {
                // It's a field
                if (isConstant)
                {
                    PARSER_ERROR(DiagnosticCode::INVALID_MODIFIERS,
                                 "const field must have an initializer (e.g., const i32 X = 10;)",
                                 SourceLocation(memberName.location.fileId, memberName.location.line,
                                     memberName.location.column, 1));
                }
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

    LOG_DEBUG("[parser] struct '%s' parsed: %zu fields, %zu methods, %zu properties",
              name.token_name.c_str(), fields.size(), methods.size(), structDecl_properties.size());

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
        std::string attrName = identifier.value;
        auto loc = makeSourceIdentifier(identifier);
        while (match(TokenType::MINUS))
        {
            const Token& next = expect("Esperado continuação do nome do atributo", TokenType::IDENTIFIER);
            attrName += "-" + next.value;
        }
        loc.token_name = attrName;

        std::vector<AttributeArg> args;
        if (match(TokenType::LPAREN))
        {
            if (!check(TokenType::RPAREN))
            {
                do
                {
                    AttributeArg arg;
                    arg.location = SourceLocation(peek().position, peek().value.length());

                    // Check for named argument: identifier = value
                    if (check(TokenType::IDENTIFIER) && current + 1 < tokens.size() &&
                        tokens[current + 1].type == TokenType::EQUAL)
                    {
                        arg.name = advance().value;
                        advance(); // consume '='
                    }

                    // Parse value
                    if (match(TokenType::INTEGER_LITERAL))
                    {
                        arg.value = std::stoll(previous().value);
                    }
                    else if (match(TokenType::FLOAT_LITERAL))
                    {
                        arg.value = std::stod(previous().value);
                    }
                    else if (match(TokenType::STRING_LITERAL))
                    {
                        arg.value = previous().value;
                    }
                    else if (match(TokenType::TRUE))
                    {
                        arg.value = true;
                    }
                    else if (match(TokenType::FALSE))
                    {
                        arg.value = false;
                    }
                    else
                    {
                        // Identifier as string value (e.g., AttributeTarget.Function)
                        std::string val = expect("Esperado valor do argumento do atributo", TokenType::IDENTIFIER).
                            value;
                        while (match(TokenType::DOT))
                        {
                            val += "." + expect("Esperado nome após '.'", TokenType::IDENTIFIER).value;
                        }
                        // Support bitwise OR for target flags: Target.A | Target.B
                        while (match(TokenType::PIPE))
                        {
                            val += " | ";
                            std::string next = expect("Esperado identificador", TokenType::IDENTIFIER).value;
                            while (match(TokenType::DOT))
                            {
                                next += "." + expect("Esperado nome após '.'", TokenType::IDENTIFIER).value;
                            }
                            val += next;
                        }
                        arg.value = val;
                    }

                    args.push_back(std::move(arg));
                }
                while (match(TokenType::COMMA));
            }
            expect("Esperado ')' nos argumentos do atributo", TokenType::RPAREN);
        }

        attributes.emplace_back(loc, std::move(args));
        expect("Esperado ']' no uso de Atributos", TokenType::RBRACKET);
    }

    return attributes;
}

std::unique_ptr<CompileTimeBlock> Parser::parse_compile_time_block(const std::string& programName)
{
    auto block = std::make_unique<CompileTimeBlock>();
    expect("Esperado 'if'", TokenType::IF);

    if (match(TokenType::CONST_EXPR))
        block->compileTimeKind = CompileTimeKind::ConstExpr;
    else if (match(TokenType::CONST_EVAL))
        block->compileTimeKind = CompileTimeKind::ConstEval;

    block->condition = parse_expression();
    expect("Esperado '{'", TokenType::LBRACE);

    block->thenProgram = std::make_unique<Program>(programName);
    parse_top_level_declarations(block->thenProgram.get());
    expect("Esperado '}'", TokenType::RBRACE);

    if (match(TokenType::ELSE))
    {
        if (check(TokenType::IF))
        {
            block->elseProgram = std::make_unique<Program>(programName);
            block->elseProgram->compileTimeBlocks.push_back(parse_compile_time_block(programName));
        }
        else
        {
            expect("Esperado '{'", TokenType::LBRACE);
            block->elseProgram = std::make_unique<Program>(programName);
            parse_top_level_declarations(block->elseProgram.get());
            expect("Esperado '}'", TokenType::RBRACE);
        }
    }

    return block;
}

void Parser::parse_top_level_declarations(Program* program)
{
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        if (check(TokenType::IMPORT))
            program->imports.push_back(parse_import());
        else if (check(TokenType::EXTERN))
            parse_extern(program);
        else if (check(TokenType::ENUM))
            program->enums.push_back(parse_enum());
        else if (check(TokenType::INTERFACE))
            program->interfaces.push_back(parse_interface());
        else if (check(TokenType::IMPL))
            program->impls.push_back(parse_impl());
        else if (check(TokenType::LBRACKET))
        {
            const size_t saved = current;
            auto attrs = parse_attributes();
            if (check(TokenType::CONST_EVAL) || check(TokenType::CONST_EXPR) || check(TokenType::CONST))
                parse_constexpr_declaration(program, std::move(attrs));
            else
            {
                current = saved;
                program->structs.push_back(parse_struct());
            }
        }
        else if (check(TokenType::STRUCT) || check(TokenType::LBRACE))
            program->structs.push_back(parse_struct());
        else if (check(TokenType::CONST_EVAL) || check(TokenType::CONST_EXPR) || check(TokenType::CONST))
            parse_constexpr_declaration(program, {});
        else if (check(TokenType::MACRO))
        {
            auto macroDecl = parse_macro();
            _macros[macroDecl->name.token_name] = macroDecl.get();
            program->macros.push_back(std::move(macroDecl));
        }
        else if (check(TokenType::IF))
            program->compileTimeBlocks.push_back(parse_compile_time_block(program->name));
        else if (check(TokenType::ASYNC))
        {
            advance();
            auto func = parse_function();
            func->isAsync = true;
            program->functions.push_back(std::move(func));
        }
        else
            program->functions.push_back(parse_function());
    }
}

void Parser::parse_constexpr_declaration(Program* program, std::vector<AttributeUsageDeclaration> attrs)
{
    bool isConstEval = check(TokenType::CONST_EVAL);
    bool isConstExpr = check(TokenType::CONST_EXPR);
    advance(); // consume consteval/constexpr/const

    bool isIntrinsic = false;
    for (const auto& attr : attrs)
    {
        if (attr.name.token_name == "intrinsic")
            isIntrinsic = true;
    }

    if (check(TokenType::STRUCT) || check(TokenType::LBRACE))
    {
        auto structDecl = parse_struct();
        structDecl->isConstExpr = true;
        structDecl->isIntrinsic = isIntrinsic;
        structDecl->attributes = std::move(attrs);
        program->structs.push_back(std::move(structDecl));
        return;
    }

    auto type = parse_type();
    const Token& nameToken = expect("Esperado nome", TokenType::IDENTIFIER);

    if ((isConstEval || isConstExpr) && check(TokenType::LPAREN))
    {
        pushScope();
        auto params = parse_parameters();
        for (const auto& param : params)
            currentScope->define_variable(param.name.token_name, *param.type);
        auto body = parse_block();
        popScope();
        auto func = std::make_unique<FunctionDeclaration>(std::move(type), makeSourceIdentifier(nameToken),
                                                          params, std::move(body));
        func->constEval = isConstEval;
        func->constExpr = isConstExpr;
        program->functions.push_back(std::move(func));
    }
    else if (isIntrinsic && check(TokenType::SEMICOLON))
    {
        advance(); // consume ';'
        auto decl = std::make_unique<ConstExprDeclaration>(*type, makeSourceIdentifier(nameToken), nullptr);
        decl->attributes = std::move(attrs);
        decl->isIntrinsic = true;
        program->constExprs.push_back(std::move(decl));
    }
    else
    {
        expect("Esperado '='", TokenType::EQUAL);
        auto value = parse_expression();
        expect("Esperado ';'", TokenType::SEMICOLON);
        auto decl = std::make_unique<ConstExprDeclaration>(*type, makeSourceIdentifier(nameToken), std::move(value));
        decl->attributes = std::move(attrs);
        program->constExprs.push_back(std::move(decl));
    }
}

std::unique_ptr<Program> Parser::parse(const std::string& program_name)
{
    LOG_DEBUG("[parser] === parsing program: '%s' ===", program_name.c_str());
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
                        LOG_DEBUG("[parser] namespace: '%s'", program->fileNamespace.c_str());
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
            else if (check(TokenType::LBRACKET))
            {
                const size_t saved = current;
                auto attrs = parse_attributes();
                if (check(TokenType::CONST_EVAL) || check(TokenType::CONST_EXPR) || check(TokenType::CONST))
                {
                    parse_constexpr_declaration(program.get(), std::move(attrs));
                }
                else
                {
                    current = saved;
                    auto structDecl = parse_struct();
                    auto structName = structDecl->name.token_name;
                    program->structs.push_back(std::move(structDecl));

                    if (structName.starts_with("__anon_struct_") && check(TokenType::IDENTIFIER))
                    {
                        auto returnType = std::make_unique<Type>(Type::struct_type(structName));
                        program->functions.push_back(parse_function_with_type(std::move(returnType)));
                    }
                }
            }
            else if (check(TokenType::STRUCT) || check(TokenType::LBRACE))
            {
                auto structDecl = parse_struct();
                auto structName = structDecl->name.token_name;
                program->structs.push_back(std::move(structDecl));

                if (structName.starts_with("__anon_struct_") && check(TokenType::IDENTIFIER))
                {
                    auto returnType = std::make_unique<Type>(Type::struct_type(structName));
                    program->functions.push_back(parse_function_with_type(std::move(returnType)));
                }
            }
            else if (check(TokenType::CONST_EVAL) || check(TokenType::CONST_EXPR) || check(TokenType::CONST))
            {
                parse_constexpr_declaration(program.get(), {});
            }
            else if (check(TokenType::IF))
            {
                program->compileTimeBlocks.push_back(parse_compile_time_block(program->name));
            }
            else if (check(TokenType::MACRO))
            {
                auto macroDecl = parse_macro();
                _macros[macroDecl->name.token_name] = macroDecl.get();
                program->macros.push_back(std::move(macroDecl));
            }
            else if (check(TokenType::ASYNC))
            {
                advance(); // consume 'async'
                auto func = parse_function();
                func->isAsync = true;
                program->functions.push_back(std::move(func));
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

    LOG_DEBUG("[parser] === done parsing '%s': %zu structs, %zu enums, %zu functions, %zu imports, ns='%s' ===",
              program_name.c_str(), program->structs.size(), program->enums.size(),
              program->functions.size(), program->imports.size(), program->fileNamespace.c_str());
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

    LOG_DEBUG("[parser] function declared: '%s' with %zu params", nameToken.value.c_str(), params.size());
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

    if (match(TokenType::SPAWN))
    {
        auto expr = parse_expression();
        expect("Esperado ';' após spawn", TokenType::SEMICOLON);
        return std::make_unique<SpawnStatement>(std::move(expr));
    }

    if (match(TokenType::YIELD))
    {
        expect("Esperado ';' após yield", TokenType::SEMICOLON);
        return std::make_unique<YieldStatement>();
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
    auto ifToken = expect("Esperado 'if'", TokenType::IF);
    auto ifStatementIndex = ifToken.position.index;

    auto kind = CompileTimeKind::None;
    if (match(TokenType::CONST_EXPR))
    {
        kind = CompileTimeKind::ConstExpr;
    }
    else if (match(TokenType::CONST_EVAL))
    {
        kind = CompileTimeKind::ConstEval;
    }
    else if (!check(TokenType::LPAREN))
    {
        kind = CompileTimeKind::ConstExpr;
    }

    if (kind == CompileTimeKind::None)
    {
        advance(); // consume '('
    }

    auto condition = parse_expression();

    if (kind == CompileTimeKind::None)
    {
        expect("Esperado ')' após condição", TokenType::RPAREN);
    }

    auto& lastCondToken = previous();
    auto ifStatementLocation = SourceLocation(ifToken.position,
                                              lastCondToken.position.index + lastCondToken.value.size() -
                                              ifStatementIndex);
    auto thenBranch = parse_block();

    std::unique_ptr<Block> elseBranch = nullptr;
    if (match(TokenType::ELSE))
    {
        if (check(TokenType::IF))
        {
            elseBranch = std::make_unique<Block>();
            elseBranch->statements.push_back(parse_if_statement());
        }
        else
        {
            elseBranch = parse_block();
        }
    }

    auto stmt = std::make_unique<IfStatement>();
    stmt->compileTimeKind = kind;
    stmt->location = ifStatementLocation;
    stmt->condition = std::move(condition);
    stmt->thenBranch = std::move(thenBranch);
    stmt->elseBranch = std::move(elseBranch);
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_for_statement()
{
    expect("Esperado 'for'", TokenType::FOR);
    expect("Esperado '(' após for", TokenType::LPAREN);

    // Detect range-for: for (type name in start..end)
    auto saved = current;
    bool isRangeFor = false;
    if (isType())
    {
        parse_type();
        if (check(TokenType::IDENTIFIER))
        {
            advance();
            if (check(TokenType::IN))
            {
                isRangeFor = true;
            }
        }
    }
    current = saved;

    // Detect anonymous range-for: for (start..end) or for ([start..end])
    // Must check before named range-for falls through to regular for
    saved = current;
    bool isAnonRange = false;
    bool anonHasBracket = false;
    if (!isRangeFor)
    {
        if (check(TokenType::LBRACKET))
        {
            advance();
            parse_expression();
            if (check(TokenType::DOT_DOT) || check(TokenType::DOT_DOT_EQUAL))
            {
                isAnonRange = true;
                anonHasBracket = true;
            }
        }
        else
        {
            parse_expression();
            if (check(TokenType::DOT_DOT) || check(TokenType::DOT_DOT_EQUAL))
                isAnonRange = true;
        }
        current = saved;
    }

    if (isRangeFor || isAnonRange)
    {
        pushScope();

        auto stmt = std::make_unique<RangeForStatement>();

        if (isRangeFor)
        {
            auto varType = parse_type();
            stmt->variableType = std::move(*varType);
            stmt->variableName = makeSourceIdentifier(advance());
            expect("Esperado 'in' após nome da variável", TokenType::IN);
        }
        else
        {
            static int anonRangeCounter = 0;
            auto loc = SourceLocation(peek().position, peek().value.length());
            stmt->variableType = Type(TypeKind::INTEGER, 32, true);
            stmt->variableName = SourceIdentifier("__anon_i_" + std::to_string(anonRangeCounter++), loc);
        }

        // Parse range bounds: [start..end], [start..end), start..end, start..=end
        bool hasBracket = match(TokenType::LBRACKET);
        stmt->startInclusive = true;

        auto startExpr = parse_expression();

        bool inclusive = false;
        if (match(TokenType::DOT_DOT_EQUAL))
            inclusive = true;
        else
            expect("Esperado '..' ou '..=' no range", TokenType::DOT_DOT);

        auto endExpr = parse_expression();

        if (hasBracket)
        {
            if (match(TokenType::RBRACKET))
                stmt->endInclusive = true;
            else if (match(TokenType::RPAREN))
                stmt->endInclusive = false;
            else
                expect("Esperado ']' ou ')' para fechar o bound do range", TokenType::RBRACKET);
        }
        else
        {
            stmt->endInclusive = inclusive;
        }

        expect("Esperado ')' após range", TokenType::RPAREN);

        stmt->start = std::move(startExpr);
        stmt->end = std::move(endExpr);
        stmt->body = parse_block();

        popScope();
        return stmt;
    }

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
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_and()
{
    auto left = parse_bitor();

    while (match(TokenType::AND_AND))
    {
        TokenType op = previous().type;
        auto right = parse_bitor();
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_bitor()
{
    auto left = parse_bitxor();

    while (match(TokenType::PIPE))
    {
        TokenType op = previous().type;
        auto right = parse_bitxor();
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_bitxor()
{
    auto left = parse_bitand();

    while (match(TokenType::CARET))
    {
        TokenType op = previous().type;
        auto right = parse_bitand();
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_bitand()
{
    auto left = parse_equality();

    // AMPERSAND as infix bitwise-and (& is already tokenized separately from &&)
    while (match(TokenType::AMPERSAND))
    {
        TokenType op = previous().type;
        auto right = parse_equality();
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
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
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
    }

    if (match(TokenType::IS))
    {
        auto type = parse_type();
        std::optional<std::string> bindingName;
        if (check(TokenType::IDENTIFIER))
        {
            bindingName = advance().value;
        }
        auto finalLocation = left->location;
        left = std::make_unique<IsExpression>(std::move(left), std::move(*type), finalLocation, std::move(bindingName));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_comparison()
{
    auto left = parse_shift();

    while (match(TokenType::LESS) || match(TokenType::LESS_EQUAL) ||
        match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL))
    {
        TokenType op = previous().type;
        auto right = parse_shift();
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_shift()
{
    auto left = parse_term();

    while (match(TokenType::LESS_LESS) || match(TokenType::GREATER_GREATER))
    {
        TokenType op = previous().type;
        auto right = parse_term();
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
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
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
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
        auto finalLocation = right->location - left->location;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right), finalLocation);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_unary()
{
    // C-style cast: (Type)expr
    if (check(TokenType::LPAREN))
    {
        const size_t saved = current;
        advance(); // consume '('
        if (isType())
        {
            auto castType = parse_type();
            if (match(TokenType::RPAREN))
            {
                auto operand = parse_unary();
                auto finalLocation = operand->location - castType->location;
                return std::make_unique<CastExpression>(
                    std::move(*castType),
                    std::move(operand),
                    finalLocation
                );
            }
        }
        current = saved; // backtrack — not a cast
    }

    if (match(TokenType::AWAIT))
    {
        auto awaitLocation = SourceLocation(previous().position, previous().value.length());
        auto operand = parse_unary();
        auto finalLocation = operand->location - awaitLocation;
        return std::make_unique<AwaitExpression>(std::move(operand), finalLocation);
    }

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
        match(TokenType::AMPERSAND) || match(TokenType::STAR) ||
        match(TokenType::TILDE))
    {
        TokenType op = previous().type;
        const auto opLocation = SourceLocation(previous().position, previous().value.length());
        auto operand = parse_unary();
        auto finalLocation = operand->location - opLocation;
        return std::make_unique<UnaryExpression>(op, std::move(operand), finalLocation);
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
        else if (check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS))
        {
            auto op = advance();
            auto loc = SourceLocation(op.position, op.value.length());
            expr = std::make_unique<PostfixExpression>(op.type, std::move(expr), loc);
        }
        else if (match(TokenType::LBRACKET))
        {
            auto index = parse_expression();
            expect("Esperado ']' após índice", TokenType::RBRACKET);

            if (match(TokenType::EQUAL))
            {
                auto value = parse_expression();
                auto finalLocation = value->location - index->location;
                expr = std::make_unique<IndexAssignment>(std::move(expr), std::move(index), std::move(value),
                                                         finalLocation);
            }
            else
            {
                expr = std::make_unique<IndexAccess>(std::move(expr), std::move(index), index->location);
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
    auto currentLocation = SourceLocation(peek().position, peek().value.length());
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
            return std::make_unique<IntegerLiteral>(value.substr(0, value.length() - 1), true, currentLocation);
        }
        if (value.ends_with("u"))
        {
            // Unsigned integer suffix (e.g., 10u)
            return std::make_unique<IntegerLiteral>(value.substr(0, value.length() - 1), false, currentLocation);
        }
        // Default: signed integer
        return std::make_unique<IntegerLiteral>(value, true, currentLocation);
    }

    if (match(TokenType::FLOAT_LITERAL))
    {
        return std::make_unique<FloatLiteral>(previous().value, currentLocation);
    }

    if (match(TokenType::TRUE) || match(TokenType::FALSE))
    {
        return std::make_unique<BooleanLiteral>(previous().value, currentLocation);
    }

    if (match(TokenType::STRING_LITERAL))
    {
        return std::make_unique<StringLiteral>(previous().value, currentLocation);
    }

    // Handle 'this' keyword as a simple identifier
    if (match(TokenType::THIS))
    {
        return std::make_unique<Identifier>(makeSourceIdentifier(previous()));
    }

    // Local constexpr/consteval variable: constexpr i32 x = expr;
    if (check(TokenType::CONST_EXPR) || check(TokenType::CONST_EVAL))
    {
        advance(); // consume constexpr/consteval
        auto type = parse_type();
        const Token& nameToken = expect("Esperado nome da variável constexpr", TokenType::IDENTIFIER);
        expect("Esperado '='", TokenType::EQUAL);
        auto value = parse_expression();
        // Treat as an immutable VariableInit — the generator resolves via constEvaluator
        currentScope->define_variable(nameToken.value, *type);
        return std::make_unique<VariableInit>(
            *type, makeSourceIdentifier(nameToken), std::move(value), false);
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
        // When isAuto, the identifier is always the variable name (not a type),
        // so skip type modifier parsing to avoid misinterpreting names that shadow types
        std::vector<Type> genericArgs;
        if (!isAuto && (isDeclaredStruct || (!existingVar && !isPrimitive)) && match(TokenType::LESS))
        {
            do
            {
                genericArgs.push_back(std::move(*parse_type()));
            }
            while (match(TokenType::COMMA));
            expect("Esperado '>' após argumentos genéricos", TokenType::GREATER);
        }

        int pointerDepth = 0;
        if (!isAuto && isKnownType && !existingVar)
        {
            while (match(TokenType::STAR))
            {
                pointerDepth++;
            }
        }

        bool isArray = false;
        if (!isAuto && isKnownType && !existingVar && match(TokenType::LBRACKET))
        {
            if (check(TokenType::RBRACKET))
            {
                // i32[] — array type suffix
                advance(); // consume ]
                isArray = true;
            }
            else
            {
                // Parse first expression to distinguish:
                //   type[length]       — fixed-size array allocation (single expr, no comma)
                //   type[1, 2, 3]      — typed array literal (comma-separated elements)
                auto firstExpr = parse_expression();
                if (check(TokenType::COMMA))
                {
                    // type[expr, expr, ...] — typed array literal
                    std::vector<std::unique_ptr<Expression>> elements;
                    elements.push_back(std::move(firstExpr));
                    while (match(TokenType::COMMA))
                    {
                        elements.push_back(parse_expression());
                    }
                    expect("Esperado ']'", TokenType::RBRACKET);

                    Type elemType = Type::fromToken(firstToken);
                    if (elemType.kind == TypeKind::VOID && firstToken.value != "void")
                    {
                        elemType = Type::struct_type(firstToken.value);
                    }
                    return std::make_unique<ArrayLiteral>(std::move(elements), std::move(elemType), SourceLocation{});
                }
                else
                {
                    // type[length] — fixed-size array allocation
                    expect("Esperado ']'", TokenType::RBRACKET);
                    Type elemType = Type::fromToken(firstToken);
                    if (elemType.kind == TypeKind::VOID && firstToken.value != "void")
                    {
                        elemType = Type::struct_type(firstToken.value);
                    }
                    const auto loc = SourceLocation(firstToken.position, firstToken.value.length());
                    return std::make_unique<FixedArrayExpression>(std::move(elemType), std::move(firstExpr), loc);
                }
            }
        }

        if (!isMutable && match(TokenType::MUT))
        {
            isMutable = true;
        }

        // Step 4: Decide if this is a variable declaration or an expression
        const bool isTypeInference = isAuto && !existingVar &&
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
            LOG_DEBUG(
                "[parser] variable declared: '%s' type='%s' (knownType=%d, declaredStruct=%d, primitive=%d, isArray=%d, ptr=%d, inference=%d)",
                varName.token_name.c_str(), firstToken.value.c_str(),
                isKnownType, isDeclaredStruct, isPrimitive, isArray, pointerDepth, isTypeInference);

            if (match(TokenType::EQUAL))
            {
                auto initExpression = parse_expression();
                return std::make_unique<VariableInit>(
                    std::move(varType),
                    std::move(varName),
                    std::move(initExpression),
                    isMutable
                );
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
            auto macroIt = _macros.find(qualified_name);
            if (macroIt != _macros.end())
            {
                return expand_macro(*macroIt->second);
            }

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

        // Compound assignment: name += expr → name = name + expr
        {
            TokenType compoundOp = TokenType::UNKNOWN;
            if (match(TokenType::PLUS_EQUAL)) compoundOp = TokenType::PLUS;
            else if (match(TokenType::MINUS_EQUAL)) compoundOp = TokenType::MINUS;
            else if (match(TokenType::STAR_EQUAL)) compoundOp = TokenType::STAR;
            else if (match(TokenType::SLASH_EQUAL)) compoundOp = TokenType::SLASH;
            else if (match(TokenType::PERCENT_EQUAL)) compoundOp = TokenType::PERCENT;
            else if (match(TokenType::AMPERSAND_EQUAL)) compoundOp = TokenType::AMPERSAND;
            else if (match(TokenType::PIPE_EQUAL)) compoundOp = TokenType::PIPE;
            else if (match(TokenType::CARET_EQUAL)) compoundOp = TokenType::CARET;
            else if (match(TokenType::LESS_LESS_EQUAL)) compoundOp = TokenType::LESS_LESS;
            else if (match(TokenType::GREATER_GREATER_EQUAL)) compoundOp = TokenType::GREATER_GREATER;

            if (compoundOp != TokenType::UNKNOWN)
            {
                auto rhs = parse_expression();
                auto lhsCopy = std::make_unique<Identifier>(nameIdentifier);
                auto binExpr = std::make_unique<BinaryExpression>(
                    std::move(lhsCopy), compoundOp, std::move(rhs), nameIdentifier.location);
                return std::make_unique<Assignment>(std::move(nameIdentifier), std::move(binExpr));
            }
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
    const auto initialLocation = SourceLocation(peek().position, peek().value.length());
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
    const auto finalLocation = SourceLocation(peek().position, peek().value.length());

    return std::make_unique<BraceInitializer>(std::move(elements), finalLocation - initialLocation);
}

std::unique_ptr<Expression> Parser::parse_array_literal()
{
    const auto initialLocation = SourceLocation(peek().position, peek().value.length());
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
    const auto finalLocation = SourceLocation(peek().position, peek().value.length());

    return std::make_unique<ArrayLiteral>(std::move(elements), std::optional<Type>{}, finalLocation - initialLocation);
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

    return std::make_unique<ArrayLiteral>(std::move(elements), std::move(elemType), SourceLocation{});
    // TODO: VALID SOURCE LOCATION HERE
}

std::unique_ptr<Expression> Parser::parse_switch_expression()
{
    const auto initialLocation = SourceLocation(peek().position, peek().value.length());
    expect("Esperado 'switch'", TokenType::SWITCH);

    // Parse the value expression (without parentheses, unlike switch statement)
    auto value = parse_expression();
    const auto finalLocation = SourceLocation(peek().position, peek().value.length());

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

    return std::make_unique<SwitchExpression>(std::move(value), std::move(arms), finalLocation - initialLocation);
}

std::unique_ptr<ExternFunctionDeclaration> Parser::parse_extern_function(const std::string& abi)
{
    const auto initialLocation = SourceLocation(peek().position, peek().value.length());
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
    const auto finalLocation = SourceLocation(peek().position, peek().value.length());
    expect("Esperado ';'", TokenType::SEMICOLON);

    auto decl = std::make_unique<ExternFunctionDeclaration>();
    decl->name = makeSourceIdentifier(nameToken);
    decl->returnType = std::move(returnType);
    decl->parameters = std::move(parameters);
    decl->isVariadic = isVariadic;
    decl->location = finalLocation - initialLocation;
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
            expect("Esperado 'struct'", TokenType::STRUCT);
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

void Parser::parse_where_clause(GenericParams& params)
{
    expect("Experado 'where' antes de uma constraint", TokenType::WHERE);
    // where_clause = "where" constraint_decl { ";" constraint_decl }
    // constraint_decl = IDENTIFIER ":" IDENTIFIER { "," IDENTIFIER }
    const Token& paramNameToken = expect("Esperado nome do parâmetro genérico na cláusula where",
                                         TokenType::IDENTIFIER);
    const std::string& paramName = paramNameToken.value;

    // Find the GenericParam by name
    GenericParam* target = nullptr;
    for (auto& p : params.params)
    {
        if (p.name.token_name == paramName)
        {
            target = &p;
            break;
        }
    }

    if (!target)
    {
        PARSER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN,
                     "'" + paramName + "' is not a generic parameter",
                     SourceLocation(paramNameToken.position.fileId, paramNameToken.position.line,
                         paramNameToken.position.column, paramName.length()));
    }

    expect("Esperado ':' após nome do parâmetro na cláusula where", TokenType::COLON);

    // Parse constraint list: IFoo, IBar, IBaz
    do
    {
        const Token& constraintToken = expect("Esperado nome da interface constraint", TokenType::IDENTIFIER);
        target->constraints.push_back(constraintToken.value);
    }
    while (match(TokenType::COMMA));

    // Consume optional trailing semicolon to allow:
    //   where Key : Hashable;
    //   where Value : Hashable
    match(TokenType::SEMICOLON);
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

    // Parse where clause: enum Result<T, E> where T : ISerializable { ... }
    while (check(TokenType::WHERE))
    {
        parse_where_clause(genericParams);
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
        LOG_DEBUG("[parser] enum declared: '%s' with %zu variants%s", identifierToken.value.c_str(),
                  values.size(), genericParams.empty() ? "" : " (generic)");
        return std::make_unique<EnumDeclaration>(makeSourceIdentifier(identifierToken), std::move(genericParams),
                                                 values);
    }
    LOG_DEBUG("[parser] enum declared: '%s' (empty)%s", identifierToken.value.c_str(),
              genericParams.empty() ? "" : " (generic)");
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

    // Check for "impl Interface[, Interface2, ...] for Type" form
    if (check(TokenType::COMMA) || check(TokenType::FOR))
    {
        // First interface name
        impl->interfaceNames.push_back(firstType->kind == TypeKind::STRUCT
                                           ? firstType->structName
                                           : firstType->toHumanString());

        // Parse additional comma-separated interface names
        while (check(TokenType::COMMA))
        {
            advance(); // consume ','
            auto nextType = parse_type();
            impl->interfaceNames.push_back(nextType->kind == TypeKind::STRUCT
                                               ? nextType->structName
                                               : nextType->toHumanString());
        }

        expect("Esperado 'for' após interface(s) no impl", TokenType::FOR);
        do
        {
            impl->targetTypes.push_back(*parse_type());
        }
        while (match(TokenType::COMMA));
    }
    else
    {
        // "impl Type { methods }" form
        impl->targetTypes.emplace_back(*firstType);
    }

    expect("Esperado '{' após tipo no impl", TokenType::LBRACE);

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        auto methodAttributes = parse_attributes();

        if (match(TokenType::OPERATOR))
        {
            auto op = parse_operator(true);
            op->attributes = std::move(methodAttributes);
            impl->methods.push_back(std::move(op));
            continue;
        }

        // Parse optional modifiers (public, private, static) and field qualifiers (mut, const)
        const size_t saved = current;
        auto fieldModifiers = parse_modifiers();
        bool isMutable = match(TokenType::MUT);
        bool isConstant = match(TokenType::CONST);
        isMutable |= match(TokenType::MUT);
        isConstant |= match(TokenType::CONST);

        if (isConstant || isMutable)
        {
            // Definitely a field: const/mut type name [= expr] ;
            auto fieldType = parse_type();
            const Token& fieldNameToken = expect("Esperado nome do campo", TokenType::IDENTIFIER);
            SourceIdentifier fieldName = makeSourceIdentifier(fieldNameToken);

            std::unique_ptr<Expression> initializer;
            if (match(TokenType::EQUAL))
            {
                initializer = parse_expression();
            }
            expect("Esperado ';'", TokenType::SEMICOLON);
            impl->fields.emplace_back(std::move(fieldType), std::move(fieldName),
                                      isConstant, std::move(initializer), std::move(fieldModifiers));
            continue;
        }

        // No const/mut — could be a field (type name ;) or method (type name ( ... ))
        // Try parsing as type + name, then check what follows
        if (!fieldModifiers.empty())
        {
            // Has visibility modifiers but no const/mut — must be a method
            current = saved;
            auto m = parse_method(true);
            m->attributes = std::move(methodAttributes);
            impl->methods.push_back(std::move(m));
            continue;
        }

        // No modifiers at all — try to distinguish field from method
        // Save position, try to parse type + identifier
        auto fieldType = parse_type();
        if (check(TokenType::IDENTIFIER))
        {
            const size_t afterType = current;
            const Token& nameToken = advance();

            if (check(TokenType::SEMICOLON) || check(TokenType::EQUAL))
            {
                // It's a field: type name [= expr] ;
                SourceIdentifier fieldName = makeSourceIdentifier(nameToken);
                std::unique_ptr<Expression> initializer;
                if (match(TokenType::EQUAL))
                {
                    initializer = parse_expression();
                }
                expect("Esperado ';'", TokenType::SEMICOLON);
                impl->fields.emplace_back(std::move(fieldType), std::move(fieldName),
                                          false, std::move(initializer));
                continue;
            }
            // Not a field — backtrack to before type and parse as method
            current = saved;
        }
        else
        {
            current = saved;
        }

        auto m = parse_method(true);
        m->attributes = std::move(methodAttributes);
        impl->methods.push_back(std::move(m));
    }

    expect("Esperado '}' no impl", TokenType::RBRACE);

    return impl;
}

std::string Parser::operator_token_to_canonical_name(const TokenType op)
{
    switch (op)
    {
    case TokenType::PLUS: return "__op_add";
    case TokenType::MINUS: return "__op_sub";
    case TokenType::STAR: return "__op_mul";
    case TokenType::SLASH: return "__op_div";
    case TokenType::PERCENT: return "__op_mod";
    case TokenType::EQUAL_EQUAL: return "__op_eq";
    case TokenType::BANG_EQUAL: return "__op_neq";
    case TokenType::LESS: return "__op_lt";
    case TokenType::LESS_EQUAL: return "__op_lte";
    case TokenType::GREATER: return "__op_gt";
    case TokenType::GREATER_EQUAL: return "__op_gte";
    case TokenType::AMPERSAND: return "__op_bitand";
    case TokenType::PIPE: return "__op_bitor";
    case TokenType::CARET: return "__op_bitxor";
    case TokenType::TILDE: return "__op_bitnot";
    case TokenType::LESS_LESS: return "__op_shl";
    case TokenType::GREATER_GREATER: return "__op_shr";
    case TokenType::AND_AND: return "__op_and";
    case TokenType::OR_OR: return "__op_or";
    case TokenType::BANG: return "__op_not";
    default: return "";
    }
}

std::unique_ptr<StructMethodDeclaration> Parser::parse_operator(const bool allowBody)
{
    auto method = std::make_unique<StructMethodDeclaration>();
    method->isOperatorMethod = true;

    // operator keyword already consumed
    // Operators are implicitly public and static
    method->modifiers.push_back(VisibilityModifier::PUBLIC);
    method->modifiers.push_back(VisibilityModifier::STATIC);

    // Check for get[] or set[]
    if (check(TokenType::IDENTIFIER) && (peek().value == "get" || peek().value == "set"))
    {
        const std::string indexKind = peek().value;
        advance(); // consume get/set
        expect("Esperado '[' após '" + indexKind + "'", TokenType::LBRACKET);
        expect("Esperado ']' após '['", TokenType::RBRACKET);
        method->operatorCanonicalName = "__op_index_" + indexKind;
        method->name = SourceIdentifier(method->operatorCanonicalName,
                                        SourceLocation(previous().position.fileId, previous().position.line,
                                                       previous().position.column, 0));
    }
    else
    {
        // Parse operator symbol: +, -, *, /, %, ==, !=, <, >, <=, >=, &, |, ^, ~, <<, >>, &&, ||, !
        static const std::vector<TokenType> operatorTokens = {
            TokenType::PLUS, TokenType::MINUS, TokenType::STAR, TokenType::SLASH, TokenType::PERCENT,
            TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL,
            TokenType::LESS, TokenType::LESS_EQUAL, TokenType::GREATER, TokenType::GREATER_EQUAL,
            TokenType::AMPERSAND, TokenType::PIPE, TokenType::CARET, TokenType::TILDE,
            TokenType::LESS_LESS, TokenType::GREATER_GREATER,
            TokenType::AND_AND, TokenType::OR_OR, TokenType::BANG,
        };

        const Token& opToken = expect("Esperado operador (+, -, *, /, ==, etc.)", operatorTokens);
        method->operatorCanonicalName = operator_token_to_canonical_name(opToken.type);
        method->name = SourceIdentifier(method->operatorCanonicalName,
                                        SourceLocation(opToken.position.fileId, opToken.position.line,
                                                       opToken.position.column, opToken.value.length()));
    }

    // Parse parameters: (Type left, Type right) or (Type operand) for unary
    expect("Esperado '(' após operador", TokenType::LPAREN);
    if (!check(TokenType::RPAREN))
    {
        do
        {
            auto paramType = parse_type();
            const Token& paramNameToken = expect("Esperado nome do parâmetro", TokenType::IDENTIFIER);
            bool isMutable = match(TokenType::MUT);
            method->parameters.emplace_back(std::move(paramType), makeSourceIdentifier(paramNameToken), isMutable);
        }
        while (match(TokenType::COMMA));
    }
    expect("Esperado ')'", TokenType::RPAREN);

    // Parse return type: -> Type
    expect("Esperado '->' para tipo de retorno do operador", TokenType::THIN_ARROW);
    method->returnType = parse_type();

    if (!allowBody)
    {
        // Interface operator signature - just semicolon
        expect("Esperado ';' após assinatura do operador", TokenType::SEMICOLON);
        return method;
    }

    // Parse body
    pushScope();
    for (const auto& param : method->parameters)
    {
        currentScope->define_variable(param.name.token_name, *param.type);
    }

    if (match(TokenType::ARROW))
    {
        method->expression = parse_expression();
        expect("Esperado ';' após expressão", TokenType::SEMICOLON);
    }
    else if (check(TokenType::LBRACE))
    {
        method->body = parse_block();
    }
    else
    {
        expect("Esperado '{' ou '=>' para corpo do operador", TokenType::LBRACE);
    }

    popScope();
    return method;
}

// macro name { (params) => { body } }
std::unique_ptr<MacroDeclaration> Parser::parse_macro()
{
    const auto initialLocation = SourceLocation(peek().position, peek().value.length());
    expect("Esperado 'macro'", TokenType::MACRO);
    const Token& nameToken = expect("Esperado nome da macro", TokenType::IDENTIFIER);

    auto decl = std::make_unique<MacroDeclaration>(makeSourceIdentifier(nameToken));
    decl->location = initialLocation;

    expect("Esperado '{'", TokenType::LBRACE);

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        MacroRule rule;

        expect("Esperado '(' para parâmetros da macro", TokenType::LPAREN);
        while (!check(TokenType::RPAREN) && !isAtEnd())
        {
            if (!rule.parameters.empty())
                expect("Esperado ','", TokenType::COMMA);

            bool isLocal = false;
            if (check(TokenType::IDENTIFIER) && peek().value == "local")
            {
                advance();
                isLocal = true;
            }

            const Token& fragToken = expect("Esperado tipo do fragmento (expr)", TokenType::IDENTIFIER);
            if (fragToken.value != "expr")
            {
                PARSER_ERROR(DiagnosticCode::EXPECTED_EXPRESSION,
                             "tipo de fragmento de macro desconhecido: '" + fragToken.value + "'. Esperado: 'expr'",
                             SourceLocation(fragToken.position, fragToken.value.length()));
            }

            const Token& paramName = expect("Esperado nome do parâmetro", TokenType::IDENTIFIER);
            rule.parameters.emplace_back(MacroFragmentType::EXPR, makeSourceIdentifier(paramName), isLocal);
        }
        expect("Esperado ')'", TokenType::RPAREN);

        expect("Esperado '=>'", TokenType::ARROW);

        expect("Esperado '{'", TokenType::LBRACE);
        int braceDepth = 1;
        while (braceDepth > 0 && !isAtEnd())
        {
            if (check(TokenType::LBRACE)) braceDepth++;
            else if (check(TokenType::RBRACE))
            {
                braceDepth--;
                if (braceDepth == 0) break;
            }
            rule.bodyTokens.push_back(peek());
            advance();
        }
        expect("Esperado '}'", TokenType::RBRACE);

        for (const auto& param : rule.parameters)
        {
            if (param.isLocal) continue;
            int count = 0;
            for (const auto& tok : rule.bodyTokens)
            {
                if (tok.type == TokenType::IDENTIFIER && tok.value == param.name.token_name)
                    count++;
            }
            if (count > 1)
            {
                _diagnostics.emitAndPrint(Diagnostic(
                    Severity::Warning, DiagnosticCode::MACRO_POSSIBLE_SIDE_EFFECT,
                    "macro '" + nameToken.value + "': parameter '" + param.name.token_name +
                    "' is used " + std::to_string(count) + " times without 'local', "
                    "which may cause side effects. Consider using 'local expr " + param.name.token_name + "'",
                    param.name.location));
            }
        }

        decl->rules.push_back(std::move(rule));
    }

    expect("Esperado '}'", TokenType::RBRACE);

    LOG_DEBUG("[parser] macro declared: '%s' with %zu rules", nameToken.value.c_str(), decl->rules.size());
    return decl;
}

std::vector<Token> Parser::collect_macro_arg_tokens()
{
    std::vector<Token> argTokens;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;

    while (!isAtEnd())
    {
        if (check(TokenType::LPAREN)) parenDepth++;
        else if (check(TokenType::RPAREN))
        {
            if (parenDepth == 0) break;
            parenDepth--;
        }
        else if (check(TokenType::LBRACKET)) bracketDepth++;
        else if (check(TokenType::RBRACKET)) bracketDepth--;
        else if (check(TokenType::LBRACE)) braceDepth++;
        else if (check(TokenType::RBRACE)) braceDepth--;
        else if (check(TokenType::COMMA) && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) break;

        argTokens.push_back(peek());
        advance();
    }
    return argTokens;
}

std::unique_ptr<Expression> Parser::expand_macro(const MacroDeclaration& macro)
{
    if (macro.rules.empty())
    {
        PARSER_ERROR(DiagnosticCode::EXPECTED_EXPRESSION,
                     "macro '" + macro.name.token_name + "' não tem regras definidas",
                     macro.location);
    }

    const auto& rule = macro.rules[0];

    std::vector<std::vector<Token>> argTokenSets;
    while (!check(TokenType::RPAREN) && !isAtEnd())
    {
        if (!argTokenSets.empty())
            expect("Esperado ','", TokenType::COMMA);
        argTokenSets.push_back(collect_macro_arg_tokens());
    }
    expect("Esperado ')'", TokenType::RPAREN);

    if (argTokenSets.size() != rule.parameters.size())
    {
        PARSER_ERROR(DiagnosticCode::EXPECTED_EXPRESSION,
                     "macro '" + macro.name.token_name + "' espera " +
                     std::to_string(rule.parameters.size()) + " argumento(s), mas recebeu " +
                     std::to_string(argTokenSets.size()),
                     macro.location);
    }

    bool hasLocals = false;
    for (const auto& param : rule.parameters)
    {
        if (param.isLocal)
        {
            hasLocals = true;
            break;
        }
    }

    const auto expansionId = _macroExpansionCounter++;

    std::vector<std::string> localVarNames(rule.parameters.size());
    std::vector<std::vector<Token>> localArgTokens;

    if (hasLocals)
    {
        for (size_t i = 0; i < rule.parameters.size(); i++)
        {
            if (rule.parameters[i].isLocal)
            {
                localVarNames[i] = "__macro_" + macro.name.token_name + "_" + rule.parameters[i].name.token_name + "_" +
                    std::to_string(expansionId);
                localArgTokens.push_back(argTokenSets[i]);
            }
        }
    }

    std::vector<Token> expanded;
    for (const auto& tok : rule.bodyTokens)
    {
        bool substituted = false;
        if (tok.type == TokenType::IDENTIFIER)
        {
            for (size_t i = 0; i < rule.parameters.size(); i++)
            {
                if (tok.value == rule.parameters[i].name.token_name)
                {
                    if (rule.parameters[i].isLocal)
                    {
                        expanded.push_back({tok.position, TokenType::IDENTIFIER, localVarNames[i]});
                    }
                    else
                    {
                        expanded.push_back({tok.position, TokenType::LPAREN, "("});
                        for (const auto& argTok : argTokenSets[i])
                            expanded.push_back(argTok);
                        expanded.push_back({tok.position, TokenType::RPAREN, ")"});
                    }
                    substituted = true;
                    break;
                }
            }
        }
        if (!substituted)
        {
            expanded.push_back(tok);
        }
    }
    expanded.push_back({Position{}, TokenType::END_OF_FILE, ""});

    {
        std::ostringstream bodyStream;
        if (hasLocals)
        {
            for (size_t i = 0; i < rule.parameters.size(); i++)
            {
                if (rule.parameters[i].isLocal)
                {
                    bodyStream << "auto " << rule.parameters[i].name.token_name << " = ";
                    for (const auto& t : argTokenSets[i]) bodyStream << t.value;
                    bodyStream << "; ";
                }
            }
        }
        for (const auto& tok : rule.bodyTokens)
        {
            bool replaced = false;
            if (tok.type == TokenType::IDENTIFIER)
            {
                for (size_t i = 0; i < rule.parameters.size(); i++)
                {
                    if (tok.value == rule.parameters[i].name.token_name)
                    {
                        if (rule.parameters[i].isLocal)
                        {
                            bodyStream << rule.parameters[i].name.token_name;
                        }
                        else
                        {
                            bodyStream << '(';
                            for (const auto& t : argTokenSets[i]) bodyStream << t.value;
                            bodyStream << ')';
                        }
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced)
            {
                if (bodyStream.tellp() > 0 && tok.value != "(" && tok.value != ")" &&
                    bodyStream.str().back() != '(' && bodyStream.str().back() != ')')
                    bodyStream << ' ';
                bodyStream << tok.value;
            }
        }

        std::ostringstream argsStream;
        for (size_t i = 0; i < argTokenSets.size(); i++)
        {
            if (i > 0) argsStream << ", ";
            for (const auto& t : argTokenSets[i]) argsStream << t.value;
        }

        int callLine = 0;
        if (!argTokenSets.empty() && !argTokenSets[0].empty())
            callLine = argTokenSets[0][0].position.line;

        macroExpansions.push_back({macro.name.token_name, argsStream.str(), bodyStream.str(), callLine});

        if (printMacroExpansion)
        {
            const auto& record = macroExpansions.back();
            LOG_INFO("[macro] %s(%s) => %s", macro.name.token_name.c_str(),
                     record.argsText.c_str(), record.expandedText.c_str());
        }
    }

    auto savedTokens = std::move(tokens);
    auto savedCurrent = current;

    if (hasLocals)
    {
        std::vector<MacroExpansionExpression::LocalBinding> locals;
        size_t localIdx = 0;
        for (size_t i = 0; i < rule.parameters.size(); i++)
        {
            if (rule.parameters[i].isLocal)
            {
                auto argToks = localArgTokens[localIdx++];
                argToks.push_back({Position{}, TokenType::END_OF_FILE, ""});

                tokens = std::move(argToks);
                current = 0;
                auto argExpr = parse_expression();

                MacroExpansionExpression::LocalBinding binding;
                binding.varName = localVarNames[i];
                binding.value = std::move(argExpr);
                locals.push_back(std::move(binding));
            }
        }

        tokens = std::move(expanded);
        current = 0;
        auto bodyExpr = parse_expression();

        tokens = std::move(savedTokens);
        current = savedCurrent;

        return std::make_unique<MacroExpansionExpression>(std::move(locals), std::move(bodyExpr));
    }

    tokens = std::move(expanded);
    current = 0;
    auto result = parse_expression();

    tokens = std::move(savedTokens);
    current = savedCurrent;

    return result;
}
