//
// Created by Claude on 05/01/2026.
//

#include "Binder.h"

bool Binder::resolveType(const Type &type) {
    return isTypeDefined(type);
}

bool Binder::isTypeDefined(const Type &type) {
    switch (type.kind) {
        case TypeKind::INTEGER:
        case TypeKind::STRING:
        case TypeKind::VOID:
        case TypeKind::F16:
        case TypeKind::F32:
        case TypeKind::F64:
        case TypeKind::F128:
        case TypeKind::AUTO:
            return true;

        case TypeKind::STRUCT:
            // Check both interface and struct - interface takes priority for type usage
            // Struct is only used for instantiation
            if (_global_scope->lookupInterface(type.structName) != nullptr) {
                return true;
            }
            return _global_scope->lookupStruct(type.structName) != nullptr;

        case TypeKind::ARRAY:
        case TypeKind::POINTER:
            if (type.elementType) {
                return isTypeDefined(*type.elementType);
            }
            return false;

        default:
            return false;
    }
}