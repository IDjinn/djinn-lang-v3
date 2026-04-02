//
// Program Binder Visitor - Binds program declarations (validation + method/function binding)
//

#ifndef DJINN_PROGRAM_BINDER_VISITOR_H
#define DJINN_PROGRAM_BINDER_VISITOR_H

#include "../../visitor/DeclarationVisitor.h"

class Binder;

namespace djinn
{
    class ProgramBinderVisitor : public IDeclarationVisitor
    {
        Binder& _binder;

    public:
        explicit ProgramBinderVisitor(Binder& binder) : _binder(binder)
        {
        }

        void visit(const StructDeclaration& decl, const std::string& prefix) override;

        void visit(const FunctionDeclaration& decl, const std::string& prefix) override;

        void visit(const EnumDeclaration& decl, const std::string& prefix) override;

        void visit(const InterfaceDeclaration& decl, const std::string& prefix) override;

        void visit(const ExternFunctionDeclaration& decl, const std::string& prefix) override;

        void visit(const NamespaceDeclaration& decl, const std::string& prefix) override;

        void visit(const ImportDeclaration& decl, const std::string& prefix) override;

        void visit(const ImplDeclaration& decl, const std::string& prefix) override;

        void visit(const MacroDeclaration& decl, const std::string& prefix) override;
    };
} // namespace djinn

#endif // DJINN_PROGRAM_BINDER_VISITOR_H