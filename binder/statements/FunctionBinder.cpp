//
// Function, method and namespace binding - uses visitor pattern
//

#include <assert.h>

#include "../Binder.h"
#include "../visitors/ProgramBinderVisitor.h"

void Binder::bindProgram(const Program &program) {
    djinn::ProgramBinderVisitor visitor(*this);

    for (const auto &ext: program.externFunctions) {
        ext->accept(visitor, "");
    }

    for (const auto &struc: program.structs) {
        struc->accept(visitor, program.fileNamespace);
    }

    for (const auto &func: program.functions) {
        func->accept(visitor, program.fileNamespace);
    }

    for (const auto &ns: program.namespaces) {
        ns->accept(visitor, "");
    }
}

void Binder::bindFunction(const FunctionDeclaration &func, const std::string &prefix) {
    // Build qualified name same as in collectFunctionWithPrefix
    const std::string qualifiedName = (func.name.token_name == "main" || prefix.empty())
                                          ? func.name.token_name
                                          : prefix + "::" + func.name.token_name;
    currentFunction_ = qualifiedName;

    pushScope();

    for (const auto &param: func.parameters) {
        if (!isTypeDefined(*param.type) && param.type->kind == TypeKind::STRUCT) {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + param.type->structName + "'", param,
                         param.name.location);
        }

        if (!_current_scope->defineParameter(param.name.token_name, *param.type, param.isMutable)) {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "parameter '" + param.name.token_name + "' is already defined", param, param.name.location);
        }
    }

    if (!isTypeDefined(*func.returnType)) {
        if (func.returnType->kind == TypeKind::STRUCT) {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + func.returnType->structName + "'",
                         func, func.name.location);
        }
    }

    // Get the body from the FunctionSymbol (ownership was transferred during collection)
    const auto funcSym = _global_scope->lookupFunction(qualifiedName);
    if (funcSym && funcSym->body) {
        bindBlock(*funcSym->body);
    }

    popScope();
    currentFunction_.clear();
}

void Binder::bindMethod(StructMethodDeclaration &method, const StructDeclaration &struc) {
    currentFunction_ = struc.name.token_name + "::" + method.name.token_name;

    pushScope();

    // Define 'this' as pointer to struct type (fields accessed via this.fieldName)
    Type thisType;
    thisType.kind = TypeKind::POINTER;
    thisType.elementType = std::make_unique<Type>();
    thisType.elementType->kind = TypeKind::STRUCT;
    thisType.elementType->structName = struc.name.token_name;
    _current_scope->defineVariable("this", thisType, false);

    for (const auto &param: method.parameters) {
        if (!isTypeDefined(*param.type) && !is_generic_type(*param.type, struc)) {
            if (param.type->kind == TypeKind::STRUCT) {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + param.type->structName + "'",
                             param, param.name.location);
            }
        }
        if (!_current_scope->defineParameter(param.name.token_name, *param.type, param.isMutable)) {
            BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION,
                         "parameter '" + param.name.token_name + "' is already defined", param, param.name.location);
        }
    }

    auto returnType = resolveType(*method.returnType);
    if (!returnType) {
        if (!is_generic_type(*method.returnType, struc)) {
            if (method.returnType->kind == TypeKind::STRUCT) {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                             "undefined struct '" + method.returnType->structName + "'",
                             method, method.name.location);
            }
        }
        // Keep original method.returnType for generic types or error recovery
    } else {
        method.returnType = std::move(returnType);
    }
    if (method.body) {
        bindBlock(*method.body);
    } else if (method.expression) {
        bindExpression(*method.expression);
    }

    const auto methodSymbol = _current_scope->lookupMethod(method.name.token_name);
    assert(methodSymbol && "Method symbol not found in scope table");
    methodSymbol->returnType = *method.returnType.get();

    popScope();
    currentFunction_.clear();
}

void Binder::bindNamespace(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    djinn::ProgramBinderVisitor visitor(*this);

    for (const auto &struc: ns.structs) {
        struc->accept(visitor, qualifiedPrefix);
    }

    for (const auto &func: ns.functions) {
        func->accept(visitor, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        nestedNs->accept(visitor, qualifiedPrefix);
    }
}

void Binder::validateExternFunctionTypes(const ExternFunctionDeclaration &decl) {
    if (!isTypeDefined(*decl.returnType)) {
        if (decl.returnType->kind == TypeKind::STRUCT) {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                         "undefined struct '" + decl.returnType->structName + "'",
                         &decl, decl.name.location);
        }
    }
    for (const auto &param: decl.parameters) {
        if (!isTypeDefined(*param.type)) {
            if (param.type->kind == TypeKind::STRUCT) {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                             "undefined struct '" + param.type->structName + "'",
                             param, param.name.location);
            }
        }
    }
}

void Binder::validateStructFieldTypes(const StructDeclaration &decl) {
    for (const auto &field: decl.fields) {
        if (!isTypeDefined(*field.type)) {
            if (field.type->kind == TypeKind::STRUCT) {
                if (decl.genericParams.find(field.type->structName) == nullptr) {
                    BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                                 "undefined struct '" + field.type->structName + "'",
                                 field, field.name.location);
                }
            }
        }
    }
}