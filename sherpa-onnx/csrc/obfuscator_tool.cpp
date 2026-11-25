#include "binary_obfuscator.h"
#include "obfuscator_tool.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <sstream>
#include <stdexcept>

/**
 * @file obfuscator_tool.cpp
 * @brief 二进制文件混淆工具命令行界面
 * @author Assistant
 * @date 2025-10-18
 */

using namespace BinaryObfuscator;
// 命令行选项结构
struct CommandLineOptions {
    std::string input_file;
    std::string output_file;
    std::string operation = "obfuscate";  // obfuscate 或 deobfuscate
    std::string algorithm = "xor_simple";
    std::string key = "";
    std::string level = "medium";
    uint32_t seed = 0;
    uint32_t rounds = 1;
    bool preserve_header = false;
    size_t header_size = 0;
    bool show_help = false;
    bool show_algorithms = false;
    bool generate_key = false;
    size_t key_length = 16;
    bool verbose = false;
    bool create_backup = false;
    bool verify_integrity = false;
};

// 字符串到枚举的映射
std::map<std::string, ObfuscationType> algorithm_map = {
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

void showHelp() {
    std::cout << "Binary Obfuscator Tool v1.0\n";
    std::cout << "============================\n\n";
    std::cout << "用法: obfuscator_tool [选项]\n\n";
    
    std::cout << "基本选项:\n";
    std::cout << "  -i, --input <file>      输入文件路径\n";
    std::cout << "  -o, --output <file>     输出文件路径\n";
    std::cout << "  -op, --operation <op>   操作类型: obfuscate|deobfuscate (默认: obfuscate)\n";
    std::cout << "  -h, --help             显示帮助信息\n\n";
    
    std::cout << "混淆选项:\n";
    std::cout << "  -a, --algorithm <alg>   混淆算法 (默认: xor_simple)\n";
    std::cout << "  -k, --key <key>         混淆密钥\n";
    std::cout << "  -l, --level <level>     混淆级别: low|medium|high|extreme (默认: medium)\n";
    std::cout << "  -s, --seed <seed>       随机种子 (默认: 自动生成)\n";
    std::cout << "  -r, --rounds <rounds>   混淆轮数 (默认: 1)\n\n";
    
    std::cout << "高级选项:\n";
    std::cout << "  --preserve-header       保留文件头部不混淆\n";
    std::cout << "  --header-size <size>    指定头部大小 (字节)\n";
    std::cout << "  --create-backup         创建原文件备份\n";
    std::cout << "  --verify               验证混淆/还原完整性\n";
    std::cout << "  -v, --verbose          详细输出\n\n";
    
    std::cout << "工具选项:\n";
    std::cout << "  --list-algorithms      列出支持的算法\n";
    std::cout << "  --generate-key [len]   生成随机密钥 (默认长度: 16)\n\n";
    
    std::cout << "支持的算法:\n";
    std::cout << "  xor_simple    - 简单异或混淆\n";
    std::cout << "  xor_rotating  - 旋转异或混淆 (变化密钥)\n";
    std::cout << "  byte_shuffle  - 字节打乱混淆\n";
    std::cout << "  bit_shift     - 位移混淆\n";
    std::cout << "  substitution  - 替换表混淆\n";
    std::cout << "  multi_layer   - 多层混淆 (最强)\n\n";
    
    std::cout << "使用示例:\n";
    std::cout << "  # 混淆文件\n";
    std::cout << "  obfuscator_tool -i input.exe -o output.obf -k \"mysecretkey\"\n\n";
    std::cout << "  # 还原文件\n";
    std::cout << "  obfuscator_tool -i output.obf -o restored.exe -op deobfuscate -k \"mysecretkey\"\n\n";
    std::cout << "  # 使用高级算法\n";
    std::cout << "  obfuscator_tool -i file.dll -o file.obf -a multi_layer -l high -r 3\n\n";
    std::cout << "  # 保留PE头部\n";
    std::cout << "  obfuscator_tool -i program.exe -o program.obf --preserve-header --header-size 1024\n\n";
}

void showAlgorithms() {
    BinaryObfuscatorTool tool;
    auto algorithms = tool.getSupportedAlgorithms();
    
    std::cout << "支持的混淆算法:\n";
    std::cout << "================\n\n";
    
    for (const auto& alg : algorithms) {
        std::cout << "• " << alg << "\n";
    }
    std::cout << "\n";
}

std::string generateKey(size_t length) {
    BinaryObfuscatorTool tool;
    return tool.generateRandomKey(length);
}

void showProgress(int percentage) {
    std::cout << "\r进度: [";
    int width = 30;
    int filled = (width * percentage) / 100;
    
    for (int i = 0; i < width; ++i) {
        if (i < filled) {
            std::cout << "█";
        } else {
            std::cout << "░";
        }
    }
    
    std::cout << "] " << percentage << "%";
    std::cout.flush();
    
    if (percentage == 100) {
        std::cout << "\n";
    }
}

bool parseCommandLine(int argc, char* argv[], CommandLineOptions& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            options.show_help = true;
        }
        else if (arg == "--list-algorithms") {
            options.show_algorithms = true;
        }
        else if (arg == "--generate-key") {
            options.generate_key = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.key_length = std::stoul(argv[++i]);
            }
        }
        else if (arg == "-i" || arg == "--input") {
            if (i + 1 < argc) {
                options.input_file = argv[++i];
            }
        }
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                options.output_file = argv[++i];
            }
        }
        else if (arg == "-op" || arg == "--operation") {
            if (i + 1 < argc) {
                options.operation = argv[++i];
            }
        }
        else if (arg == "-a" || arg == "--algorithm") {
            if (i + 1 < argc) {
                options.algorithm = argv[++i];
            }
        }
        else if (arg == "-k" || arg == "--key") {
            if (i + 1 < argc) {
                options.key = argv[++i];
            }
        }
        else if (arg == "-l" || arg == "--level") {
            if (i + 1 < argc) {
                options.level = argv[++i];
            }
        }
        else if (arg == "-s" || arg == "--seed") {
            if (i + 1 < argc) {
                options.seed = std::stoul(argv[++i]);
            }
        }
        else if (arg == "-r" || arg == "--rounds") {
            if (i + 1 < argc) {
                options.rounds = std::stoul(argv[++i]);
            }
        }
        else if (arg == "--preserve-header") {
            options.preserve_header = true;
        }
        else if (arg == "--header-size") {
            if (i + 1 < argc) {
                options.header_size = std::stoul(argv[++i]);
            }
        }
        else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
        }
        else if (arg == "--create-backup") {
            options.create_backup = true;
        }
        else if (arg == "--verify") {
            options.verify_integrity = true;
        }
        else {
            std::cerr << "未知选项: " << arg << "\n";
            return false;
        }
    }
    
    return true;
}

