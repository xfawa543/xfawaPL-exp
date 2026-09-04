#ifndef XFAWA_CONFIG_H
#define XFAWA_CONFIG_H

#include <string>

namespace xfawa {

enum class OptimizationLevel {
    O0 = 0,
    O1 = 1,
    O2 = 2,
    O3 = 3
};

enum class LogLanguage {
    EN,
    ZH
};

struct CompilerConfig {
    bool debug_info = false;
    bool warnings = true;
    bool emit_ll = false;
    bool emit_asm = false;
    bool show_warning_types = true;
    bool xraphics_log = false;
    OptimizationLevel opt_level = OptimizationLevel::O2;
    LogLanguage log_language = LogLanguage::EN;
    std::string output_dir = "bin";
    std::string intermediate_dir = "build";
};

class ConfigLoader {
public:
    static CompilerConfig load(const std::string& start_path = "");
    static CompilerConfig loadFromFile(const std::string& filepath);
    static std::string findConfigFile(const std::string& start_path);
    static void save(const std::string& filepath, const CompilerConfig& config);

private:
    static std::string searchFromDirectory(const std::string& dir);
    static std::string searchUpwards(const std::string& start_path);
    static std::string searchInstallDir();
    static void parseLine(const std::string& line, CompilerConfig& config);
    static std::string trim(const std::string& s);
};

bool isValidPath(const std::string& path);
std::string sanitizePath(const std::string& path);

}

#endif
