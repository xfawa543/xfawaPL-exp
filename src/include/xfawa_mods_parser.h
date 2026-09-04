#ifndef XFAWA_MODS_PARSER_H
#define XFAWA_MODS_PARSER_H

#include "xfawa_mods_lexer.h"
#include <vector>
#include <memory>
#include <string>

namespace xfawa {

class ModsParser {
private:
    std::vector<ModsToken> tokens;
    size_t current;
    std::vector<std::string> errors;
    
public:
    explicit ModsParser(const std::vector<ModsToken>& toks);
    
    bool hasErrors() const { return !errors.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    
private:
    const ModsToken& peek() const;
    const ModsToken& peek(int offset) const;
    ModsToken consume();
    bool consume(ModsTokenType type);
    bool isAtEnd() const;
    void advance();
    void addError(const std::string& message);
};

}

#endif