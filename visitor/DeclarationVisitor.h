//
// Declaration Visitor Interface - Base for all declaration visitors
//

#ifndef DJINN_DECLARATION_VISITOR_H
#define DJINN_DECLARATION_VISITOR_H

#include <string>

// Forward declarations of all declaration types
struct StructDeclaration;
struct FunctionDeclaration;
struct EnumDeclaration;
struct InterfaceDeclaration;
struct ExternFunctionDeclaration;
struct NamespaceDeclaration;
struct ImportDeclaration;

namespace djinn {
    class IDeclarationVisitor {
    public:
        virtual ~IDeclarationVisitor() = default;

        virtual void visit(const StructDeclaration &decl, const std::string &prefix) = 0;

        virtual void visit(const FunctionDeclaration &decl, const std::string &prefix) = 0;

        virtual void visit(const EnumDeclaration &decl, const std::string &prefix) = 0;

        virtual void visit(const InterfaceDeclaration &decl, const std::string &prefix) = 0;

        virtual void visit(const ExternFunctionDeclaration &decl, const std::string &prefix) = 0;

        virtual void visit(const NamespaceDeclaration &decl, const std::string &prefix) = 0;

        virtual void visit(const ImportDeclaration &decl, const std::string &prefix) = 0;
    };

    // Convenience base class with empty implementations for selective overriding
    class DeclarationVisitorBase : public IDeclarationVisitor {
    public:
        void visit(const StructDeclaration &decl, const std::string &prefix) override {
        }

        void visit(const FunctionDeclaration &decl, const std::string &prefix) override {
        }

        void visit(const EnumDeclaration &decl, const std::string &prefix) override {
        }

        void visit(const InterfaceDeclaration &decl, const std::string &prefix) override {
        }

        void visit(const ExternFunctionDeclaration &decl, const std::string &prefix) override {
        }

        void visit(const NamespaceDeclaration &decl, const std::string &prefix) override {
        }

        void visit(const ImportDeclaration &decl, const std::string &prefix) override {
        }
    };
} // namespace djinn

#endif // DJINN_DECLARATION_VISITOR_H