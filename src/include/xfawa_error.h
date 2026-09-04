#ifndef XFAWA_ERROR_H
#define XFAWA_ERROR_H

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>
#include <cmath>

namespace xfawa {

// Levenshtein Distance Algorithm
inline int levenshteinDistance(const std::string& s1, const std::string& s2) {
    int m = static_cast<int>(s1.length());
    int n = static_cast<int>(s2.length());
    
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));
    
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;
    
    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            if (s1[static_cast<size_t>(i - 1)] == s2[static_cast<size_t>(j - 1)]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }
    
    return dp[m][n];
}

// Find similar candidates
inline std::vector<std::string> findSuggestions(const std::string& input, 
                                                  const std::vector<std::string>& candidates,
                                                  int maxDistance = 3,
                                                  int maxSuggestions = 3) {
    std::vector<std::pair<int, std::string>> distances;
    
    for (const auto& candidate : candidates) {
        int dist = levenshteinDistance(input, candidate);
        if (dist <= maxDistance) {
            distances.push_back({dist, candidate});
        }
    }
    
    std::sort(distances.begin(), distances.end());
    
    std::vector<std::string> suggestions;
    int count = 0;
    for (const auto& p : distances) {
        if (count >= maxSuggestions) break;
        suggestions.push_back(p.second);
        count++;
    }
    
    return suggestions;
}

enum class ErrorType {
    SYNTAX,
    MOD,
    CONFIG
};

enum class ErrorSeverity {
    ERR,
    WARN
};

struct SourcePosition {
    std::string filename;
    int line;
    int column;
    
    SourcePosition(const std::string& file = "", int l = 0, int c = 0)
        : filename(file), line(l), column(c) {}
    
    std::string toString() const {
        if (filename.empty()) {
            return "line " + std::to_string(line) + ", column " + std::to_string(column);
        }
        return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
    }
};

struct Diagnostic {
    ErrorType type;
    ErrorSeverity severity;
    SourcePosition position;
    std::string message;
    std::vector<std::string> suggestions;
    
    Diagnostic(ErrorType t, ErrorSeverity s, const SourcePosition& pos, const std::string& msg)
        : type(t), severity(s), position(pos), message(msg) {}
    
    Diagnostic(ErrorType t, ErrorSeverity s, const SourcePosition& pos, const std::string& msg,
               const std::vector<std::string>& suggs)
        : type(t), severity(s), position(pos), message(msg), suggestions(suggs) {}
    
    std::string toString(bool showType = true) const {
        std::ostringstream oss;
        
        if (severity == ErrorSeverity::ERR) {
            oss << "[error";
        } else {
            oss << "[warning";
        }
        
        if (showType) {
            oss << ":" << typeToString();
        }
        
        oss << "] ";
        
        if (position.line > 0) {
            oss << position.toString() << ": ";
        }
        
        oss << message;
        
        // Add suggestions
        if (!suggestions.empty()) {
            oss << "\n  Did you mean:\n";
            for (size_t i = 0; i < suggestions.size(); i++) {
                oss << "    " << (i + 1) << ". " << suggestions[i];
                if (i < suggestions.size() - 1) oss << "\n";
            }
        }
        
        return oss.str();
    }
    
private:
    std::string typeToString() const {
        switch (type) {
            case ErrorType::SYNTAX: return "syntax";
            case ErrorType::MOD: return "mod";
            case ErrorType::CONFIG: return "config";
            default: return "unknown";
        }
    }
};

class ErrorSystem {
private:
    std::vector<Diagnostic> diagnostics;
    std::string currentFile;
    bool showWarningTypes;
    bool warningsEnabled;
    int errorCount;
    int warningCount;
    
public:
    ErrorSystem() : showWarningTypes(true), warningsEnabled(true), errorCount(0), warningCount(0) {}
    
    void setCurrentFile(const std::string& file) {
        currentFile = file;
    }
    
    const std::string& getCurrentFile() const {
        return currentFile;
    }
    
    void setShowWarningTypes(bool show) {
        showWarningTypes = show;
    }
    
    void setWarningsEnabled(bool enabled) {
        warningsEnabled = enabled;
    }
    
    void addError(ErrorType type, int line, int column, const std::string& message) {
        SourcePosition pos(currentFile, line, column);
        diagnostics.push_back(Diagnostic(type, ErrorSeverity::ERR, pos, message));
        errorCount++;
    }
    
