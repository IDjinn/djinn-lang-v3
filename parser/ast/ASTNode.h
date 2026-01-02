//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_ASTNODE_H
#define DJINN_ASTNODE_H

#include <ostream>

inline void writeIndent(std::ostream &os, const int indent) {
    for (int i = 0; i < indent; ++i) os.put(' ');
}

struct ASTNode {
    virtual ~ASTNode() = default;

    virtual void print(std::ostream &os, int indent = 0) const = 0;
};

inline std::ostream &operator<<(std::ostream &os, const ASTNode &node) {
    node.print(os);
    return os;
}

#endif //DJINN_ASTNODE_H
