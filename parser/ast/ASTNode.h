//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_ASTNODE_H
#define DJINN_ASTNODE_H

#include <ostream>

#include "../../diagnostics/Diagnostic.h"

inline void writeIndent(std::ostream &os, const int indent) {
    for (int i = 0; i < indent; ++i) os.put(' ');
}

struct ASTNode {
    virtual ~ASTNode() = default;

    virtual void print(std::ostream &os, int indent = 0) const = 0;
};


struct Location : ASTNode {
    SourceLocation location;

    void print(std::ostream &os, int indent = 0) const override = 0;
};

struct SourceIdentifier : Location {
    std::string token_name;

    SourceIdentifier() = default;

    explicit SourceIdentifier(std::string name) : token_name(std::move(name)) {
    }

    SourceIdentifier(std::string name, const SourceLocation location) : token_name(std::move(name)) {
        this->location = location;
    }

    void print(std::ostream &os, int indent = 0) const override {
        writeIndent(os, indent);
        os << "Identifier(" << token_name << ")";
    }
};

inline std::ostream &operator<<(std::ostream &os, const ASTNode &node) {
    node.print(os);
    return os;
}

#endif //DJINN_ASTNODE_H
