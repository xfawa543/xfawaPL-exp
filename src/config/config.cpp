#include "xfawa_config.h"
#include "xfawa_error.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <windows.h>
#include <regex>

namespace xfawa {

static const std::string CONFIG_FILENAME = "xfawac.xfconf";
static const std::string DEFAULT_OUTPUT_DIR = "bin";
static const std::string DEFAULT_INTERMEDIATE_DIR = "build";

static std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static LogLanguage parseLogLanguage(const std::string& value) {
    std::string lowered = toLowerAscii(value);
    if (lowered == "zh" || lowered == "zh-cn" || lowered == "cn" || lowered == "chinese") {
        return LogLanguage::ZH;
    }
    return LogLanguage::EN;
}

static const char* logLanguageToString(LogLanguage language) {
    switch (language) {
        case LogLanguage::ZH: return "zh";
        case LogLanguage::EN:
        default: return "en";
    }
}

std::string ConfigLoader::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void ConfigLoader::parseLine(const std::string& line, CompilerConfig& config) {
    std::string trimmed = trim(line);
    
    if (trimmed.empty() || trimmed[0] == '#') return;
    
    size_t eqPos = trimmed.find('=');
    if (eqPos == std::string::npos) return;
    
    std::string key = trim(trimmed.substr(0, eqPos));
    std::string value = trim(trimmed.substr(eqPos + 1));
    std::string loweredValue = toLowerAscii(value);
    
    if (key == "debug_info") config.debug_info = (loweredValue == "true" || value == "1");
    else if (key == "warnings") config.warnings = (loweredValue == "true" || value == "1");
    else if (key == "emit_ll") config.emit_ll = (loweredValue == "true" || value == "1");
    else if (key == "emit_asm") config.emit_asm = (loweredValue == "true" || value == "1");
    else if (key == "show_warning_types") config.show_warning_types = (loweredValue == "true" || value == "1");
    else if (key == "xraphics_log") config.xraphics_log = (loweredValue == "true" || value == "1");
    else if (key == "optimization_level" || key == "opt_level") {
        if (value == "0" || loweredValue == "o0") config.opt_level = OptimizationLevel::O0;
        else if (value == "1" || loweredValue == "o1") config.opt_level = OptimizationLevel::O1;
        else if (value == "2" || loweredValue == "o2") config.opt_level = OptimizationLevel::O2;
        else if (value == "3" || loweredValue == "o3") config.opt_level = OptimizationLevel::O3;
    }
    else if (key == "output_dir") config.output_dir = value;
    else if (key == "intermediate_dir") config.intermediate_dir = value;
    else if (key == "log_language" || key == "language" || key == "log_lang") config.log_language = parseLogLanguage(value);
}

std::string ConfigLoader::searchFromDirectory(const std::string& dir) {
    std::string filepath = dir + "/" + CONFIG_FILENAME;
    if (std::filesystem::exists(filepath)) {
        return filepath;
    }
    return "";
}

std::string ConfigLoader::searchUpwards(const std::string& start_path) {
    std::filesystem::path current = start_path;
    std::filesystem::path absolute = std::filesystem::absolute(current);
    
    for (int i = 0; i < 10 && !absolute.empty(); ++i) {
        std::string result = searchFromDirectory(absolute.string());
        if (!result.empty()) return result;
        absolute = absolute.parent_path();
    }
    return "";
}

std::string ConfigLoader::searchInstallDir() {
    char buffer[4096];
    if (GetModuleFileNameA(NULL, buffer, sizeof(buffer)) == 0) {
        return "";
    }
    std::filesystem::path exePath = buffer;
    std::filesystem::path installDir = exePath.parent_path();
    return searchFromDirectory(installDir.string());
}

std::string ConfigLoader::findConfigFile(const std::string& start_path) {
    std::string result;
    
    if (!start_path.empty()) {
        result = searchFromDirectory(start_path);
        if (!result.empty()) return result;
        result = searchUpwards(start_path);
        if (!result.empty()) return result;
    }
    
    result = searchInstallDir();
    if (!result.empty()) return result;
    
    return "";
}

bool isValidPath(const std::string& path) {
    if (path.empty()) return false;
    
    std::regex invalidChars(R"([<>:"|?*])");
    if (std::regex_search(path, invalidChars)) {
        return false;
    }
    
    if (path.find("..") != std::string::npos) {
        return false;
    }
    
    if (path.length() > 260) {
        return false;
    }
    
    return true;
}

std::string sanitizePath(const std::string& path) {
    std::string result = path;
    
    while (!result.empty() && (result.back() == '/' || result.back() == '\\')) {
        result.pop_back();
    }
    
    for (char& c : result) {
        if (c == '\\') c = '/';
    }
    
    return result;
}

CompilerConfig ConfigLoader::loadFromFile(const std::string& filepath) {
    CompilerConfig config;
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        ErrorReporter::get().addConfigWarning(0, 0, "Could not open config file: " + filepath);
        return config;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        parseLine(line, config);
    }
    
    file.close();
    
    if (!isValidPath(config.output_dir)) {
        ErrorReporter::get().addConfigWarning(0, 0, 
            "Invalid output_dir path: " + config.output_dir + ", using default: " + DEFAULT_OUTPUT_DIR);
        config.output_dir = DEFAULT_OUTPUT_DIR;
    } else {
        config.output_dir = sanitizePath(config.output_dir);
    }
    
    if (!isValidPath(config.intermediate_dir)) {
        ErrorReporter::get().addConfigWarning(0, 0, 
            "Invalid intermediate_dir path: " + config.intermediate_dir + ", using default: " + DEFAULT_INTERMEDIATE_DIR);
        config.intermediate_dir = DEFAULT_INTERMEDIATE_DIR;
    } else {
        config.intermediate_dir = sanitizePath(config.intermediate_dir);
    }
    
    return config;
}

CompilerConfig ConfigLoader::load(const std::string& start_path) {
    std::string filepath = findConfigFile(start_path);
    
    if (!filepath.empty()) {
        return loadFromFile(filepath);
    }
    
    CompilerConfig config;
    config.output_dir = DEFAULT_OUTPUT_DIR;
    config.intermediate_dir = DEFAULT_INTERMEDIATE_DIR;
    return config;
}

void ConfigLoader::save(const std::string& filepath, const CompilerConfig& config) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not save config file: " << filepath << std::endl;
        return;
    }
    
    auto writeBool = [&file](const char* key, bool value) {
        file << "# " << key << "\n" << key << "=" << (value ? "true" : "false") << "\n\n";
    };
    
    auto writeString = [&file](const char* key, const std::string& value) {
        file << "# " << key << "\n" << key << "=" << value << "\n\n";
    };
    
    auto writeOptLevel = [&file](const char* key, OptimizationLevel level) {
        file << "# " << key << " (O0, O1, O2, O3)\n" << key << "=O" << static_cast<int>(level) << "\n\n";
    };
    
    file << "# xfawac Compiler Configuration File\n\n";
    writeBool("debug_info", config.debug_info);
    writeBool("warnings", config.warnings);
    writeBool("emit_ll", config.emit_ll);
    writeBool("emit_asm", config.emit_asm);
    writeBool("show_warning_types", config.show_warning_types);
    writeBool("xraphics_log", config.xraphics_log);
    writeOptLevel("optimization_level", config.opt_level);
    writeString("log_language", logLanguageToString(config.log_language));
    writeString("output_dir", config.output_dir);
    writeString("intermediate_dir", config.intermediate_dir);
    
    file.close();
}

}
