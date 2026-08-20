#ifndef DJINN_DJLIB_FORMAT_H
#define DJINN_DJLIB_FORMAT_H

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../parser/ast/Type.h"
#include "../parser/ast/Declaration.h"
#include "../binder/Symbol.h"
#include "../lexer/Token.h"

namespace djlib
{
    constexpr char MAGIC[4] = {'D', 'J', 'L', 'B'};
    constexpr uint16_t VERSION = 1;

    struct Header
    {
        char magic[4];
        uint16_t version;
        uint16_t flags;
        uint32_t metadataSize;
        uint32_t bitcodeSize;
    };

    static_assert(sizeof(Header) == 16, "DjLib header must be exactly 16 bytes");

    inline std::string serializeTokenType(TokenType t)
    {
        switch (t)
        {
        case TokenType::UNKNOWN: return "UNKNOWN";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::EXTERN: return "EXTERN";
        case TokenType::RETURN: return "RETURN";
        case TokenType::STRUCT: return "STRUCT";
        case TokenType::NAMESPACE: return "NAMESPACE";
        case TokenType::IMPORT: return "IMPORT";
        case TokenType::MUT: return "MUT";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::FOR: return "FOR";
        case TokenType::WHILE: return "WHILE";
        case TokenType::DO: return "DO";
        case TokenType::SWITCH: return "SWITCH";
        case TokenType::CASE: return "CASE";
        case TokenType::DEFAULT: return "DEFAULT";
        case TokenType::BREAK: return "BREAK";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::INTERFACE: return "INTERFACE";
        case TokenType::PUBLIC: return "PUBLIC";
        case TokenType::PRIVATE: return "PRIVATE";
        case TokenType::STATIC: return "STATIC";
        case TokenType::THIS: return "THIS";
        case TokenType::INT: return "INT";
        case TokenType::UINT: return "UINT";
        case TokenType::VOID: return "VOID";
        case TokenType::FLOAT: return "FLOAT";
        case TokenType::STRING: return "STRING";
        case TokenType::AUTO: return "AUTO";
        case TokenType::FALSE: return "FALSE";
        case TokenType::TRUE: return "TRUE";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::PLUS_PLUS: return "PLUS_PLUS";
        case TokenType::MINUS_MINUS: return "MINUS_MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TokenType::BANG_EQUAL: return "BANG_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::GREATER: return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::BANG: return "BANG";
        case TokenType::AND_AND: return "AND_AND";
        case TokenType::OR_OR: return "OR_OR";
        case TokenType::AMPERSAND: return "AMPERSAND";
        case TokenType::PIPE: return "PIPE";
        case TokenType::CARET: return "CARET";
        case TokenType::TILDE: return "TILDE";
        case TokenType::LESS_LESS: return "LESS_LESS";
        case TokenType::GREATER_GREATER: return "GREATER_GREATER";
        case TokenType::EQUAL: return "EQUAL";
        case TokenType::ARROW: return "ARROW";
        case TokenType::THIN_ARROW: return "THIN_ARROW";
        case TokenType::PLUS_EQUAL: return "PLUS_EQUAL";
        case TokenType::MINUS_EQUAL: return "MINUS_EQUAL";
        case TokenType::STAR_EQUAL: return "STAR_EQUAL";
        case TokenType::SLASH_EQUAL: return "SLASH_EQUAL";
        case TokenType::PERCENT_EQUAL: return "PERCENT_EQUAL";
        case TokenType::AMPERSAND_EQUAL: return "AMPERSAND_EQUAL";
        case TokenType::PIPE_EQUAL: return "PIPE_EQUAL";
        case TokenType::CARET_EQUAL: return "CARET_EQUAL";
        case TokenType::LESS_LESS_EQUAL: return "LESS_LESS_EQUAL";
        case TokenType::GREATER_GREATER_EQUAL: return "GREATER_GREATER_EQUAL";
        case TokenType::COLON: return "COLON";
        case TokenType::COLON_COLON: return "COLON_COLON";
        case TokenType::DOT: return "DOT";
        case TokenType::DOT_DOT: return "DOT_DOT";
        case TokenType::DOT_DOT_EQUAL: return "DOT_DOT_EQUAL";
        case TokenType::DOT_DOT_DOT: return "DOT_DOT_DOT";
        case TokenType::AT: return "AT";
        case TokenType::ENUM: return "ENUM";
        case TokenType::NEW: return "NEW";
        case TokenType::IMPL: return "IMPL";
        case TokenType::WHERE: return "WHERE";
        case TokenType::IN: return "IN";
        case TokenType::ASYNC: return "ASYNC";
        case TokenType::AWAIT: return "AWAIT";
        case TokenType::YIELD: return "YIELD";
        case TokenType::SPAWN: return "SPAWN";
        case TokenType::OPERATOR: return "OPERATOR";
        case TokenType::CONST_EXPR: return "CONST_EXPR";
        case TokenType::CONST_EVAL: return "CONST_EVAL";
        case TokenType::CONST: return "CONST";
        case TokenType::MACRO: return "MACRO";
        case TokenType::IS: return "IS";
        default: return "UNKNOWN";
        }
    }

