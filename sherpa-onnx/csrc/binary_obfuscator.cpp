#include "binary_obfuscator.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>

/**
 * @file binary_obfuscator.cpp
 * @brief 二进制文件混淆工具实现
 */

namespace BinaryObfuscator {

// =============================================================================
// XOR混淆算法实现
// =============================================================================

std::vector<uint8_t> XORObfuscation::obfuscate(const std::vector<uint8_t>& data, 
                                               const ObfuscationConfig& config) {
    if (data.empty()) return data;
    
    std::vector<uint8_t> result = data;
    bool rotating = (config.type == ObfuscationType::XOR_ROTATING);
    
    for (uint32_t round = 0; round < config.rounds; ++round) {
        auto key_seq = generateKeySequence(config.key, config.seed + round, 
                                         result.size(), rotating);
        
        size_t start_pos = config.preserve_header ? config.header_size : 0;
        for (size_t i = start_pos; i < result.size(); ++i) {
            result[i] ^= key_seq[i];
        }
    }
    
    return result;
}

std::vector<uint8_t> XORObfuscation::deobfuscate(const std::vector<uint8_t>& data, 
                                                 const ObfuscationConfig& config) {
    // XOR是对称操作，解密与加密相同
    return obfuscate(data, config);
}

bool XORObfuscation::validateConfig(const ObfuscationConfig& config) const {
    return !config.key.empty() && config.rounds > 0;
}

std::vector<uint8_t> XORObfuscation::generateKeySequence(const std::string& key, 
                                                        uint32_t seed, 
                                                        size_t length, 
                                                        bool rotating) {
    std::vector<uint8_t> sequence;
    sequence.reserve(length);
    
    std::mt19937 rng(seed);
    
    if (rotating) {
        // 生成变化的密钥序列
        for (size_t i = 0; i < length; ++i) {
            uint8_t key_byte = key[i % key.length()];
            uint8_t random_byte = static_cast<uint8_t>(rng());
            sequence.push_back(key_byte ^ random_byte ^ static_cast<uint8_t>(i));
        }
    } else {
        // 简单重复密钥
        for (size_t i = 0; i < length; ++i) {
            sequence.push_back(key[i % key.length()]);
        }
    }
    
    return sequence;
}

// =============================================================================
// 字节打乱混淆算法实现
// =============================================================================

std::vector<uint8_t> ByteShuffleObfuscation::obfuscate(const std::vector<uint8_t>& data, 
                                                       const ObfuscationConfig& config) {
    if (data.empty()) return data;
    
    std::vector<uint8_t> result = data;
    size_t start_pos = config.preserve_header ? config.header_size : 0;
    size_t shuffle_size = result.size() - start_pos;
    
    if (shuffle_size <= 1) return result;
    
    for (uint32_t round = 0; round < config.rounds; ++round) {
        auto shuffle_seq = generateShuffleSequence(shuffle_size, config.seed + round);
        
        std::vector<uint8_t> temp(result.begin() + start_pos, result.end());
        for (size_t i = 0; i < shuffle_size; ++i) {
            result[start_pos + i] = temp[shuffle_seq[i]];
        }
    }
    
    return result;
}

std::vector<uint8_t> ByteShuffleObfuscation::deobfuscate(const std::vector<uint8_t>& data, 
                                                        const ObfuscationConfig& config) {
    if (data.empty()) return data;
    
    std::vector<uint8_t> result = data;
    size_t start_pos = config.preserve_header ? config.header_size : 0;
    size_t shuffle_size = result.size() - start_pos;
    
    if (shuffle_size <= 1) return result;
    
    // 逆向处理
    for (int round = config.rounds - 1; round >= 0; --round) {
        auto shuffle_seq = generateShuffleSequence(shuffle_size, config.seed + round);
        auto reverse_seq = generateReverseSequence(shuffle_seq);
        
        std::vector<uint8_t> temp(result.begin() + start_pos, result.end());
        for (size_t i = 0; i < shuffle_size; ++i) {
            result[start_pos + i] = temp[reverse_seq[i]];
        }
    }
    
    return result;
}

bool ByteShuffleObfuscation::validateConfig(const ObfuscationConfig& config) const {
    return config.rounds > 0;
}

std::vector<size_t> ByteShuffleObfuscation::generateShuffleSequence(size_t length, uint32_t seed) {
    std::vector<size_t> sequence;
    for (size_t i = 0; i < length; ++i) {
        sequence.push_back(i);
    }
    
    std::mt19937 rng(seed);
    std::shuffle(sequence.begin(), sequence.end(), rng);
    
    return sequence;
}

std::vector<size_t> ByteShuffleObfuscation::generateReverseSequence(const std::vector<size_t>& sequence) {
    std::vector<size_t> reverse(sequence.size());
    for (size_t i = 0; i < sequence.size(); ++i) {
        reverse[sequence[i]] = i;
    }
    return reverse;
}

// =============================================================================
// 位移混淆算法实现
// =============================================================================

std::vector<uint8_t> BitShiftObfuscation::obfuscate(const std::vector<uint8_t>& data, 
                                                    const ObfuscationConfig& config) {
    if (data.empty()) return data;
    
    std::vector<uint8_t> result = data;
    size_t start_pos = config.preserve_header ? config.header_size : 0;
    
    std::mt19937 rng(config.seed);
    
    for (uint32_t round = 0; round < config.rounds; ++round) {
        for (size_t i = start_pos; i < result.size(); ++i) {
            int shift = (rng() % 7) + 1; // 1-7位移动
            result[i] = rotateLeft(result[i], shift);
        }
    }
    
    return result;
}

std::vector<uint8_t> BitShiftObfuscation::deobfuscate(const std::vector<uint8_t>& data, 
                                                      const ObfuscationConfig& config) {
    if (data.empty()) return data;
    
    std::vector<uint8_t> result = data;
    size_t start_pos = config.preserve_header ? config.header_size : 0;
    
    std::mt19937 rng(config.seed);
    std::vector<int> shifts;
    
    // 生成相同的移位序列
    for (uint32_t round = 0; round < config.rounds; ++round) {
        for (size_t i = start_pos; i < result.size(); ++i) {
            shifts.push_back((rng() % 7) + 1);
        }
    }
    
    // 逆向处理
    size_t shift_idx = shifts.size() - 1;
    for (int round = config.rounds - 1; round >= 0; --round) {
        for (int i = result.size() - 1; i >= (int)start_pos; --i) {
            result[i] = rotateRight(result[i], shifts[shift_idx--]);
        }
    }
    
    return result;
}

bool BitShiftObfuscation::validateConfig(const ObfuscationConfig& config) const {
    return config.rounds > 0;
}

uint8_t BitShiftObfuscation::rotateLeft(uint8_t value, int shift) {
    shift %= 8;
    return (value << shift) | (value >> (8 - shift));
}

uint8_t BitShiftObfuscation::rotateRight(uint8_t value, int shift) {
    shift %= 8;
    return (value >> shift) | (value << (8 - shift));
}

// =============================================================================
// 替换表混淆算法实现
// =============================================================================

std::vector<uint8_t> SubstitutionObfuscation::obfuscate(const std::vector<uint8_t>& data, 
                                                        const ObfuscationConfig& config) {
    if (data.empty()) return data;
    
    std::vector<uint8_t> result = data;
    size_t start_pos = config.preserve_header ? config.header_size : 0;
    
    for (uint32_t round = 0; round < config.rounds; ++round) {
        auto sub_table = generateSubTable(config.key, config.seed + round);
        
        for (size_t i = start_pos; i < result.size(); ++i) {
            result[i] = sub_table[result[i]];
        }
    }
    
    return result;
}

std::vector<uint8_t> SubstitutionObfuscation::deobfuscate(const std::vector<uint8_t>& data, 
                                                          const ObfuscationConfig& config) {
    if (data.empty()) return data;
    
    std::vector<uint8_t> result = data;
    size_t start_pos = config.preserve_header ? config.header_size : 0;
    
    // 逆向处理
    for (int round = config.rounds - 1; round >= 0; --round) {
        auto sub_table = generateSubTable(config.key, config.seed + round);
        auto inverse_table = generateInverseTable(sub_table);
        
        for (size_t i = start_pos; i < result.size(); ++i) {
            result[i] = inverse_table[result[i]];
        }
    }
    
    return result;
}

bool SubstitutionObfuscation::validateConfig(const ObfuscationConfig& config) const {
    return !config.key.empty() && config.rounds > 0;
}

std::vector<uint8_t> SubstitutionObfuscation::generateSubTable(const std::string& key, uint32_t seed) {
    std::vector<uint8_t> table;
    for (int i = 0; i < 256; ++i) {
        table.push_back(static_cast<uint8_t>(i));
    }
    
    // 基于密钥和种子打乱
    std::mt19937 rng(seed);
    for (char c : key) {
        rng.seed(rng() ^ static_cast<uint32_t>(c));
    }
    
    std::shuffle(table.begin(), table.end(), rng);
    return table;
}

std::vector<uint8_t> SubstitutionObfuscation::generateInverseTable(const std::vector<uint8_t>& table) {
    std::vector<uint8_t> inverse(256);
    for (size_t i = 0; i < table.size(); ++i) {
        inverse[table[i]] = static_cast<uint8_t>(i);
    }
    return inverse;
}

// =============================================================================
// 多层混淆算法实现
// =============================================================================

MultiLayerObfuscation::MultiLayerObfuscation() {
    initializeLayers();
}

std::vector<uint8_t> MultiLayerObfuscation::obfuscate(const std::vector<uint8_t>& data, 
                                                      const ObfuscationConfig& config) {
    std::vector<uint8_t> result = data;
    
    // 根据强度级别选择层数
    size_t num_layers = static_cast<size_t>(config.level);
    num_layers = std::min(num_layers, layers.size());
    
    ObfuscationConfig layer_config = config;
    layer_config.rounds = 1; // 每层只执行一轮
    
    for (size_t i = 0; i < num_layers; ++i) {
        layer_config.seed = config.seed + static_cast<uint32_t>(i);
        result = layers[i]->obfuscate(result, layer_config);
    }
    
    return result;
}

std::vector<uint8_t> MultiLayerObfuscation::deobfuscate(const std::vector<uint8_t>& data, 
                                                        const ObfuscationConfig& config) {
    std::vector<uint8_t> result = data;
    
    // 根据强度级别选择层数
    size_t num_layers = static_cast<size_t>(config.level);
    num_layers = std::min(num_layers, layers.size());
    
    ObfuscationConfig layer_config = config;
    layer_config.rounds = 1;
    
    // 逆向处理
    for (int i = num_layers - 1; i >= 0; --i) {
        layer_config.seed = config.seed + static_cast<uint32_t>(i);
        result = layers[i]->deobfuscate(result, layer_config);
    }
    
    return result;
}

bool MultiLayerObfuscation::validateConfig(const ObfuscationConfig& config) const {
    return !config.key.empty() && config.rounds > 0;
}

void MultiLayerObfuscation::initializeLayers() {
    layers.push_back(std::make_unique<XORObfuscation>());
    layers.push_back(std::make_unique<ByteShuffleObfuscation>());
    layers.push_back(std::make_unique<BitShiftObfuscation>());
    layers.push_back(std::make_unique<SubstitutionObfuscation>());
}

// =============================================================================
// 主混淆器工具实现
// =============================================================================

BinaryObfuscatorTool::BinaryObfuscatorTool() : random_generator(std::random_device{}()) {
    initializeAlgorithms();
}

BinaryObfuscatorTool::~BinaryObfuscatorTool() = default;

ObfuscationResult BinaryObfuscatorTool::obfuscateFile(const std::string& input_file,
                                                      const std::string& output_file,
                                                      const ObfuscationConfig& config) {
    ObfuscationResult result;
    auto start_time = Utils::getCurrentTime();
    
    try {
        if (!Utils::fileExists(input_file)) {
            result.message = "Input file does not exist: " + input_file;
            return result;
        }
        
        auto data = readFile(input_file);
        result.original_size = data.size();
        
        updateProgress(25);
        
        auto obfuscated_data = obfuscateData(data, config);
        result.processed_size = obfuscated_data.size();
        
        updateProgress(75);
        
        if (writeFile(output_file, obfuscated_data)) {
            result.success = true;
            result.message = "File obfuscated successfully";
        } else {
            result.message = "Failed to write output file: " + output_file;
        }
        
        updateProgress(100);
        
    } catch (const std::exception& e) {
        result.message = "Error: " + std::string(e.what());
    }
    
    result.time_taken = Utils::getCurrentTime() - start_time;
    return result;
}

ObfuscationResult BinaryObfuscatorTool::deobfuscateFile(const std::string& input_file,
                                                        const std::string& output_file,
                                                        const ObfuscationConfig& config) {
    ObfuscationResult result;
    auto start_time = Utils::getCurrentTime();
    
    try {
        if (!Utils::fileExists(input_file)) {
            result.message = "Input file does not exist: " + input_file;
            return result;
        }
        
        auto data = readFile(input_file);
        result.original_size = data.size();
        
        updateProgress(25);
        
        auto deobfuscated_data = deobfuscateData(data, config);
        result.processed_size = deobfuscated_data.size();
        
        updateProgress(75);
        
        if (writeFile(output_file, deobfuscated_data)) {
            result.success = true;
            result.message = "File deobfuscated successfully";
        } else {
            result.message = "Failed to write output file: " + output_file;
        }
        
        updateProgress(100);
        
    } catch (const std::exception& e) {
        result.message = "Error: " + std::string(e.what());
    }
    
    result.time_taken = Utils::getCurrentTime() - start_time;
    return result;
}

std::vector<uint8_t> BinaryObfuscatorTool::obfuscateData(const std::vector<uint8_t>& data,
                                                        const ObfuscationConfig& config) {
    auto it = algorithms.find(config.type);
    if (it == algorithms.end()) {
        throw std::runtime_error("Unsupported obfuscation type");
    }
    
    if (!it->second->validateConfig(config)) {
        throw std::runtime_error("Invalid configuration");
    }
    
    return it->second->obfuscate(data, config);
}

std::vector<uint8_t> BinaryObfuscatorTool::deobfuscateData(const std::vector<uint8_t>& data,
                                                          const ObfuscationConfig& config) {
    auto it = algorithms.find(config.type);
    if (it == algorithms.end()) {
        throw std::runtime_error("Unsupported obfuscation type");
    }
    
    if (!it->second->validateConfig(config)) {
        throw std::runtime_error("Invalid configuration");
    }
    
    return it->second->deobfuscate(data, config);
}

std::string BinaryObfuscatorTool::generateRandomKey(size_t length) {
    const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string key;
    key.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        key += chars[random_generator() % chars.length()];
    }
    
