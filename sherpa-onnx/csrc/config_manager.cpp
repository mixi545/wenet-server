#include "config_manager.h"
#include "binary_obfuscator.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

/**
 * @file config_manager.cpp
 * @brief 配置文件和密钥管理器实现
 */

namespace BinaryObfuscator {

// =============================================================================
// ConfigManager 实现
// =============================================================================

ConfigManager::ConfigManager() : config_filename("obfuscator.conf") {}

ConfigManager::ConfigManager(const std::string& config_file) : config_filename(config_file) {}

bool ConfigManager::loadConfig(const std::string& filename) {
    std::string file_to_load = filename.empty() ? config_filename : filename;
    
    std::ifstream file(file_to_load);
    if (!file.is_open()) {
        // 如果文件不存在，创建默认配置
        createDefaultConfig();
        return saveConfig(file_to_load);
    }
    
    config_data.clear();
    std::string line;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        line = trim(line);
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // 查找等号分隔符
        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            std::cerr << "配置文件语法错误 (行 " << line_number << "): " << line << std::endl;
            continue;
        }
        
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        
        // 移除值两端的引号
        if (value.length() >= 2 && 
            ((value[0] == '"' && value[value.length()-1] == '"') ||
             (value[0] == '\'' && value[value.length()-1] == '\''))) {
            value = value.substr(1, value.length() - 2);
        }
        
        config_data[key] = value;
    }
    
    return true;
}

bool ConfigManager::saveConfig(const std::string& filename) const {
    std::string file_to_save = filename.empty() ? config_filename : filename;
    
    std::ofstream file(file_to_save);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# Binary Obfuscator Configuration File\n";
    file << "# Generated on " << BinaryObfuscator::Utils::getCurrentTime() << "\n\n";
    
    for (const auto& pair : config_data) {
        file << pair.first << " = \"" << pair.second << "\"\n";
    }
    
    return true;
}

void ConfigManager::setValue(const std::string& key, const std::string& value) {
    config_data[key] = value;
}

std::string ConfigManager::getValue(const std::string& key, const std::string& default_value) const {
    auto it = config_data.find(key);
    return (it != config_data.end()) ? it->second : default_value;
}

bool ConfigManager::hasKey(const std::string& key) const {
    return config_data.find(key) != config_data.end();
}

void ConfigManager::removeKey(const std::string& key) {
    config_data.erase(key);
}

int ConfigManager::getIntValue(const std::string& key, int default_value) const {
    try {
        return std::stoi(getValue(key, std::to_string(default_value)));
    } catch (...) {
        return default_value;
    }
}

bool ConfigManager::getBoolValue(const std::string& key, bool default_value) const {
    std::string value = getValue(key, default_value ? "true" : "false");
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

double ConfigManager::getDoubleValue(const std::string& key, double default_value) const {
    try {
        return std::stod(getValue(key, std::to_string(default_value)));
    } catch (...) {
        return default_value;
    }
}

void ConfigManager::setIntValue(const std::string& key, int value) {
    setValue(key, std::to_string(value));
}

void ConfigManager::setBoolValue(const std::string& key, bool value) {
    setValue(key, value ? "true" : "false");
}

void ConfigManager::setDoubleValue(const std::string& key, double value) {
    setValue(key, std::to_string(value));
}

ConfigMap ConfigManager::getAllConfig() const {
    return config_data;
}

void ConfigManager::clear() {
    config_data.clear();
}

bool ConfigManager::validateConfig() const {
    // 检查必要的配置项
    std::vector<std::string> required_keys = {
        "default_algorithm", "default_level", "default_rounds"
    };
    
    for (const auto& key : required_keys) {
        if (!hasKey(key)) {
            return false;
        }
    }
    
    return true;
}

void ConfigManager::createDefaultConfig() {
    setValue("default_algorithm", "xor_simple");
    setValue("default_level", "medium");
    setValue("default_rounds", "1");
    setValue("default_key_length", "16");
    setValue("create_backup", "true");
    setValue("verify_integrity", "true");
    setValue("verbose_output", "false");
    setValue("preserve_header", "false");
    setValue("header_size", "0");
    setValue("max_file_size", "104857600"); // 100MB
    setValue("temp_directory", "./temp");
    setValue("log_level", "info");
}

std::string ConfigManager::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> ConfigManager::split(const std::string& str, char delimiter) const {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    
    return tokens;
}

// =============================================================================
// KeyManager 实现
// =============================================================================

KeyManager::KeyManager() : keystore_filename("keystore.dat") {}

KeyManager::KeyManager(const std::string& keystore_file) : keystore_filename(keystore_file) {}

bool KeyManager::loadKeystore(const std::string& filename) {
    std::string file_to_load = filename.empty() ? keystore_filename : filename;
    
    std::ifstream file(file_to_load);
    if (!file.is_open()) {
        return true; // 文件不存在是正常的
    }
    
    keys.clear();
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        // 格式: name|algorithm|key|seed|created_time|description
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> parts;
        
        while (std::getline(ss, token, '|')) {
            parts.push_back(token);
        }
        
        if (parts.size() >= 4) {
            KeyInfo key_info;
            key_info.name = parts[0];
            key_info.algorithm = parts[1];
            key_info.key = decryptKey(parts[2]); // 解密存储的密钥
            key_info.seed = std::stoul(parts[3]);
            if (parts.size() > 4) key_info.created_time = parts[4];
            if (parts.size() > 5) key_info.description = parts[5];
            
            keys[key_info.name] = key_info;
        }
    }
    
    return true;
}