    inline TokenType deserializeTokenType(const std::string& s)
    {
        static const std::unordered_map<std::string, TokenType> map = {
            {"UNKNOWN", TokenType::UNKNOWN}, {"EOF", TokenType::END_OF_FILE},
            {"EXTERN", TokenType::EXTERN}, {"RETURN", TokenType::RETURN},
            {"STRUCT", TokenType::STRUCT}, {"NAMESPACE", TokenType::NAMESPACE},
            {"IMPORT", TokenType::IMPORT}, {"MUT", TokenType::MUT},
            {"IF", TokenType::IF}, {"ELSE", TokenType::ELSE},
            {"FOR", TokenType::FOR}, {"WHILE", TokenType::WHILE},
            {"DO", TokenType::DO}, {"SWITCH", TokenType::SWITCH},
            {"CASE", TokenType::CASE}, {"DEFAULT", TokenType::DEFAULT},
            {"BREAK", TokenType::BREAK}, {"CONTINUE", TokenType::CONTINUE},
            {"INTERFACE", TokenType::INTERFACE}, {"PUBLIC", TokenType::PUBLIC},
            {"PRIVATE", TokenType::PRIVATE}, {"STATIC", TokenType::STATIC},
            {"THIS", TokenType::THIS}, {"INT", TokenType::INT},
            {"UINT", TokenType::UINT}, {"VOID", TokenType::VOID},
            {"FLOAT", TokenType::FLOAT}, {"STRING", TokenType::STRING},
            {"AUTO", TokenType::AUTO}, {"FALSE", TokenType::FALSE},
            {"TRUE", TokenType::TRUE}, {"IDENTIFIER", TokenType::IDENTIFIER},
            {"STRING_LITERAL", TokenType::STRING_LITERAL},
            {"INTEGER_LITERAL", TokenType::INTEGER_LITERAL},
            {"FLOAT_LITERAL", TokenType::FLOAT_LITERAL},
            {"LPAREN", TokenType::LPAREN}, {"RPAREN", TokenType::RPAREN},
            {"LBRACE", TokenType::LBRACE}, {"RBRACE", TokenType::RBRACE},
            {"LBRACKET", TokenType::LBRACKET}, {"RBRACKET", TokenType::RBRACKET},
            {"SEMICOLON", TokenType::SEMICOLON}, {"COMMA", TokenType::COMMA},
            {"PLUS", TokenType::PLUS}, {"MINUS", TokenType::MINUS},
            {"PLUS_PLUS", TokenType::PLUS_PLUS}, {"MINUS_MINUS", TokenType::MINUS_MINUS},
            {"STAR", TokenType::STAR}, {"SLASH", TokenType::SLASH},
            {"PERCENT", TokenType::PERCENT}, {"EQUAL_EQUAL", TokenType::EQUAL_EQUAL},
            {"BANG_EQUAL", TokenType::BANG_EQUAL}, {"LESS", TokenType::LESS},
            {"LESS_EQUAL", TokenType::LESS_EQUAL}, {"GREATER", TokenType::GREATER},
            {"GREATER_EQUAL", TokenType::GREATER_EQUAL}, {"BANG", TokenType::BANG},
            {"AND_AND", TokenType::AND_AND}, {"OR_OR", TokenType::OR_OR},
            {"AMPERSAND", TokenType::AMPERSAND}, {"PIPE", TokenType::PIPE},
            {"CARET", TokenType::CARET}, {"TILDE", TokenType::TILDE},
            {"LESS_LESS", TokenType::LESS_LESS}, {"GREATER_GREATER", TokenType::GREATER_GREATER},
            {"EQUAL", TokenType::EQUAL}, {"ARROW", TokenType::ARROW},
            {"THIN_ARROW", TokenType::THIN_ARROW}, {"PLUS_EQUAL", TokenType::PLUS_EQUAL},
            {"MINUS_EQUAL", TokenType::MINUS_EQUAL}, {"STAR_EQUAL", TokenType::STAR_EQUAL},
            {"SLASH_EQUAL", TokenType::SLASH_EQUAL}, {"PERCENT_EQUAL", TokenType::PERCENT_EQUAL},
            {"AMPERSAND_EQUAL", TokenType::AMPERSAND_EQUAL}, {"PIPE_EQUAL", TokenType::PIPE_EQUAL},
            {"CARET_EQUAL", TokenType::CARET_EQUAL}, {"LESS_LESS_EQUAL", TokenType::LESS_LESS_EQUAL},
            {"GREATER_GREATER_EQUAL", TokenType::GREATER_GREATER_EQUAL},
            {"COLON", TokenType::COLON}, {"COLON_COLON", TokenType::COLON_COLON},
            {"DOT", TokenType::DOT}, {"DOT_DOT", TokenType::DOT_DOT},
            {"DOT_DOT_EQUAL", TokenType::DOT_DOT_EQUAL}, {"DOT_DOT_DOT", TokenType::DOT_DOT_DOT},
            {"AT", TokenType::AT}, {"ENUM", TokenType::ENUM},
            {"NEW", TokenType::NEW}, {"IMPL", TokenType::IMPL},
            {"WHERE", TokenType::WHERE}, {"IN", TokenType::IN},
            {"ASYNC", TokenType::ASYNC}, {"AWAIT", TokenType::AWAIT},
            {"YIELD", TokenType::YIELD}, {"SPAWN", TokenType::SPAWN},
            {"OPERATOR", TokenType::OPERATOR}, {"CONST_EXPR", TokenType::CONST_EXPR},
            {"CONST_EVAL", TokenType::CONST_EVAL}, {"CONST", TokenType::CONST},
            {"MACRO", TokenType::MACRO}, {"IS", TokenType::IS},
        };
        auto it = map.find(s);
        return it != map.end() ? it->second : TokenType::UNKNOWN;
    }

