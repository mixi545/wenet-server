#ifndef BINARY_OBFUSCATOR_H
#define BINARY_OBFUSCATOR_H

#include <string>
#include <vector>
#include <memory>
#include <random>
#include <cstdint>
#include <functional>
#include <map>

/**
 * @file binary_obfuscator.h
 * @brief 二进制文件混淆工具头文件
 * @author Assistant
 * @date 2025-10-18
 * @description 提供多种算法对二进制文件进行混淆和还原
 */

namespace BinaryObfuscator {

// 混淆算法类型
enum class ObfuscationType {
    XOR_SIMPLE,      // 简单异或
    XOR_ROTATING,    // 旋转异或（变化密钥）
    BYTE_SHUFFLE,    // 字节打乱
    BIT_SHIFT,       // 位移
    SUBSTITUTION,    // 替换表
    MULTI_LAYER,     // 多层混淆
    ENCRYPTION       // 简单加密
};

// 混淆强度
enum class ObfuscationLevel {
    LOW = 1,         // 低强度
    MEDIUM = 2,      // 中等强度
    HIGH = 3,        // 高强度
    EXTREME = 4      // 极高强度
};

// 混淆配置
struct ObfuscationConfig {
    ObfuscationType type;
    ObfuscationLevel level;
    std::string key;
    uint32_t seed;
    uint32_t rounds;
    bool preserve_header;
    size_t header_size;
    
    ObfuscationConfig() 
        : type(ObfuscationType::XOR_SIMPLE)
        , level(ObfuscationLevel::MEDIUM)
        , key("default_key")
        , seed(12345)
        , rounds(1)
        , preserve_header(false)
        , header_size(0) {}
};

// 混淆结果
struct ObfuscationResult {
    bool success;
    std::string message;
    size_t original_size;
    size_t processed_size;
    double time_taken;
    
    ObfuscationResult() 
        : success(false)
        , original_size(0)
        , processed_size(0)
        , time_taken(0.0) {}
};

// 抽象混淆算法基类
class ObfuscationAlgorithm {
public:
    virtual ~ObfuscationAlgorithm() = default;
    
    virtual std::vector<uint8_t> obfuscate(const std::vector<uint8_t>& data, 
                                          const ObfuscationConfig& config) = 0;
    virtual std::vector<uint8_t> deobfuscate(const std::vector<uint8_t>& data, 
                                             const ObfuscationConfig& config) = 0;
    virtual std::string getName() const = 0;
    virtual bool validateConfig(const ObfuscationConfig& config) const = 0;
};

// XOR混淆算法
class XORObfuscation : public ObfuscationAlgorithm {
public:
    std::vector<uint8_t> obfuscate(const std::vector<uint8_t>& data, 
                                  const ObfuscationConfig& config) override;
    std::vector<uint8_t> deobfuscate(const std::vector<uint8_t>& data, 
                                    const ObfuscationConfig& config) override;
    std::string getName() const override { return "XOR Obfuscation"; }
    bool validateConfig(const ObfuscationConfig& config) const override;
    
private:
    std::vector<uint8_t> generateKeySequence(const std::string& key, 
                                            uint32_t seed, 
                                            size_t length, 
                                            bool rotating = false);
};

// 字节打乱混淆
class ByteShuffleObfuscation : public ObfuscationAlgorithm {
public:
    std::vector<uint8_t> obfuscate(const std::vector<uint8_t>& data, 
                                  const ObfuscationConfig& config) override;
    std::vector<uint8_t> deobfuscate(const std::vector<uint8_t>& data, 
                                    const ObfuscationConfig& config) override;
    std::string getName() const override { return "Byte Shuffle Obfuscation"; }
    bool validateConfig(const ObfuscationConfig& config) const override;
    
private:
    std::vector<size_t> generateShuffleSequence(size_t length, uint32_t seed);
    std::vector<size_t> generateReverseSequence(const std::vector<size_t>& sequence);
};

// 位移混淆
class BitShiftObfuscation : public ObfuscationAlgorithm {
public:
    std::vector<uint8_t> obfuscate(const std::vector<uint8_t>& data, 
                                  const ObfuscationConfig& config) override;
    std::vector<uint8_t> deobfuscate(const std::vector<uint8_t>& data, 
                                    const ObfuscationConfig& config) override;
    std::string getName() const override { return "Bit Shift Obfuscation"; }
    bool validateConfig(const ObfuscationConfig& config) const override;
    
private:
    uint8_t rotateLeft(uint8_t value, int shift);
    uint8_t rotateRight(uint8_t value, int shift);
};

// 替换表混淆
class SubstitutionObfuscation : public ObfuscationAlgorithm {
public:
    std::vector<uint8_t> obfuscate(const std::vector<uint8_t>& data, 
                                  const ObfuscationConfig& config) override;
    std::vector<uint8_t> deobfuscate(const std::vector<uint8_t>& data, 
                                    const ObfuscationConfig& config) override;
    std::string getName() const override { return "Substitution Obfuscation"; }
    bool validateConfig(const ObfuscationConfig& config) const override;
    
private:
    std::vector<uint8_t> generateSubTable(const std::string& key, uint32_t seed);
    std::vector<uint8_t> generateInverseTable(const std::vector<uint8_t>& table);
};

// 多层混淆
class MultiLayerObfuscation : public ObfuscationAlgorithm {
public:
    MultiLayerObfuscation();
    