    return key;
}

std::vector<std::string> BinaryObfuscatorTool::getSupportedAlgorithms() const {
    std::vector<std::string> algorithms_list;
    for (const auto& pair : algorithms) {
        algorithms_list.push_back(pair.second->getName());
    }
    return algorithms_list;
}

void BinaryObfuscatorTool::setProgressCallback(std::function<void(int)> callback) {
    progress_callback = callback;
}

bool BinaryObfuscatorTool::verifyIntegrity(const std::vector<uint8_t>& original,
                                          const std::vector<uint8_t>& processed,
                                          const ObfuscationConfig& config) {
    try {
        auto recovered = deobfuscateData(processed, config);
        return original == recovered;
    } catch (...) {
        return false;
    }
}

void BinaryObfuscatorTool::initializeAlgorithms() {
    algorithms[ObfuscationType::XOR_SIMPLE] = std::make_unique<XORObfuscation>();
    algorithms[ObfuscationType::XOR_ROTATING] = std::make_unique<XORObfuscation>();
    algorithms[ObfuscationType::BYTE_SHUFFLE] = std::make_unique<ByteShuffleObfuscation>();
    algorithms[ObfuscationType::BIT_SHIFT] = std::make_unique<BitShiftObfuscation>();
    algorithms[ObfuscationType::SUBSTITUTION] = std::make_unique<SubstitutionObfuscation>();
    algorithms[ObfuscationType::MULTI_LAYER] = std::make_unique<MultiLayerObfuscation>();
}