    inline std::string fragmentTypeToString(MacroFragmentType ft)
    {
        switch (ft)
        {
        case MacroFragmentType::EXPRESSION: return "expression";
        case MacroFragmentType::IDENTIFIER: return "identifier";
        case MacroFragmentType::LITERAL: return "literal";
        case MacroFragmentType::TYPE: return "type";
        case MacroFragmentType::BLOCK: return "block";
        case MacroFragmentType::LITERAL_TOKEN: return "literal_token";
        default: return "expression";
        }
    }

    inline MacroFragmentType stringToFragmentType(const std::string& s)
    {
        if (s == "expression") return MacroFragmentType::EXPRESSION;
        if (s == "identifier") return MacroFragmentType::IDENTIFIER;
        if (s == "literal") return MacroFragmentType::LITERAL;
        if (s == "type") return MacroFragmentType::TYPE;
        if (s == "block") return MacroFragmentType::BLOCK;
        if (s == "literal_token") return MacroFragmentType::LITERAL_TOKEN;
        return MacroFragmentType::EXPRESSION;
    }
} // namespace djlib

// ============================================================================
// ADL-visible to_json/from_json for global-namespace types
// nlohmann/json finds these via argument-dependent lookup
// ============================================================================

inline void to_json(nlohmann::json& j, const Position& p)
{
    j = {{"line", p.line}, {"col", p.column}};
}

inline void from_json(const nlohmann::json& j, Position& p)
{
    p.fileId = "";
    p.line = j.value("line", 0u);
    p.column = j.value("col", 0u);
    p.index = 0;
}

inline void to_json(nlohmann::json& j, const Token& t)
{
    j = {{"t", djlib::serializeTokenType(t.type)}, {"v", t.value}};
    if (t.position.line != 0)
        j["p"] = t.position;
}