bool KeyManager::saveKeystore(const std::string& filename) const {
    std::string file_to_save = filename.empty() ? keystore_filename : filename;
    
    std::ofstream file(file_to_save);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# Binary Obfuscator Keystore\n";
    file << "# Format: name|algorithm|encrypted_key|seed|created_time|description\n\n";
    
    for (const auto& pair : keys) {
        const KeyInfo& key_info = pair.second;
        file << key_info.name << "|"
             << key_info.algorithm << "|"
             << encryptKey(key_info.key) << "|"  // 加密存储
             << key_info.seed << "|"
             << key_info.created_time << "|"
             << key_info.description << "\n";
    }
    
    return true;
}

bool KeyManager::addKey(const KeyInfo& key_info) {
    if (key_info.name.empty() || key_info.key.empty()) {
        return false;
    }
    
    if (!validateKey(key_info)) {
        return false;
    }
    
    keys[key_info.name] = key_info;
    return true;
}

bool KeyManager::removeKey(const std::string& name) {
    return keys.erase(name) > 0;
}

bool KeyManager::hasKey(const std::string& name) const {
    return keys.find(name) != keys.end();
}

KeyInfo KeyManager::getKey(const std::string& name) const {
    auto it = keys.find(name);
    return (it != keys.end()) ? it->second : KeyInfo();
}

std::vector<std::string> KeyManager::listKeys() const {
    std::vector<std::string> key_names;
    for (const auto& pair : keys) {
        key_names.push_back(pair.first);
    }
    return key_names;
}

std::vector<KeyInfo> KeyManager::getAllKeys() const {
    std::vector<KeyInfo> all_keys;
    for (const auto& pair : keys) {
        all_keys.push_back(pair.second);
    }
    return all_keys;
}

KeyInfo KeyManager::generateKey(const std::string& name, 
                               const std::string& algorithm,
                               size_t key_length,
                               const std::string& description) {
    BinaryObfuscatorTool tool;
    
    KeyInfo key_info;
    key_info.name = name;
    key_info.algorithm = algorithm;
    key_info.key = tool.generateRandomKey(key_length);
    key_info.seed = Utils::generateSeed();
    key_info.created_time = getCurrentTime();
    key_info.description = description;
    
    return key_info;
}

bool KeyManager::validateKey(const KeyInfo& key_info) const {
    if (key_info.name.empty() || key_info.key.empty()) {
        return false;
    }
    
    // 检查算法名称
    std::vector<std::string> valid_algorithms = {
        "xor_simple", "xor_rotating", "byte_shuffle", 
        "bit_shift", "substitution", "multi_layer"
    };
    
    auto it = std::find(valid_algorithms.begin(), valid_algorithms.end(), key_info.algorithm);
    return it != valid_algorithms.end();
}

bool KeyManager::exportKey(const std::string& name, const std::string& filename) const {
    auto it = keys.find(name);
    if (it == keys.end()) {
        return false;
    }
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    const KeyInfo& key_info = it->second;
    file << "# Exported Key: " << name << "\n";
    file << "name=" << key_info.name << "\n";
    file << "algorithm=" << key_info.algorithm << "\n";
    file << "key=" << key_info.key << "\n";
    file << "seed=" << key_info.seed << "\n";
    file << "created_time=" << key_info.created_time << "\n";
    file << "description=" << key_info.description << "\n";
    
    return true;
}

