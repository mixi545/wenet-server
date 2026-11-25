#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <memory>

// Forward declaration to avoid circular dependency
namespace BinaryObfuscator {
    enum class ObfuscationType;
    enum class ObfuscationLevel;
    struct ObfuscationConfig;
}

/**
 * @file config_manager.h
 * @brief 配置文件和密钥管理器
 * @author Assistant
 * @date 2025-10-18
 */

namespace BinaryObfuscator {

// 配置键值对
using ConfigMap = std::map<std::string, std::string>;

// 密钥信息结构
struct KeyInfo {
    std::string name;
    std::string key;
    std::string algorithm;
    uint32_t seed;
    std::string created_time;
    std::string description;
    
    KeyInfo() : seed(0) {}
};

// 配置管理器类
class ConfigManager {
public:
    ConfigManager();
    explicit ConfigManager(const std::string& config_file);
    
    // 配置文件操作
    bool loadConfig(const std::string& filename = "");
    bool saveConfig(const std::string& filename = "") const;
    
    // 配置项操作
    void setValue(const std::string& key, const std::string& value);
    std::string getValue(const std::string& key, const std::string& default_value = "") const;
    bool hasKey(const std::string& key) const;
    void removeKey(const std::string& key);
    
    // 类型安全的getter
    int getIntValue(const std::string& key, int default_value = 0) const;
    bool getBoolValue(const std::string& key, bool default_value = false) const;
    double getDoubleValue(const std::string& key, double default_value = 0.0) const;
    
    // 类型安全的setter
    void setIntValue(const std::string& key, int value);
    void setBoolValue(const std::string& key, bool value);
    void setDoubleValue(const std::string& key, double value);
    
    // 获取所有配置
    ConfigMap getAllConfig() const;
    
    // 清空配置
    void clear();
    
    // 配置文件格式验证
    bool validateConfig() const;
    
    // 创建默认配置
    void createDefaultConfig();

private:
    std::string config_filename;
    ConfigMap config_data;
    
    std::string trim(const std::string& str) const;
    std::vector<std::string> split(const std::string& str, char delimiter) const;
};

// 密钥管理器类
class KeyManager {
public:
    KeyManager();
    explicit KeyManager(const std::string& keystore_file);
    
    // 密钥库操作
    bool loadKeystore(const std::string& filename = "");
    bool saveKeystore(const std::string& filename = "") const;
    
    // 密钥操作
    bool addKey(const KeyInfo& key_info);
    bool removeKey(const std::string& name);
    bool hasKey(const std::string& name) const;
    KeyInfo getKey(const std::string& name) const;
    std::vector<std::string> listKeys() const;
    std::vector<KeyInfo> getAllKeys() const;
    
    // 密钥生成
    KeyInfo generateKey(const std::string& name, 
                       const std::string& algorithm = "xor_simple",
                       size_t key_length = 16,
                       const std::string& description = "");
    
    // 密钥验证
    bool validateKey(const KeyInfo& key_info) const;
    
    // 导入/导出
    bool exportKey(const std::string& name, const std::string& filename) const;
    bool importKey(const std::string& filename);
    
    // 密钥库统计
    size_t getKeyCount() const;
    void clearKeystore();

private:
    std::string keystore_filename;
    std::map<std::string, KeyInfo> keys;
    
    std::string encryptKey(const std::string& key) const;
    std::string decryptKey(const std::string& encrypted_key) const;
    std::string getCurrentTime() const;
};

// 配置和密钥的统一管理器
class ObfuscatorConfigManager {
public:
    ObfuscatorConfigManager();
    explicit ObfuscatorConfigManager(const std::string& base_path);
    
    // 初始化
    bool initialize();
    
    // 获取管理器
    ConfigManager& getConfigManager() { return config_manager; }
    KeyManager& getKeyManager() { return key_manager; }
    
    // 预设配置
    void setupDefaultProfile(const std::string& profile_name);
    bool loadProfile(const std::string& profile_name);
    bool saveProfile(const std::string& profile_name) const;
    std::vector<std::string> listProfiles() const;
    
    // 快捷配置
    ObfuscationConfig createObfuscationConfig(const std::string& profile_name = "") const;
    bool saveObfuscationConfig(const ObfuscationConfig& config, const std::string& profile_name);
    
    // 工具函数
    std::string getConfigPath() const;
    std::string getKeystorePath() const;
    bool createDirectories() const;

private:
    std::string base_path;
    ConfigManager config_manager;
    KeyManager key_manager;
    
    void setupDefaultConfigs();
    std::string joinPath(const std::string& path1, const std::string& path2) const;
};

} // namespace BinaryObfuscator

#endif // CONFIG_MANAGER_H