inline void from_json(const nlohmann::json& j, Token& t)
{
    t.type = djlib::deserializeTokenType(j.at("t").get<std::string>());
    t.value = j.at("v").get<std::string>();
    if (j.contains("p"))
        t.position = j.at("p").get<Position>();
}

inline void to_json(nlohmann::json& j, const Type& t)
{
    j["kind"] = Type::kindToString(t.kind);
    if (t.size != 0) j["size"] = t.size;
    if (t.sign) j["sign"] = true;
    if (t.nullable) j["nullable"] = true;
    if (t.readOnly) j["readOnly"] = true;
    if (t.isTransparent) j["transparent"] = true;
    if (t.native) j["native"] = true;
    if (t.overflowMode != OverflowMode::None) j["overflowMode"] = static_cast<uint8_t>(t.overflowMode);
    if (!t.structName.empty()) j["structName"] = t.structName;
    if (!t.genericArgs.empty()) j["genericArgs"] = t.genericArgs;
    if (t.elementType) j["elementType"] = *t.elementType;
}

inline void from_json(const nlohmann::json& j, Type& t)
{
    auto kindStr = j.at("kind").get<std::string>();
    if (kindStr == "INTEGER") t.kind = TypeKind::INTEGER;
    else if (kindStr == "VOID") t.kind = TypeKind::VOID;
    else if (kindStr == "F16") t.kind = TypeKind::F16;
    else if (kindStr == "F32") t.kind = TypeKind::F32;
    else if (kindStr == "F64") t.kind = TypeKind::F64;
    else if (kindStr == "F128") t.kind = TypeKind::F128;
    else if (kindStr == "STRUCT") t.kind = TypeKind::STRUCT;
    else if (kindStr == "AUTO") t.kind = TypeKind::AUTO;
    else if (kindStr == "ARRAY") t.kind = TypeKind::ARRAY;
    else if (kindStr == "POINTER") t.kind = TypeKind::POINTER;

    t.size = j.value("size", static_cast<size_t>(0));
    t.sign = j.value("sign", false);
    t.nullable = j.value("nullable", false);
    t.readOnly = j.value("readOnly", false);
    t.isTransparent = j.value("transparent", false);
    t.native = j.value("native", false);
    t.overflowMode = static_cast<OverflowMode>(j.value("overflowMode", static_cast<uint8_t>(OverflowMode::None)));
    t.structName = j.value("structName", std::string{});
    if (j.contains("genericArgs"))
        t.genericArgs = j.at("genericArgs").get<std::vector<Type>>();
    if (j.contains("elementType"))
        t.elementType = std::make_unique<Type>(j.at("elementType").get<Type>());
}

inline void to_json(nlohmann::json& j, const GenericParamInfo& g)
{
    j["name"] = g.name;
    if (!g.constraints.empty()) j["constraints"] = g.constraints;
}

inline void from_json(const nlohmann::json& j, GenericParamInfo& g)
{
    g.name = j.at("name").get<std::string>();
    if (j.contains("constraints"))
        g.constraints = j.at("constraints").get<std::vector<std::string>>();
}

inline void to_json(nlohmann::json& j, const AttributeArg& a)
{
    if (a.name.has_value()) j["name"] = *a.name;
    std::visit([&j](auto&& val)
    {
        using V = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<V, int64_t>)
        {
            j["type"] = "int";
            j["value"] = val;
        }
        else if constexpr (std::is_same_v<V, double>)
        {
            j["type"] = "float";
            j["value"] = val;
        }
        else if constexpr (std::is_same_v<V, std::string>)
        {
            j["type"] = "string";
            j["value"] = val;
        }
        else if constexpr (std::is_same_v<V, bool>)
        {
            j["type"] = "bool";
            j["value"] = val;
        }
    }, a.value);
}

inline void from_json(const nlohmann::json& j, AttributeArg& a)
{
    if (j.contains("name")) a.name = j.at("name").get<std::string>();
    auto type = j.at("type").get<std::string>();
    if (type == "int") a.value = j.at("value").get<int64_t>();
    else if (type == "float") a.value = j.at("value").get<double>();
    else if (type == "string") a.value = j.at("value").get<std::string>();
    else if (type == "bool") a.value = j.at("value").get<bool>();
}

inline void to_json(nlohmann::json& j, const AttributeSymbol& a)
{
    j["name"] = a.name;
    if (!a.args.empty()) j["args"] = a.args;
}

