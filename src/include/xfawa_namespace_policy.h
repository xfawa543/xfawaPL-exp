#ifndef XFAWA_NAMESPACE_POLICY_H
#define XFAWA_NAMESPACE_POLICY_H

#include <string>
#include <unordered_set>
#include <stdexcept>

namespace xfawa {

class NamespacePolicy {
public:
    static const std::unordered_set<std::string> RESERVED_NAMESPACES;
    static const std::string PUBLIC_NAMESPACE;
    
    static bool isReserved(const std::string& ns) {
        return RESERVED_NAMESPACES.count(ns) > 0;
    }
    
    static bool isPublic(const std::string& ns) {
        return ns == PUBLIC_NAMESPACE;
    }
    
    static bool isValidUserNamespace(const std::string& ns) {
        if (ns.empty()) return true;
        if (isReserved(ns)) return false;
        size_t colonCount = 0;
        for (char c : ns) {
            if (c == ':') colonCount++;
        }
        if (colonCount > 0) return false;
        return true;
    }
    
    static std::string getReservedNamespacesError(const std::string& ns) {
        return "Namespace '" + ns + "' is reserved and cannot be used. Reserved namespaces: core, sys, runtime, compiler, internal, builtin, std, ext, lib";
    }
    
    static std::string getInvalidNamespaceError(const std::string& ns) {
        return "Invalid namespace '" + ns + "'. Multi-level namespaces (containing ':') are not allowed.";
    }
};

const std::unordered_set<std::string> NamespacePolicy::RESERVED_NAMESPACES = {
    "core",
    "sys", 
    "runtime",
    "compiler",
    "internal",
    "builtin",
    "std",
    "ext",
    "lib"
};

const std::string NamespacePolicy::PUBLIC_NAMESPACE = "pub";

}

#endif