bool KeyManager::importKey(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    KeyInfo key_info;
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        if (key == "name") key_info.name = value;
        else if (key == "algorithm") key_info.algorithm = value;
        else if (key == "key") key_info.key = value;
        else if (key == "seed") key_info.seed = std::stoul(value);
        else if (key == "created_time") key_info.created_time = value;
        else if (key == "description") key_info.description = value;
    }
    
    return addKey(key_info);
}

size_t KeyManager::getKeyCount() const {
    return keys.size();
}

void KeyManager::clearKeystore() {
    keys.clear();
}

std::string KeyManager::encryptKey(const std::string& key) const {
    // 简单的XOR加密（实际应用中应使用更强的加密）
    std::string encrypted = key;
    const std::string password = "obfuscator_key_encrypt";
    
    for (size_t i = 0; i < encrypted.size(); ++i) {
        encrypted[i] ^= password[i % password.size()];
    }
    
    return Utils::bytesToHexString(std::vector<uint8_t>(encrypted.begin(), encrypted.end()));
}

std::string KeyManager::decryptKey(const std::string& encrypted_key) const {
    // 从十六进制字符串转换回字节
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < encrypted_key.length(); i += 2) {
        std::string hex_byte = encrypted_key.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(hex_byte, nullptr, 16));
        bytes.push_back(byte);
    }
    
    std::string decrypted(bytes.begin(), bytes.end());
    const std::string password = "obfuscator_key_encrypt";
    
    for (size_t i = 0; i < decrypted.size(); ++i) {
        decrypted[i] ^= password[i % password.size()];
    }
    
    return decrypted;
}

std::string KeyManager::getCurrentTime() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// =============================================================================
// ObfuscatorConfigManager 实现
// =============================================================================

ObfuscatorConfigManager::ObfuscatorConfigManager() : base_path("./obfuscator_config") {
    initialize();
}

ObfuscatorConfigManager::ObfuscatorConfigManager(const std::string& base_path) 
    : base_path(base_path) {
    initialize();
}

bool ObfuscatorConfigManager::initialize() {
    if (!createDirectories()) {
        return false;
    }
    
    config_manager = ConfigManager(getConfigPath());
    key_manager = KeyManager(getKeystorePath());
    
    setupDefaultConfigs();
    
    return config_manager.loadConfig() && key_manager.loadKeystore();
}

void ObfuscatorConfigManager::setupDefaultProfile(const std::string& profile_name) {
    std::string prefix = profile_name + ".";
    
    if (profile_name == "secure") {
        config_manager.setValue(prefix + "algorithm", "multi_layer");
        config_manager.setValue(prefix + "level", "high");
        config_manager.setValue(prefix + "rounds", "3");
        config_manager.setValue(prefix + "verify_integrity", "true");
    } else if (profile_name == "fast") {
        config_manager.setValue(prefix + "algorithm", "xor_simple");
        config_manager.setValue(prefix + "level", "low");
        config_manager.setValue(prefix + "rounds", "1");
        config_manager.setValue(prefix + "verify_integrity", "false");
    } else if (profile_name == "balanced") {
        config_manager.setValue(prefix + "algorithm", "xor_rotating");
        config_manager.setValue(prefix + "level", "medium");
        config_manager.setValue(prefix + "rounds", "2");
        config_manager.setValue(prefix + "verify_integrity", "true");
    }
}

bool ObfuscatorConfigManager::loadProfile(const std::string& profile_name) {
    std::string filename = joinPath(base_path, profile_name + ".profile");
    return config_manager.loadConfig(filename);
}

bool ObfuscatorConfigManager::saveProfile(const std::string& profile_name) const {
    std::string filename = joinPath(base_path, profile_name + ".profile");
    return config_manager.saveConfig(filename);
}

std::vector<std::string> ObfuscatorConfigManager::listProfiles() const {
    std::vector<std::string> profiles;
    // 这里应该扫描目录中的.profile文件
    // 简化实现，返回预定义的配置
    profiles.push_back("secure");
    profiles.push_back("fast");
    profiles.push_back("balanced");
    return profiles;
}