inline void from_json(const nlohmann::json& j, AttributeSymbol& a)
{
    a.name = j.at("name").get<std::string>();
    if (j.contains("args"))
        a.args = j.at("args").get<std::vector<AttributeArg>>();
}

inline void to_json(nlohmann::json& j, const EnumVariant& v)
{
    j["name"] = v.name;
    j["tag"] = v.tag;
    if (!v.associatedTypes.empty()) j["types"] = v.associatedTypes;
}

inline void from_json(const nlohmann::json& j, EnumVariant& v)
{
    v.name = j.at("name").get<std::string>();
    v.tag = j.at("tag").get<unsigned>();
    if (j.contains("types"))
        v.associatedTypes = j.at("types").get<std::vector<Type>>();
}

inline void to_json(nlohmann::json& j, const MacroParameter& p)
{
    j["fragment"] = djlib::fragmentTypeToString(p.fragmentType);
    j["name"] = p.name.token_name;
    if (p.isLocal) j["local"] = true;
    if (!p.literalToken.empty()) j["literalToken"] = p.literalToken;
}

inline void from_json(const nlohmann::json& j, MacroParameter& p)
{
    p.fragmentType = djlib::stringToFragmentType(j.at("fragment").get<std::string>());
    p.name = SourceIdentifier(j.at("name").get<std::string>());
    p.isLocal = j.value("local", false);
    p.literalToken = j.value("literalToken", std::string{});
}

inline void to_json(nlohmann::json& j, const MacroRule& r)
{
    j["params"] = r.parameters;
    j["tokens"] = r.bodyTokens;
    if (r.isVoid) j["void"] = true;
}

inline void from_json(const nlohmann::json& j, MacroRule& r)
{
    r.parameters = j.at("params").get<std::vector<MacroParameter>>();
    r.bodyTokens = j.at("tokens").get<std::vector<Token>>();
    r.isVoid = j.value("void", false);
}

inline void to_json(nlohmann::json& j, const PropertySymbol& p)
{
    j["name"] = p.name;
    j["type"] = p.type;
    if (p.hasGetter) j["getter"] = true;
    if (p.hasSetter) j["setter"] = true;
}

inline void from_json(const nlohmann::json& j, PropertySymbol& p)
{
    p.name = j.at("name").get<std::string>();
    p.type = j.at("type").get<Type>();
    p.hasGetter = j.value("getter", false);
    p.hasSetter = j.value("setter", false);
}

inline void to_json(nlohmann::json& j, const FieldSymbol& f)
{
    j["name"] = f.name;
    j["type"] = f.type;
    if (f.isMutable) j["mut"] = true;
    if (f.isConstant) j["const"] = true;
}

inline void from_json(const nlohmann::json& j, FieldSymbol& f)
{
    auto name = j.at("name").get<std::string>();
    auto type = j.at("type").get<Type>();
    bool mut = j.value("mut", false);
    bool constant = j.value("const", false);
    f = FieldSymbol(SymbolKind::Struct, name, type, {}, mut, constant);
}

// ============================================================================
// Compound serializers (use nlohmann::json explicitly, stay in djlib namespace)
// ============================================================================

namespace djlib
{
    inline nlohmann::json serializeMethod(const MethodSymbol& m)
    {
        nlohmann::json j;
        j["name"] = m.name;
        j["returnType"] = m.returnType;
        j["structName"] = m.structName;

        nlohmann::json params = nlohmann::json::array();
        for (size_t i = 0; i < m.paramTypes.size(); ++i)
        {
            nlohmann::json p;
            p["type"] = m.paramTypes[i];
            p["name"] = m.paramNames[i];
            if (i < m.paramAttributes.size() && !m.paramAttributes[i].empty())
                p["attrs"] = m.paramAttributes[i];
            params.push_back(std::move(p));
        }
        j["params"] = std::move(params);

        if (!m.genericParams.empty()) j["genericParams"] = m.genericParams;
        if (!m.attributes.empty()) j["attrs"] = m.attributes;
        if (m.isAbstract) j["abstract"] = true;
        if (m.isStatic) j["static"] = true;
        if (m.isConstructor) j["constructor"] = true;
        if (m.isAsync) j["async"] = true;
        if (m.isOperator)
        {
            j["operator"] = true;
            j["operatorName"] = m.operatorCanonicalName;
        }
        if (!m.variadicName.empty()) j["variadic"] = m.variadicName;
        return j;
    }

