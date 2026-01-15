//
// Function, method and namespace binding
//

#include "../Binder.h"

void Binder::bindProgram(const Program &program) {
    for (const auto &ext: program.externFunctions) {
        if (!isTypeDefined(*ext->returnType)) {
            if (ext->returnType->kind == TypeKind::STRUCT) {
                BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + ext->returnType->structName + "'",
                             ext, ext->name.location);
            }
        }
        for (const auto &param: ext->parameters) {
            if (!isTypeDefined(*param.type)) {
                if (param.type->kind == TypeKind::STRUCT) {
                    BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + param.type->structName + "'",
                                 param, param.name.location);
                }
            }
        }
    }

    for (const auto &struc: program.structs) {
        for (const auto &field: struc->fields) {
            if (!isTypeDefined(*field.type)) {
                if (field.type->kind == TypeKind::STRUCT) {
                    if (struc->genericParams.find(field.type->structName) == nullptr) {
                        BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                                     "undefined struct '" + field.type->structName + "'", field, field.name.location);
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
    currentFunction_ = func.name.token_name;

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

    if (func.body) {
        bindBlock(*func.body);
    }

    popScope();
    currentFunction_.clear();
}

void Binder::bindMethod(const StructMethodDeclaration &method, const StructDeclaration &struc) {
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

    if (!isTypeDefined(*method.returnType) && !is_generic_type(*method.returnType, struc)) {
        if (method.returnType->kind == TypeKind::STRUCT) {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + method.returnType->structName + "'",
                         method, method.name.location);
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
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    for (const auto &struc: ns.structs) {
        for (const auto &field: struc->fields) {
            if (!isTypeDefined(*field.type)) {
                if (field.type->kind == TypeKind::STRUCT) {
                    if (struc->genericParams.find(field.type->structName) == nullptr) {
                        BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                                     "undefined struct '" + field.type->structName + "'", field, field.name.location);
                    }
                }
            }
        }
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