ObfuscationConfig ObfuscatorConfigManager::createObfuscationConfig(const std::string& profile_name) const {
    ObfuscationConfig config;
    std::string prefix = profile_name.empty() ? "" : profile_name + ".";
    
    // 算法映射
    std::map<std::string, ObfuscationType> alg_map = {
        {"xor_simple", ObfuscationType::XOR_SIMPLE},
        {"xor_rotating", ObfuscationType::XOR_ROTATING},
        {"byte_shuffle", ObfuscationType::BYTE_SHUFFLE},
        {"bit_shift", ObfuscationType::BIT_SHIFT},
        {"substitution", ObfuscationType::SUBSTITUTION},
        {"multi_layer", ObfuscationType::MULTI_LAYER}
    };
    
    std::map<std::string, ObfuscationLevel> level_map = {
        {"low", ObfuscationLevel::LOW},
        {"medium", ObfuscationLevel::MEDIUM},
        {"high", ObfuscationLevel::HIGH},
        {"extreme", ObfuscationLevel::EXTREME}
    };
    
    std::string alg_str = config_manager.getValue(prefix + "algorithm", "xor_simple");
    auto alg_it = alg_map.find(alg_str);
    config.type = (alg_it != alg_map.end()) ? alg_it->second : ObfuscationType::XOR_SIMPLE;
    
    std::string level_str = config_manager.getValue(prefix + "level", "medium");
    auto level_it = level_map.find(level_str);
    config.level = (level_it != level_map.end()) ? level_it->second : ObfuscationLevel::MEDIUM;
    
    config.key = config_manager.getValue(prefix + "key", "default_key");
    config.seed = config_manager.getIntValue(prefix + "seed", Utils::generateSeed());
    config.rounds = config_manager.getIntValue(prefix + "rounds", 1);
    config.preserve_header = config_manager.getBoolValue(prefix + "preserve_header", false);
    config.header_size = config_manager.getIntValue(prefix + "header_size", 0);
    
    return config;
}

bool ObfuscatorConfigManager::saveObfuscationConfig(const ObfuscationConfig& config, 
                                                    const std::string& profile_name) {
    std::string prefix = profile_name + ".";
    
    // 类型到字符串的映射
    std::map<ObfuscationType, std::string> type_to_str = {
        {ObfuscationType::XOR_SIMPLE, "xor_simple"},
        {ObfuscationType::XOR_ROTATING, "xor_rotating"},
        {ObfuscationType::BYTE_SHUFFLE, "byte_shuffle"},
        {ObfuscationType::BIT_SHIFT, "bit_shift"},
        {ObfuscationType::SUBSTITUTION, "substitution"},
        {ObfuscationType::MULTI_LAYER, "multi_layer"}
    };
    
    std::map<ObfuscationLevel, std::string> level_to_str = {
        {ObfuscationLevel::LOW, "low"},
        {ObfuscationLevel::MEDIUM, "medium"},
        {ObfuscationLevel::HIGH, "high"},
        {ObfuscationLevel::EXTREME, "extreme"}
    };
    
    config_manager.setValue(prefix + "algorithm", type_to_str[config.type]);
    config_manager.setValue(prefix + "level", level_to_str[config.level]);
    config_manager.setValue(prefix + "key", config.key);
    config_manager.setIntValue(prefix + "seed", config.seed);
    config_manager.setIntValue(prefix + "rounds", config.rounds);
    config_manager.setBoolValue(prefix + "preserve_header", config.preserve_header);
    config_manager.setIntValue(prefix + "header_size", config.header_size);
    
    return config_manager.saveConfig();
}

std::string ObfuscatorConfigManager::getConfigPath() const {
    return joinPath(base_path, "config.ini");
}

std::string ObfuscatorConfigManager::getKeystorePath() const {
    return joinPath(base_path, "keystore.dat");
}

bool ObfuscatorConfigManager::createDirectories() const {
    // 创建基础目录
    struct stat info;
    if (stat(base_path.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
        if (MKDIR(base_path.c_str()) != 0) {
            return false;
        }
    }
    
    return true;
}

void ObfuscatorConfigManager::setupDefaultConfigs() {
    // 设置默认配置文件的各种预设
    setupDefaultProfile("secure");
    setupDefaultProfile("fast");
    setupDefaultProfile("balanced");
}

std::string ObfuscatorConfigManager::joinPath(const std::string& path1, const std::string& path2) const {
    if (path1.empty()) return path2;
    if (path2.empty()) return path1;
    
    char sep = '/';
#ifdef _WIN32
    sep = '\\';
#endif
    
    std::string result = path1;
    if (result.back() != sep && path2.front() != sep) {
        result += sep;
    }
    result += path2;
    
    return result;
}

} // namespace BinaryObfuscator