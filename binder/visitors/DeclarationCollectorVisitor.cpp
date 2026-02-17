//
// Declaration Collector Visitor Implementation
//

#include "DeclarationCollectorVisitor.h"
#include "../Binder.h"
#include "../../parser/ast/Declaration.h"

namespace djinn
{
    void DeclarationCollectorVisitor::visit(const StructDeclaration& decl, const std::string& prefix)
    {
        _binder.collectStruct(decl, prefix);
    }

    void DeclarationCollectorVisitor::visit(const FunctionDeclaration& decl, const std::string& prefix)
    {
        // FunctionDeclaration requires mutable access due to body ownership transfer
        _binder.collectFunctionWithPrefix(const_cast<FunctionDeclaration&>(decl), prefix);
    }

    void DeclarationCollectorVisitor::visit(const EnumDeclaration& decl, const std::string& prefix)
    {
        _binder.collectEnumWithPrefix(decl, prefix);
    }

    void DeclarationCollectorVisitor::visit(const InterfaceDeclaration& decl, const std::string& prefix)
    {
        _binder.collectInterfaceWithPrefix(decl, prefix);
    }

    void DeclarationCollectorVisitor::visit(const ExternFunctionDeclaration& decl, const std::string& prefix)
    {
        _binder.collectExternFunction(decl, prefix);
    }

    void DeclarationCollectorVisitor::visit(const NamespaceDeclaration& decl, const std::string& prefix)
    {
        _binder.collectNamespace(decl, prefix);
    }

    void DeclarationCollectorVisitor::visit(const ImportDeclaration&/*decl*/, const std::string&/*prefix*/)
    {
        // Imports are handled separately via processImports
    }
} // namespace djinn