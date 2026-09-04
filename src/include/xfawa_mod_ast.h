#ifndef XFAWA_MOD_AST_H
#define XFAWA_MOD_AST_H

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace xfawa {

struct ModSyntax {
    std::string name;
    std::string syntax;
    std::string logic;
};

struct ModSyntaxModification {
    std::string original;
    std::string replacement;
};

struct ModFunction {
    std::string name;
    std::vector<std::string> statements;
};

struct ModCodeBlock {
    std::string name;
    std::vector<ModFunction> functions;
    std::vector<ModSyntaxModification> modifications;
    std::vector<ModSyntax> additions;
};

struct ModFile {
    std::vector<ModCodeBlock> codeBlocks;
};

}

#endif