    inline std::shared_ptr<MethodSymbol> deserializeMethod(const nlohmann::json& j)
    {
        auto returnType = j.at("returnType").get<Type>();
        auto name = j.at("name").get<std::string>();
        auto method = std::make_shared<MethodSymbol>(name, returnType);
        method->structName = j.value("structName", std::string{});

        for (const auto& p : j.at("params"))
        {
            std::vector<AttributeSymbol> attrs;
            if (p.contains("attrs"))
                attrs = p.at("attrs").get<std::vector<AttributeSymbol>>();
            method->addParameter(
                p.at("name").get<std::string>(),
                p.at("type").get<Type>(),
                std::move(attrs)
            );
        }

        if (j.contains("genericParams"))
        {
            for (const auto& gp : j.at("genericParams").get<std::vector<GenericParamInfo>>())
                method->genericParams.push_back(gp);
        }
        if (j.contains("attrs"))
            method->attributes = j.at("attrs").get<std::vector<AttributeSymbol>>();

        method->isAbstract = j.value("abstract", false);
        method->isStatic = j.value("static", false);
        method->isConstructor = j.value("constructor", false);
        method->isAsync = j.value("async", false);
        method->isOperator = j.value("operator", false);
        method->operatorCanonicalName = j.value("operatorName", std::string{});
        method->variadicName = j.value("variadic", std::string{});
        return method;
    }

    inline nlohmann::json serializeFunction(const FunctionSymbol& f)
    {
        nlohmann::json j;
        j["name"] = f.name;
        j["returnType"] = f.returnType;

        nlohmann::json params = nlohmann::json::array();
        for (size_t i = 0; i < f.paramTypes.size(); ++i)
        {
            nlohmann::json p;
            p["type"] = f.paramTypes[i];
            p["name"] = f.paramNames[i];
            if (i < f.paramMutable.size() && f.paramMutable[i]) p["mut"] = true;
            if (i < f.paramAttributes.size() && !f.paramAttributes[i].empty())
                p["attrs"] = f.paramAttributes[i];
            params.push_back(std::move(p));
        }
        j["params"] = std::move(params);

        if (f.isVariadic) j["variadic"] = true;
        if (f.isAsync) j["async"] = true;
        if (f.constEval) j["consteval"] = true;
        if (f.constExpr) j["constexpr"] = true;
        return j;
    }

    inline std::shared_ptr<FunctionSymbol> deserializeFunction(const nlohmann::json& j)
    {
        auto returnType = j.at("returnType").get<Type>();
        auto name = j.at("name").get<std::string>();
        auto func = std::make_shared<FunctionSymbol>(name, returnType);

        for (const auto& p : j.at("params"))
        {
            std::vector<AttributeSymbol> attrs;
            if (p.contains("attrs"))
                attrs = p.at("attrs").get<std::vector<AttributeSymbol>>();
            func->addParameter(
                p.at("name").get<std::string>(),
                p.at("type").get<Type>(),
                p.value("mut", false),
                std::move(attrs)
            );
        }

        func->isVariadic = j.value("variadic", false);
        func->isAsync = j.value("async", false);
        func->constEval = j.value("consteval", false);
        func->constExpr = j.value("constexpr", false);
        return func;
    }

    inline nlohmann::json serializeExternFunction(const ExternFunctionSymbol& f)
    {
        auto j = serializeFunction(f);
        j["abi"] = f.abi;
        j["extern"] = true;
        return j;
    }

    inline std::shared_ptr<ExternFunctionSymbol> deserializeExternFunction(const nlohmann::json& j)
    {
        auto returnType = j.at("returnType").get<Type>();
        auto name = j.at("name").get<std::string>();
        auto func = std::make_shared<ExternFunctionSymbol>(name, returnType);

        for (const auto& p : j.at("params"))
        {
            std::vector<AttributeSymbol> attrs;
            if (p.contains("attrs"))
                attrs = p.at("attrs").get<std::vector<AttributeSymbol>>();
            func->addParameter(
                p.at("name").get<std::string>(),
                p.at("type").get<Type>(),
                p.value("mut", false),
                std::move(attrs)
            );
        }

        func->isVariadic = j.value("variadic", false);
        func->abi = j.value("abi", std::string{"C"});
        return func;
    }