    void addError(ErrorType type, const std::string& message) {
        SourcePosition pos(currentFile, 0, 0);
        diagnostics.push_back(Diagnostic(type, ErrorSeverity::ERR, pos, message));
        errorCount++;
    }
    
    void addErrorWithSuggestions(ErrorType type, int line, int column, 
                                  const std::string& message,
                                  const std::vector<std::string>& suggestions) {
        SourcePosition pos(currentFile, line, column);
        diagnostics.push_back(Diagnostic(type, ErrorSeverity::ERR, pos, message, suggestions));
        errorCount++;
    }
    
    void addErrorWithSuggestions(ErrorType type, const std::string& message,
                                  const std::vector<std::string>& suggestions) {
        SourcePosition pos(currentFile, 0, 0);
        diagnostics.push_back(Diagnostic(type, ErrorSeverity::ERR, pos, message, suggestions));
        errorCount++;
    }
    
    void addWarning(ErrorType type, int line, int column, const std::string& message) {
        if (!warningsEnabled) return;
        
        SourcePosition pos(currentFile, line, column);
        diagnostics.push_back(Diagnostic(type, ErrorSeverity::WARN, pos, message));
        warningCount++;
    }
    
    void addWarning(ErrorType type, const std::string& message) {
        if (!warningsEnabled) return;
        
        SourcePosition pos(currentFile, 0, 0);
        diagnostics.push_back(Diagnostic(type, ErrorSeverity::WARN, pos, message));
        warningCount++;
    }
    
    void addSyntaxError(int line, int column, const std::string& message) {
        addError(ErrorType::SYNTAX, line, column, message);
    }
    
    void addSyntaxWarning(int line, int column, const std::string& message) {
        addWarning(ErrorType::SYNTAX, line, column, message);
    }
    
    void addModError(int line, int column, const std::string& message) {
        addError(ErrorType::MOD, line, column, message);
    }
    
    void addModWarning(int line, int column, const std::string& message) {
        addWarning(ErrorType::MOD, line, column, message);
    }
    
    void addConfigError(int line, int column, const std::string& message) {
        addError(ErrorType::CONFIG, line, column, message);
    }
    
    void addConfigWarning(int line, int column, const std::string& message) {
        addWarning(ErrorType::CONFIG, line, column, message);
    }
    
    bool hasErrors() const {
        return errorCount > 0;
    }
    
    bool hasWarnings() const {
        return warningCount > 0;
    }
    
    int getErrorCount() const {
        return errorCount;
    }
    
    int getWarningCount() const {
        return warningCount;
    }
    
    const std::vector<Diagnostic>& getDiagnostics() const {
        return diagnostics;
    }
    
    void printDiagnostics(std::ostream& os = std::cerr) const {
        for (const auto& diag : diagnostics) {
            if (diag.severity == ErrorSeverity::WARN) {
                os << diag.toString(showWarningTypes) << std::endl;
            } else {
                os << diag.toString(true) << std::endl;
            }
        }
    }
    
    void clear() {
        diagnostics.clear();
        errorCount = 0;
        warningCount = 0;
    }
    
    std::vector<std::string> getErrorMessages() const {
        std::vector<std::string> messages;
        for (const auto& diag : diagnostics) {
            if (diag.severity == ErrorSeverity::ERR) {
                messages.push_back(diag.toString(true));
            }
        }
        return messages;
    }
    
    std::vector<std::string> getWarningMessages() const {
        std::vector<std::string> messages;
        for (const auto& diag : diagnostics) {
            if (diag.severity == ErrorSeverity::WARN) {
                messages.push_back(diag.toString(showWarningTypes));
            }
        }
        return messages;
    }
    
    std::vector<std::string> getAllMessages() const {
        std::vector<std::string> messages;
        for (const auto& diag : diagnostics) {
            if (diag.severity == ErrorSeverity::WARN) {
                messages.push_back(diag.toString(showWarningTypes));
            } else {
                messages.push_back(diag.toString(true));
            }
        }
        return messages;
    }
};

class ErrorReporter {
private:
    static ErrorSystem* instance;
    
public:
    static ErrorSystem& get() {
        if (!instance) {
            instance = new ErrorSystem();
        }
        return *instance;
    }
    
    static void initialize() {
        if (instance) {
            delete instance;
        }
        instance = new ErrorSystem();
    }
    
    static void cleanup() {
        if (instance) {
            delete instance;
            instance = nullptr;
        }
    }
};

}

#endif
