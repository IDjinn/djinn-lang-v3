//
// Program Binder Visitor Implementation
//

#include "ProgramBinderVisitor.h"
#include "../Binder.h"
#include "../../parser/ast/Declaration.h"

namespace djinn {
    void ProgramBinderVisitor::visit(const StructDeclaration &decl, const std::string &prefix) {
        _binder.validateStructFieldTypes(decl);

        for (const auto &method: decl.methods) {
            _binder.bindMethod(*method, decl);
        }
    }

    void ProgramBinderVisitor::visit(const FunctionDeclaration &decl, const std::string &prefix) {
        _binder.bindFunction(decl);
    }

    void ProgramBinderVisitor::visit(const EnumDeclaration &decl, const std::string &prefix) {
        // Enums are fully collected in the collection phase, no binding needed
    }

    void ProgramBinderVisitor::visit(const InterfaceDeclaration &decl, const std::string &prefix) {
        // Interfaces are fully collected in the collection phase, no binding needed
    }

    void ProgramBinderVisitor::visit(const ExternFunctionDeclaration &decl, const std::string &prefix) {
        _binder.validateExternFunctionTypes(decl);
    }

    void ProgramBinderVisitor::visit(const NamespaceDeclaration &decl, const std::string &prefix) {
        _binder.bindNamespace(decl, prefix);
    }

    void ProgramBinderVisitor::visit(const ImportDeclaration &decl, const std::string &prefix) {
        // Imports are handled separately via processImports
    }
} // namespace djinn