    inline nlohmann::json serializeStruct(const StructSymbol& s)
    {
        nlohmann::json j;
        j["name"] = s.name;
        j["fields"] = s.fields;
        if (!s.genericParams.empty()) j["genericParams"] = s.genericParams;
        if (!s.implements.empty()) j["implements"] = s.implements;
        if (!s.attributes.empty()) j["attrs"] = s.attributes;
        if (s.baseType) j["baseType"] = *s.baseType;

        if (!s.methods.empty())
        {
            nlohmann::json methods = nlohmann::json::array();
            for (const auto& m : s.methods)
                methods.push_back(serializeMethod(*m));
            j["methods"] = std::move(methods);
        }

        if (!s.properties.empty())
        {
            nlohmann::json props = nlohmann::json::array();
            for (const auto& p : s.properties)
                props.push_back(*p);
            j["properties"] = std::move(props);
        }

        return j;
    }

    inline std::shared_ptr<StructSymbol> deserializeStruct(const nlohmann::json& j)
    {
        auto name = j.at("name").get<std::string>();
        auto sym = std::make_shared<StructSymbol>(name);
        sym->fields = j.at("fields").get<std::vector<FieldSymbol>>();

        if (j.contains("genericParams"))
        {
            for (const auto& gp : j.at("genericParams").get<std::vector<GenericParamInfo>>())
                sym->genericParams.push_back(gp);
        }
        if (j.contains("implements"))
            sym->implements = j.at("implements").get<std::vector<std::string>>();
        if (j.contains("attrs"))
            sym->attributes = j.at("attrs").get<std::vector<AttributeSymbol>>();
        if (j.contains("baseType"))
            sym->baseType = std::make_unique<Type>(j.at("baseType").get<Type>());

        if (j.contains("methods"))
        {
            for (const auto& mj : j.at("methods"))
                sym->methods.push_back(deserializeMethod(mj));
        }

        if (j.contains("properties"))
        {
            for (const auto& pj : j.at("properties"))
            {
                auto propName = pj.at("name").get<std::string>();
                auto propType = pj.at("type").get<Type>();
                bool getter = pj.value("getter", false);
                bool setter = pj.value("setter", false);
                sym->properties.push_back(std::make_shared<PropertySymbol>(
                    propName, propType, getter, setter));
            }
        }

        return sym;
    }

    inline nlohmann::json serializeEnum(const EnumSymbol& e)
    {
        nlohmann::json j;
        j["name"] = e.name;
        j["variants"] = e.variants;
        if (!e.genericParams.empty()) j["genericParams"] = e.genericParams;
        return j;
    }

    inline std::shared_ptr<EnumSymbol> deserializeEnum(const nlohmann::json& j)
    {
        auto name = j.at("name").get<std::string>();
        auto sym = std::make_shared<EnumSymbol>(name);
        sym->variants = j.at("variants").get<std::vector<EnumVariant>>();
        if (j.contains("genericParams"))
        {
            for (const auto& gp : j.at("genericParams").get<std::vector<GenericParamInfo>>())
                sym->genericParams.push_back(gp);
        }
        return sym;
    }

    inline nlohmann::json serializeInterface(const InterfaceSymbol& i)
    {
        nlohmann::json j;
        j["name"] = i.name;
        if (!i.genericParams.empty()) j["genericParams"] = i.genericParams;
        if (!i.methods.empty())
        {
            nlohmann::json methods = nlohmann::json::array();
            for (const auto& m : i.methods)
                methods.push_back(serializeMethod(*m));
            j["methods"] = std::move(methods);
        }
        return j;
    }

    inline std::shared_ptr<InterfaceSymbol> deserializeInterface(const nlohmann::json& j)
    {
        auto name = j.at("name").get<std::string>();
        auto sym = std::make_shared<InterfaceSymbol>(name);
        if (j.contains("genericParams"))
        {
            for (const auto& gp : j.at("genericParams").get<std::vector<GenericParamInfo>>())
                sym->genericParams.push_back(gp);
        }
        if (j.contains("methods"))
        {
            for (const auto& mj : j.at("methods"))
                sym->methods.push_back(deserializeMethod(mj));
        }
        return sym;
    }
} // namespace djlib

#endif //DJINN_DJLIB_FORMAT_H