    std::vector<uint8_t> obfuscate(const std::vector<uint8_t>& data, 
                                  const ObfuscationConfig& config) override;
    std::vector<uint8_t> deobfuscate(const std::vector<uint8_t>& data, 
                                    const ObfuscationConfig& config) override;
    std::string getName() const override { return "Multi-Layer Obfuscation"; }
    bool validateConfig(const ObfuscationConfig& config) const override;
    
private:
    std::vector<std::unique_ptr<ObfuscationAlgorithm>> layers;
    void initializeLayers();
};

// 主混淆器类
class BinaryObfuscatorTool {
public:
    BinaryObfuscatorTool();
    ~BinaryObfuscatorTool();
    
    // 文件操作
    ObfuscationResult obfuscateFile(const std::string& input_file,
                                   const std::string& output_file,
                                   const ObfuscationConfig& config);
    
    ObfuscationResult deobfuscateFile(const std::string& input_file,
                                     const std::string& output_file,
                                     const ObfuscationConfig& config);
    
    // 数据操作
    std::vector<uint8_t> obfuscateData(const std::vector<uint8_t>& data,
                                      const ObfuscationConfig& config);
    
    std::vector<uint8_t> deobfuscateData(const std::vector<uint8_t>& data,
                                        const ObfuscationConfig& config);
    
    // 工具函数
    std::string generateRandomKey(size_t length);
    std::vector<std::string> getSupportedAlgorithms() const;
    void setProgressCallback(std::function<void(int)> callback);
    
    // 验证函数
    bool verifyIntegrity(const std::vector<uint8_t>& original,
                        const std::vector<uint8_t>& processed,
                        const ObfuscationConfig& config);

private:
    std::map<ObfuscationType, std::unique_ptr<ObfuscationAlgorithm>> algorithms;
    std::function<void(int)> progress_callback;
    std::mt19937 random_generator;
    
    void initializeAlgorithms();
    std::vector<uint8_t> readFile(const std::string& filename);
    bool writeFile(const std::string& filename, const std::vector<uint8_t>& data);
    void updateProgress(int percentage);
};

// 工具函数命名空间
namespace Utils {
    std::vector<uint8_t> stringToBytes(const std::string& str);
    std::string bytesToHexString(const std::vector<uint8_t>& data);
    std::string calculateMD5(const std::vector<uint8_t>& data);
    bool fileExists(const std::string& filename);
    size_t getFileSize(const std::string& filename);
    std::string getFileExtension(const std::string& filename);
    std::string createBackup(const std::string& filename);
    
    // 时间相关
    double getCurrentTime();
    std::string formatTime(double seconds);
    
    // 随机数生成
    uint32_t generateSeed();
    std::vector<uint8_t> generateRandomBytes(size_t count, uint32_t seed);
}

} // namespace BinaryObfuscator

#endif // BINARY_OBFUSCATOR_H