std::vector<uint8_t> BinaryObfuscatorTool::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    file.seekg(0, std::ios::end);
    std::streampos file_size = file.tellg();
    size_t size = static_cast<size_t>(file_size);
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Failed to read file: " + filename);
    }
    
    return data;
}

bool BinaryObfuscatorTool::writeFile(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        return false;
    }
    
    return file.write(reinterpret_cast<const char*>(data.data()), data.size()).good();
}

void BinaryObfuscatorTool::updateProgress(int percentage) {
    if (progress_callback) {
        progress_callback(percentage);
    }
}

// =============================================================================
// 工具函数实现
// =============================================================================

namespace Utils {

std::vector<uint8_t> stringToBytes(const std::string& str) {
    return std::vector<uint8_t>(str.begin(), str.end());
}

std::string bytesToHexString(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t byte : data) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::string calculateMD5(const std::vector<uint8_t>& data) {
    // 简化的校验和实现（实际应用中应使用真正的MD5）
    uint32_t hash = 0;
    for (uint8_t byte : data) {
        hash = hash * 31 + byte;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

bool fileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

size_t getFileSize(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        return 0;
    }
    std::streampos pos = file.tellg();
    return static_cast<size_t>(pos);
}

std::string getFileExtension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    return (pos != std::string::npos) ? filename.substr(pos) : "";
}

std::string createBackup(const std::string& filename) {
    std::string backup = filename + ".backup";
    std::ifstream src(filename, std::ios::binary);
    std::ofstream dst(backup, std::ios::binary);
    dst << src.rdbuf();
    return backup;
}

double getCurrentTime() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

std::string formatTime(double seconds) {
    std::stringstream ss;
    if (seconds < 1.0) {
        ss << std::fixed << std::setprecision(2) << (seconds * 1000) << " ms";
    } else {
        ss << std::fixed << std::setprecision(2) << seconds << " s";
    }
    return ss.str();
}

uint32_t generateSeed() {
    return static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

std::vector<uint8_t> generateRandomBytes(size_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<uint8_t> bytes;
    bytes.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        bytes.push_back(static_cast<uint8_t>(rng()));
    }
    
    return bytes;
}

} // namespace Utils

} // namespace BinaryObfuscator