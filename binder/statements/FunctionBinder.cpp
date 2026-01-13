//
// Function, method and namespace binding
//

#include "../Binder.h"

void Binder::bindProgram(const Program &program) {
    // Validate extern function types (they may use imported types)
    for (const auto &ext: program.externFunctions) {
        if (!isTypeDefined(*ext->returnType)) {
            if (ext->returnType->kind == TypeKind::STRUCT) {
                errorUndefinedStruct(ext->returnType->structName, {});
            }
        }
        for (const auto &param: ext->parameters) {
            if (!isTypeDefined(*param.type)) {
                if (param.type->kind == TypeKind::STRUCT) {
                    errorUndefinedStruct(param.type->structName, {});
                }
            }
        }
    }

    for (const auto &struc: program.structs) {
        for (const auto &field: struc->fields) {
            if (!isTypeDefined(*field.type)) {
                if (field.type->kind == TypeKind::STRUCT) {
                    if (struc->genericParams.find(field.type->structName) == nullptr) {
                        errorUndefinedStruct(field.type->structName, {});
                    }
                }
            }
        }
        // Bind struct methods
        for (const auto &method: struc->methods) {
            bindMethod(*method, *struc);
        }
    }

    for (const auto &func: program.functions) {
        bindFunction(*func);
    }

    for (const auto &ns: program.namespaces) {
        bindNamespace(*ns, "");
    }
}

void Binder::bindFunction(const FunctionDeclaration &func) {
    currentFunction_ = func.name;

    pushScope();

    for (const auto &param: func.parameters) {
        if (!isTypeDefined(*param.type) && param.type->kind == TypeKind::STRUCT) {
            errorUndefinedStruct(param.type->structName, {});
        }

        if (!_current_scope->defineParameter(param.name, *param.type, param.isMutable)) {
            errorDuplicateDefinition(param.name, SymbolKind::Parameter, {});
        }
    }

    if (!isTypeDefined(*func.returnType)) {
        if (func.returnType->kind == TypeKind::STRUCT) {
            errorUndefinedStruct(func.returnType->structName, {});
        }
    }

    if (func.body) {
        bindBlock(*func.body);
    }

    popScope();
    currentFunction_.clear();
}

void Binder::bindMethod(const StructMethodDeclaration &method, const StructDeclaration &struc) {
    currentFunction_ = struc.name + "::" + method.name;

    pushScope();

    // Define 'this' as pointer to struct type (fields accessed via this.fieldName)
    Type thisType;
    thisType.kind = TypeKind::POINTER;
    thisType.elementType = std::make_unique<Type>();
    thisType.elementType->kind = TypeKind::STRUCT;
    thisType.elementType->structName = struc.name;
    _current_scope->defineVariable("this", thisType, false);

    for (const auto &param: method.parameters) {
        if (!isTypeDefined(*param.type) && !is_generic_type(*param.type, struc)) {
            if (param.type->kind == TypeKind::STRUCT) {
                errorUndefinedStruct(param.type->structName, {});
            }
        }
        if (!_current_scope->defineParameter(param.name, *param.type, param.isMutable)) {
            errorDuplicateDefinition(param.name, SymbolKind::Parameter, {});
        }
    }

    if (!isTypeDefined(*method.returnType) && !is_generic_type(*method.returnType, struc)) {
        if (method.returnType->kind == TypeKind::STRUCT) {
            errorUndefinedStruct(method.returnType->structName, {});
        }
    }

    if (method.body) {
        bindBlock(*method.body);
    } else if (method.expression) {
        bindExpression(*method.expression);
    }

    popScope();
    currentFunction_.clear();
}

void Binder::bindNamespace(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &struc: ns.structs) {
        for (const auto &field: struc->fields) {
            if (!isTypeDefined(*field.type)) {
                if (field.type->kind == TypeKind::STRUCT) {
                    if (struc->genericParams.find(field.type->structName) == nullptr) {
                        errorUndefinedStruct(field.type->structName, {});
                    }
                }
            }
        }
        // Bind struct methods
        for (const auto &method: struc->methods) {
            bindMethod(*method, *struc);
        }
    }

    for (const auto &func: ns.functions) {
        bindFunction(*func);
    }

    for (const auto &nestedNs: ns.namespaces) {
        bindNamespace(*nestedNs, qualifiedPrefix);
    }
}