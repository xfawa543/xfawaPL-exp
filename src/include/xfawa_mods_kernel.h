#ifndef XFAWA_MODS_KERNEL_H
#define XFAWA_MODS_KERNEL_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

namespace xfawa {

enum class ModActionType {
    MODIFY,
    ADD_SYNTAX,
    DELETE_SYNTAX
};

struct ModAction {
    ModActionType type;
    std::string from;
    std::string to;
    std::string syntax_name;
    std::string syntax_definition;
    
    ModAction(ModActionType t, const std::string& f = "", const std::string& to_str = "")
        : type(t), from(f), to(to_str) {}
};

class ModsKernel {
private:
    std::unordered_map<std::string, std::string> modifications;
    std::vector<std::pair<std::string, std::string>> added_syntax;
    std::vector<std::string> errors;
    
public:
    ModsKernel();
    
    void loadModFile(const std::string& filepath);
    void parseModContent(const std::string& content);
    
    void applyModifications(std::string& source);
    
    bool hasModifications() const;
    
    bool hasErrors() const { return !errors.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    
    void clear();
    
private:
    void parseModification(const std::string& line);
    void parseAddSyntax(const std::string& line);
    
    std::vector<std::string> split(const std::string& s, char delimiter);
    std::string trim(const std::string& s);
};

}

#endif