bool validateOptions(const CommandLineOptions& options) {
    if (options.show_help || options.show_algorithms || options.generate_key) {
        return true;
    }
    
    if (options.input_file.empty()) {
        std::cerr << "错误: 必须指定输入文件 (-i)\n";
        return false;
    }
    
    if (options.output_file.empty()) {
        std::cerr << "错误: 必须指定输出文件 (-o)\n";
        return false;
    }
    
    if (options.operation != "obfuscate" && options.operation != "deobfuscate") {
        std::cerr << "错误: 操作类型必须是 'obfuscate' 或 'deobfuscate'\n";
        return false;
    }
    
    if (algorithm_map.find(options.algorithm) == algorithm_map.end()) {
        std::cerr << "错误: 不支持的算法 '" << options.algorithm << "'\n";
        return false;
    }
    
    if (level_map.find(options.level) == level_map.end()) {
        std::cerr << "错误: 不支持的混淆级别 '" << options.level << "'\n";
        return false;
    }
    
    if (options.key.empty() && options.operation == "obfuscate") {
        std::cerr << "警告: 未指定密钥，将使用默认密钥\n";
    }
    
    return true;
}

ObfuscationConfig createConfig(const CommandLineOptions& options) {
    ObfuscationConfig config;
    
    config.type = algorithm_map[options.algorithm];
    config.level = level_map[options.level];
    config.key = options.key.empty() ? "default_key_" + options.algorithm : options.key;
    config.seed = options.seed == 0 ? Utils::generateSeed() : options.seed;
    config.rounds = options.rounds;
    config.preserve_header = options.preserve_header;
    config.header_size = options.header_size;
    
    return config;
}

