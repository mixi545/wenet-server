#pragma once
#include <stdlib.h>

#include <string>
#include <cstring>
#include <filesystem>
#include <vector>

namespace sherpa_onnx {
std::filesystem::path mount_fs();
void umount_fs(std::filesystem::path& path);

bool unzip(const std::filesystem::path& zip_file,
           const std::filesystem::path& out_dir,
           std::error_code& ec);
bool copy_file(const std::filesystem::path& src, 
    const std::filesystem::path& dst_dir, 
    std::error_code& ec);

bool string_ends_with(const std::string& s, const std::string& suffix);

void obf_defuse(std::string key, std::string input_file, std::string output_file);
std::vector<char> obf_defuse_to_buffer(std::string key, std::string input_file);
void obf_obfuse(std::string key, std::string input_file, std::string output_file);
void secure_zero_buffer(std::vector<char>* buffer);
void secure_zero_string(std::string* s);
std::filesystem::path defuse_model(std::string key, 
        std::string source_path, 
        std::string obf_file,
        std::string zip_file);
std::vector<std::string> 
collect_by_extern(const std::string& folder, const std::string& ext);
std::string remove_extension(const std::string filename);
}

//void printResult(const ObfuscationResult& result, const CommandLineOptions& options);