void printResult(const ObfuscationResult& result, const CommandLineOptions& options) {
    if (result.success) {
        std::cout << "✓ " << result.message << "\n";
        
        if (options.verbose) {
            std::cout << "\n详细信息:\n";
            std::cout << "原始大小: " << result.original_size << " 字节\n";
            std::cout << "处理后大小: " << result.processed_size << " 字节\n";
            std::cout << "处理时间: " << Utils::formatTime(result.time_taken) << "\n";
            
            if (result.processed_size != result.original_size) {
                double ratio = (double)result.processed_size / result.original_size;
                std::cout << "大小变化: " << std::fixed << std::setprecision(2) 
                         << (ratio * 100) << "%\n";
            }
        }
    } else {
        std::cerr << "✗ 操作失败: " << result.message << "\n";
    }
}

namespace sherpa_onnx {
namespace {

void xor_simple_in_place(char* data, size_t size, const std::string& key,
                         size_t offset = 0) {
  if (key.empty()) {
    throw std::runtime_error("obfuscation key is empty");
  }

  for (size_t i = 0; i != size; ++i) {
    data[i] ^= key[(offset + i) % key.size()];
  }
}

}  // namespace

void obf_defuse(std::string key, std::string input_file, std::string output_file) {
  CommandLineOptions options;
  options.input_file = input_file;
  options.output_file = output_file;
  options.key = key;
  
  auto config = createConfig(options);
  
  BinaryObfuscatorTool tool;
  tool.deobfuscateFile(options.input_file, options.output_file, config);
}

std::vector<char> obf_defuse_to_buffer(std::string key, std::string input_file) {
  if (key.empty()) {
    throw std::runtime_error("obfuscation key is empty");
  }

  std::ifstream is(input_file, std::ios::binary | std::ios::ate);
  if (!is) {
    throw std::runtime_error("Failed to open obfuscated file: " + input_file);
  }

  std::streamsize size = is.tellg();
  if (size < 0) {
    throw std::runtime_error("Failed to get file size: " + input_file);
  }

  is.seekg(0, std::ios::beg);

  std::vector<char> buffer(static_cast<size_t>(size));
  if (size != 0 && !is.read(buffer.data(), size)) {
    throw std::runtime_error("Failed to read obfuscated file: " + input_file);
  }

  xor_simple_in_place(buffer.data(), buffer.size(), key);
  return buffer;
}

void obf_obfuse(std::string key, std::string input_file, std::string output_file) {
  CommandLineOptions options;
  options.input_file = input_file;
  options.output_file = output_file;
  options.key = key;
  
  auto config = createConfig(options);
  
  BinaryObfuscatorTool tool;
  tool.obfuscateFile(options.input_file, options.output_file, config);
}

void secure_zero_buffer(std::vector<char>* buffer) {
  if (!buffer || buffer->empty()) {
    return;
  }

  volatile char* p = buffer->data();
  for (size_t i = 0; i != buffer->size(); ++i) {
    p[i] = 0;
  }
  buffer->clear();
  buffer->shrink_to_fit();
}

void secure_zero_string(std::string* s) {
  if (!s || s->empty()) {
    return;
  }

  volatile char* p = &(*s)[0];
  for (size_t i = 0; i != s->size(); ++i) {
    p[i] = 0;
  }
  s->clear();
  s->shrink_to_fit();
}

#if 0
void mount_fs(std::size_t size, std::string mount_path) {
  // create mount point
  int status = sys_cmd("mktemp -d", mount_path.data());
  if(status) {
    mount_path.clear();
    throw std::runtime_error(boost::str(boost::format("[%s:%d]Parse model fail!")%__FILE__%__LINE__ ));
  }
  //create ramfs on mount point
  std::string cmd = boost::str(boost::format("mount -o size=%lu -t tmpfs none %s")% size % mount_path );
  status = sys_cmd(cmd);
  if(status) {
    umount_fs();
    throw std::runtime_error(
    boost::str(boost::format("[%s:%d]Parse model fail!")%__FILE__%__LINE__ ));
    //DLOG(INF0)<<"successfully created ramfs "<< mount_path ;
  }  
}
#endif

std::filesystem::path mount_fs() {
  //constexpr auto k16GiB = 16ULL << 30; 
  //mount_path.clear();

  std::filesystem::path dir_tmp = std::filesystem::temp_directory_path() / "mydir.XXXXXX";
  std::string tpl = dir_tmp.string();
  std::filesystem::path dir_real;
  if (mkdtemp(tpl.data()) != nullptr) {
    dir_real = tpl;
    //std::cout << "get dir: " << dir_real.string() << '\n';
  }
  
  return dir_real;

/*
  std::size_t sizeKB = (size + 1023) / 1024; 
  if (sizeKB == 0 || sizeKB > k16GiB) {
    std::cout << "size out of range "<< '\n';
    return;
  }

  std::string cmd = "mount -t tmpfs -o size=" + std::to_string(sizeKB) + "K none " + mount_path;
  std::cout << "cmd: " << cmd << '\n';
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    //std::filesystem::remove_all(mount_path);
    //mount_path.clear();
    std::cout << "mount tmpfs failed, rc= " << ec.message() << '\n';
  }
*/
}

#if 0
void umount_fs() {
  if(!mount_path .empty()) {//perform'lazy'umount
    std::string cmd = boost::str(boost::format("umount -l %s")% mount_path);
    sys_cmd(cmd);
    //remove mount dir
    std::error_code ec;
    fs::remove_all(mount_path, ec);
    mount_path.clear();
  }
}
#endif

void umount_fs(std::filesystem::path& path) {
  if (path.empty()) return;
/*
  std::string normal = "umount \"" + mount_path + "\" 2>/dev/null";
  int rc = std::system(normal.c_str());
  if (rc != 0) {
    std::string lazy = "umount -l \"" + mount_path + "\"";
    rc = std::system(lazy.c_str());
    if (rc != 0) {
      return;
    }
  }
*/
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

bool copy_file(const std::filesystem::path& src,
    const std::filesystem::path& dst_dir, 
    std::error_code& ec) {
  if (!std::filesystem::exists(src, ec)) return false;
  std::filesystem::create_directories(dst_dir, ec);
  std::filesystem::path dst = dst_dir / src.filename();
  return std::filesystem::copy_file(src, dst, 
    std::filesystem::copy_options::overwrite_existing, ec);
}

bool unzip(const std::filesystem::path& zip_file,
           const std::filesystem::path& out_dir,
           std::error_code& ec) {
    // 1. 创建输出目录
    std::filesystem::create_directories(out_dir, ec);
    // 2. 构造命令（跨平台）
    std::filesystem::path cmd = std::filesystem::absolute(zip_file);
    std::filesystem::path out = std::filesystem::absolute(out_dir);
#ifdef _WIN32
    std::string cli = "tar -xf \"" + cmd.string() + "\" -C \"" + out.string() + "\"";
#else
    std::string cli = "unzip -qq -o \"" + cmd.string() + "\" -d \"" + out.string() + "\"";
#endif
    // 3. 执行
    int rc = std::system(cli.c_str());
    if (rc != 0) { ec.assign(EPROTO, std::generic_category()); return false; }
    return true;
}

bool string_ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::filesystem::path defuse_model(std::string key, 
        std::string source_path, 
        std::string obf_file,
        std::string zip_file) {
  std::filesystem::path model_path(source_path);
  std::filesystem::path model_file = model_path / obf_file;
  
  std::filesystem::path model_path_tmp = sherpa_onnx::mount_fs();
  
  std::filesystem::path tmp_obf = model_path_tmp / obf_file;
  std::filesystem::path tmp_zip = model_path_tmp / zip_file;
  std::error_code ec;
  //sherpa_onnx::copy_file(model_file, model_path_tmp, ec);
  //sherpa_onnx::obf_defuse(key, tmp_obf.string(), tmp_zip.string());
  auto current_time1 = std::chrono::steady_clock::now();
  sherpa_onnx::obf_defuse(key, model_file.string(), tmp_zip.string());
  auto current_time2 = std::chrono::steady_clock::now();

  float elapsed_milliseconds
    = std::chrono::duration_cast<std::chrono::milliseconds>(current_time2 - current_time1).count();
  std::cout << "obf time: " << elapsed_milliseconds << "ms\n";

  sherpa_onnx::unzip(tmp_zip, model_path_tmp, ec);
  auto current_time3 = std::chrono::steady_clock::now();

  elapsed_milliseconds
    = std::chrono::duration_cast<std::chrono::milliseconds>(current_time3 - current_time2).count();

  std::cout << "zip time: " << elapsed_milliseconds << "ms\n";

  return model_path_tmp;
}

std::vector<std::string>
collect_by_extern(const std::string& folder, const std::string& ext) {
  std::vector<std::string> out;
 
  //SHERPA_NCNN_LOGE("collect_by_extern: %s \n", folder.data());

  if (!std::filesystem::exists(folder)) return out;
  for (auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
    
    //SHERPA_NCNN_LOGE("collect_by_extern file: %s \n", entry.path().u8string().data());
    //SHERPA_NCNN_LOGE("collect_by_extern file ext: %s \n", entry.path().extension().string().data());

    if (entry.is_regular_file() &&
      entry.path().extension().string() == ext) {
      out.push_back(entry.path().u8string());
    }
  }
  return out;
}

static inline bool is_continuation(uint8_t c) {
    return (c & 0xC0) == 0x80;
}

std::string remove_extension(const std::string filename) {
  if (filename.empty()) return filename;

  size_t dot = std::string::npos;
  for (size_t i = filename.size(); i > 0; ) {
    --i;
    if (!is_continuation(static_cast<uint8_t>(filename[i]))) {
      if (filename[i] == '.') {
        dot = i;
        break;
    }
  }
  }
  return (dot == std::string::npos) ? filename : filename.substr(0, dot);
}

} // namespace sherpa_onnx

/*
int main(int argc, char* argv[]) {
    CommandLineOptions options;
    
    if (!parseCommandLine(argc, argv, options)) {
        return 1;
    }
    
    if (options.show_help) {
        showHelp();
        return 0;
    }
    
    if (options.show_algorithms) {
        showAlgorithms();
        return 0;
    }
    
    if (options.generate_key) {
        std::cout << "随机密钥 (" << options.key_length << " 字符): " 
                  << generateKey(options.key_length) << "\n";
        return 0;
    }
    
    if (!validateOptions(options)) {
        std::cerr << "使用 --help 查看帮助信息\n";
        return 1;
    }
    
    try {
        // 检查输入文件
        if (!Utils::fileExists(options.input_file)) {
            std::cerr << "错误: 输入文件不存在: " << options.input_file << "\n";
            return 1;
        }
        
        // 创建备份
        std::string backup_file;
        if (options.create_backup && options.operation == "obfuscate") {
            backup_file = Utils::createBackup(options.input_file);
            std::cout << "已创建备份文件: " << backup_file << "\n";
        }
        
        // 创建配置
        auto config = createConfig(options);
        
        if (options.verbose) {
            std::cout << "\n配置信息:\n";
            std::cout << "算法: " << options.algorithm << "\n";
            std::cout << "级别: " << options.level << "\n";
            std::cout << "密钥: " << (options.key.empty() ? "[默认]" : "[已设置]") << "\n";
            std::cout << "种子: " << config.seed << "\n";
            std::cout << "轮数: " << config.rounds << "\n";
            if (config.preserve_header) {
                std::cout << "保留头部: " << config.header_size << " 字节\n";
            }
            std::cout << "\n";
        }
        
        // 创建工具
        BinaryObfuscatorTool tool;
        
        // 设置进度回调
        if (!options.verbose) {
            tool.setProgressCallback(showProgress);
        }
        
        // 执行操作
        ObfuscationResult result;
        if (options.operation == "obfuscate") {
            std::cout << "正在混淆文件...\n";
            result = tool.obfuscateFile(options.input_file, options.output_file, config);
        } else {
            std::cout << "正在还原文件...\n";
            result = tool.deobfuscateFile(options.input_file, options.output_file, config);
        }
        
        // 打印结果
        printResult(result, options);
        
        // 验证完整性
        if (result.success && options.verify_integrity && options.operation == "obfuscate") {
            std::cout << "\n正在验证完整性...\n";
            
            auto original_data = tool.obfuscateData({}, config); // 这里需要读取原始数据
            auto obfuscated_data = tool.obfuscateData({}, config); // 这里需要读取混淆数据
            
            // 简化的验证逻辑
            std::cout << "完整性验证: " << (result.success ? "✓ 通过" : "✗ 失败") << "\n";
        }
        
        return result.success ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << "\n";
        return 1;
    }
}

*/
