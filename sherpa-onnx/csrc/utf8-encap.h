#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cwctype>
#include <locale>


typedef struct {
  char32_t value;
  int      len;   //0: invalid
} utf8_char_struct;

template <typename T>
std::string vector_2_string(const std::vector<T> &vec, int32_t precision = 6) {
  std::ostringstream oss;
  if (precision != 0) {
    oss << std::fixed << std::setprecision(precision);
  }
  oss << "[";
  std::string sep = "";
  for (const auto &item : vec) {
    oss << sep << item;
    sep = ", ";
  }
  oss << "]";
  return oss.str();
}

template <>  // explicit specialization for T = std::string
std::string vector_2_string<std::string>(const std::vector<std::string> &vec,
                                     int32_t) {  // ignore 2nd arg
  std::ostringstream oss;
  oss << "[";
  std::string sep = "";
  for (const auto &item : vec) {
    oss << sep << std::quoted(item);
    sep = ", ";
  }
  oss << "]";
  return oss.str();
}

std::vector<std::uint32_t> utf8_to_utf32(const std::string& u8) {
    std::vector<std::uint32_t> out;
    for (size_t i = 0; i < u8.size();) {
        unsigned char c = u8[i];
        std::uint32_t cp = 0;
        size_t need = 0;
        if ((c & 0x80) == 0x00) { cp = c; need = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; need = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; need = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; need = 4; }
        else { ++i; continue; }
        if (i + need > u8.size()) break;
        for (size_t k = 1; k < need; ++k) {
            unsigned char ck = u8[i + k];
            if ((ck & 0xC0) != 0x80) { cp = 0; break; }
            cp = (cp << 6) | (ck & 0x3F);
        }
        if (cp) out.push_back(cp);
        i += need;
    }
    return out;
}

std::string utf32_to_utf8(const std::vector<std::uint32_t>& u32) {
    std::string out;
    for (std::uint32_t cp : u32) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline bool is_utf8_start(uint8_t c) {
  return (c & 0xC0) != 0x80;
}

std::string extract_language(const std::string& s) {
  auto first = s.find('|'); 
  auto last  = s.rfind('|'); 
  if (first == std::string::npos || last == std::string::npos || first >= last)
      return ""; 
  return s.substr(first + 1, last - first - 1);
}

bool is_space_char(const char* p, size_t& len) {
  unsigned char c0 = static_cast<unsigned char>(p[0]);
  if (c0 == 0x20) { len = 1; return true; }
  if (c0 == 0xE3 && static_cast<unsigned char>(p[1]) == 0x80 &&
      static_cast<unsigned char>(p[2]) == 0x80) {
    len = 3; return true;
  }
  return false;
}

bool is_utf8_starts_with_space(const std::string& s) {
    if (s.size() < 1) return false;
    size_t len = 0;
    return is_space_char(s.data(), len);
}

static inline bool is_utf8_continuation(uint8_t c) {
    return (c & 0xC0) == 0x80;
}

static inline size_t utf8_char_len(unsigned char c) {
  if ((c & 0x80) == 0x00) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

static std::vector<size_t> utf8_offsets(const std::string& s) {
    std::vector<size_t> off;
    off.reserve(s.size() / 3 + 1);
    for (size_t i = 0; i < s.size(); ) {
        off.push_back(i);
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0x80) == 0)         i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else ++i;                          
    }
    off.push_back(s.size()); 
    return off;
}

#if 0
static std::vector<size_t> utf8_offsets(const std::string& s) {
    std::vector<size_t> off;
    off.reserve(s.size() / 3 + 1);
    for (size_t i = 0; i < s.size(); ) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0)       len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else {
            ++i; continue;
        }
        
        if (i + len > s.size()) break;
        
        bool valid = true;
        for (size_t j = 1; j < len; ++j)
            if ((uint8_t(s[i + j]) & 0xC0) != 0x80) { valid = false; break; }
        if (valid) {
            off.push_back(i);
            i += len;
        } else {
            ++i;                       // 跳过这个无效首字节
        }
    }
    off.push_back(s.size());   // sentinel
    return off;
}
#endif
// utf8 take
std::string utf8_take(const std::string& text, std::size_t count) {
    std::string out;
    out.reserve(count * 3);
    std::size_t taken = 0;
    for (std::size_t i = 0; i < text.size() && taken < count; ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        if ((c & 0x80) == 0)       len = 1; 
        else if ((c & 0xE0) == 0xC0) len = 2; 
        else if ((c & 0xF0) == 0xE0) len = 3; 
        else if ((c & 0xF8) == 0xF0) len = 4; 
        else { ++i; continue; } 
        if (i + len > text.size()) break; 
        out.append(text, i, len);
        i += len;
        ++taken;
    }
    return out;
}

std::string utf8_take_reverse(const std::string& text, std::size_t count) {
    // 1. 先记录每条字符的首字节偏移
    std::vector<size_t> off;
    off.reserve(text.size() / 3 + 1);
    for (size_t i = 0; i < text.size(); ) {
        off.push_back(i);
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0x80) == 0)       i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else ++i; 
    }
    off.push_back(text.size());       // sentinel

    // 2. 计算起始字符序号
    size_t total = off.size() - 1;
    size_t start = (total > count) ? total - count : 0;
    size_t begin_byte = off[start];
    return text.substr(begin_byte); 
}

std::string utf8_drop_back(const std::string& s, std::size_t n) {
    if (n == 0) return s; 
    std::vector<size_t> off;
    off.reserve(s.size() / 3 + 1);
    for (size_t i = 0; i < s.size(); ) {
        off.push_back(i);
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0x80) == 0)       i += 1; 
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else ++i;
    }
    if (n >= off.size()) return {};
    return s.substr(0, off[off.size() - n]);
}

std::string utf8_drop_front(const std::string& s, std::size_t n) {
    if (n == 0) return s;
    std::vector<size_t> off;
    off.reserve(s.size() / 3 + 1);
    for (size_t i = 0; i < s.size(); ) {
        off.push_back(i);
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0x80) == 0)       i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else ++i; 
    }
    if (n >= off.size()) return {}; 
    return s.substr(off[n]); 
}

bool is_last_char_sentence_end(const std::string& s) {
    if (s.empty()) return false;
    auto off = utf8_offsets(s); 
    size_t lastBeg = off[off.size() - 2];
    std::string lastChar(s.data() + lastBeg, s.data() + off.back());
    static const std::string endChars[] = {"。", "！", "？", "!", "?", "."};
    for (auto& ch : endChars)
        if (lastChar == ch) return true;
    return false;
}

void drop_leading_tsheg(std::string& s) {
  if (s.empty()) return;
  size_t firstEnd = utf8_char_len(s[0]);
  if (firstEnd > s.size()) return;
  
  std::string code = s.substr(0, firstEnd);
  if (code == "་") {
    s.erase(0, firstEnd);
  }
  
  SHERPA_ONNX_LOG(INFO) << s << "\n";
}

void add_end_tsheg(std::string& s) {
  if (s.empty()) return;
  auto off = utf8_offsets(s); 
  size_t lastBeg = off[off.size() - 2];
  std::string lastChar(s.data() + lastBeg, s.data() + off.back());

  if (lastChar == "།") s.append(" ");
  if (lastChar == "་") {
    s.erase(lastBeg);
    s.append("། ");
    return;
  }

  s.append("། ");
}

bool string_is_special_character(std::string s) {
  return  s=="." ||
          s=="," ||
          s=="!" ||
          s=="?" ||
          s=="。" ||
          s=="，" ||
          s=="！" ||
          s=="？" ||
          s==" " ;
}

bool drop_leading_special_character(std::string& s) {
  if (s.empty()) return false;
  size_t firstEnd = utf8_char_len(s[0]);
  if (firstEnd > s.size()) return false;
  
  std::string code = s.substr(0, firstEnd);
  if (string_is_special_character(code)) {
    s.erase(0, firstEnd);
    return true;
  }
  
  return false;
}

static const std::map<std::string, std::string> cpunct_map_epunct = {
  {"，", ", "}, {"。", ". "}, {"！", "! "}, {"？", "? "},
  {"；", "; "}, {"：", ": "}, {"“", "\""}, {"”", "\""},
  {"‘", "'"}, {"’", "'"}, {"（", "("}, {"）", ")"},
  {"《", "<"}, {">", ">"}
};

void utf8_erase_unk(std::string& text) {
  const std::string target = "<unk>";
  if (text.empty()) return;
  if (text.size() < target.size()) return;

  auto offs = utf8_offsets(text);
  const size_t target_length = target.size();
  auto target_offs = utf8_offsets(target);
  
  std::string out;
  out.reserve(text.size());

  //std::cout << "1: " << text << '\n';

  for (size_t i = 0; i + 1 < offs.size(); ) {
    size_t start = offs[i];
    size_t end = offs[i + 1];
    
    if ((start + target_length) > text.size()) {
      out.append(text.substr(start, text.size() - start));
      break;
    }

    if (std::memcmp(text.data() + start, target.data(), target_length) != 0) {
      out.append(text.substr(start, end - start));
      ++i;
    } else {
      i = i + target_offs.size();
    }
  }
  
  //std::cout << "2: " << out << '\n';

  text.swap(out);
}

void utf8_collapse_spaces(std::string& text) {
    auto offs = utf8_offsets(text);
    std::string out;
    out.reserve(text.size());
    char32_t prev = U'\0';
    const char32_t SPACE = U' ';

    for (size_t i = 0; i + 1 < offs.size(); ++i) {
        size_t start = offs[i];
        size_t end   = offs[i + 1];
        if (end - start == 1 && (unsigned char)text[start] == 0x20) {
            if (prev == SPACE) continue;
            out.push_back(' ');
            prev = SPACE;
        } else {
            out.append(text.substr(start, end - start));
            prev = U'X';
        }
    }
    text.swap(out);
}

std::string utf8_normalize_text(const std::string& utf8) {
  std::string out;
  out.reserve(utf8.size() * 2);
  size_t i = 0, n = utf8.size();
  bool need_upper = false; 

  while (i < n) {
    size_t len = utf8_char_len(static_cast<unsigned char>(utf8[i]));
    std::string ch = utf8.substr(i, len);
    i += len;

    std::map<std::string, std::string>::const_iterator it = cpunct_map_epunct.find(ch);
    if (it != cpunct_map_epunct.end()) {
      out += it->second;
      need_upper = (it->second == ". " || it->second == "! " || it->second == "? ");
      continue;
    }

    if (ch.size() == 1 && std::isalpha(static_cast<unsigned char>(ch[0]))) {
      char c = ch[0];
      if (need_upper) {
          out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          need_upper = false;
      } else if (std::tolower(static_cast<unsigned char>(c)) == 'i') {
        bool prevIA = out.empty() || !std::isalpha(static_cast<unsigned char>(out.back()));
        bool nextIA = (i >= n) || (utf8_char_len(static_cast<unsigned char>(utf8[i])) > 1) ||
                      !std::isalpha(static_cast<unsigned char>(utf8[i]));
        if (prevIA && nextIA) {
          out += 'I';
          continue;
        }
        // i'm / i'd / i've / i'll 
        if (i + 1 < n && (utf8[i] == '\'')) {
            size_t aposLen = utf8_char_len(static_cast<unsigned char>(utf8[i]));
            std::string after = utf8.substr(i, aposLen);
            i += aposLen;
            if (i < n) {
              size_t nextLen = utf8_char_len(static_cast<unsigned char>(utf8[i]));
              std::string nxt = utf8.substr(i, nextLen);
              if (nxt == "m" || nxt == "d" || nxt == "ve" || nxt == "ll") {
                  i += nextLen;
                  out += "I" + after + nxt;
                  continue;
              }
              
              i -= aposLen;
            }
        }
        out += 'i';   // 普通 i
      } else {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      continue;
    }
      
    out += ch;
  }
  return out;
}

bool has_cn_punct(const std::string& s,
    std::string::const_iterator first,
    std::string::const_iterator last) {
  /*
  static const std::vector<std::string> cn = {
      "\xe3\x80\x81",  // 、
      "\xef\xbc\x8c",  // ，
      "\xef\xbc\x8e",  // 。
      "\xef\xbc\x81",  // ！
      "\xef\xbc\x9f",  // ？
      "\xef\xbc\x9a",  // ：
      "\xef\xbc\x9b",  // ；
      "\xe2\x80\x9c",  // “
      "\xe2\x80\x9d",  // ”
      "\xe2\x80\x98",  // ‘
      "\xe2\x80\x99",  // ’
      "\xef\xbc\x88",  // （
      "\xef\xbc\x89",  // ）
      "\xe3\x80\x90",  // 【
      "\xe3\x80\x91",  // 】
      "\xe3\x80\x8a",  // 《
      "\xe3\x80\x8b"   // 》
  };
  */
  const std::vector<std::string> cn = {
    "\xef\xbc\x9f",  // ？
    "\xef\xbc\x8e",  // 。
    "\xef\xbc\x81",  // ！
    "\xef\xbc\x8c",  // ，
    "\xe3\x80\x81"  // 、
  };
  
  for (const auto& p : cn) 
      if (std::search(first, last, p.begin(), p.end()) != last)
          return true; 

  return false;
}

bool is_chinese_punc(uint32_t cp) {
  return
      (cp == U'。') || // 。
      (cp == U'，') || // ，
      (cp == 0xFF01) || // ！
      (cp == 0xFF1F) || // ？
      (cp == 0xFF1A) || // ：
      (cp == 0xFF1B) || // ；
      (cp == 0x201C) || (cp == 0x201D) || // “ ”
      (cp == 0x2018) || (cp == 0x2019) || // ‘ ’
      (cp == 0xFF08) || (cp == 0xFF09) || // （ ）
      (cp == 0x3010) || (cp == 0x3011) || // 【 】
      (cp == 0x300A) || (cp == 0x300B);   // 《 》
}

bool is_english_punc(uint32_t cp) {
    return
        (cp == '!') || (cp == '?') || (cp == ':') || (cp == ';') ||
        (cp == '"') || (cp == '\'') || (cp == '(') || (cp == ')') ||
        (cp == '[') || (cp == ']') || (cp == '{') || (cp == '}') ||
        (cp == '.') || (cp == ',') ;
}

bool is_punc(char32_t c) {
  if (c == U'.' || c == U'%') return false;
  // 常用 CJK 标点 + 半/全角 ASCII 符号 + 通用标点区段
  return
      (c >= 0x2000 && c <= 0x206F) || // 通用标点
      (c >= 0x3000 && c <= 0x303F) || // CJK 符号
      (c >= 0xFF00 && c <= 0xFFEF) || // 全角形式
      (c >= 0x2010 && c <= 0x2015) || // 各种横线
      (c == 0x2018) || (c == 0x2019) || // ‘ ’
      (c == 0x201C) || (c == 0x201D) || // “ ”
      (c == 0x2026) || // …
      (c == 0x2014) || // —
      //(c == 0x00A0) || // 不换行空格
      //(c == 0x0020) || // 普通空格（可选，想保留则删掉）
      // 半角 ASCII 标点
      (c >= 0x21 && c <= 0x2F) ||
      (c >= 0x3A && c <= 0x40) ||
      (c >= 0x5B && c <= 0x60) ||
      (c >= 0x7B && c <= 0x7E);
}

bool is_punc_common(char32_t c) {
    return c == 0xFF1F   // ？
        || c == 0x3002   // 。
        || c == 0xFF0C   // ，
        || c == 0x3001;  // 、
}

bool is_english_letter(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool is_ascii_alpha(char32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool is_utf8_digit(const std::string& s) {
  if (s.size() == 1) {                       // ASCII
      unsigned char c = static_cast<unsigned char>(s[0]);
      return c >= '0' && c <= '9';           // 0x30-0x39
  }
  return false;
}

bool is_punct_utf32(std::uint32_t cp) {
  return is_chinese_punc(cp) || is_english_punc(cp);
}

std::string utf8_remove_repeated_punct(const std::string& s) {
    auto u32 = utf8_to_utf32(s);
    if (u32.empty()) return {};
    std::vector<std::uint32_t> out;
    out.reserve(u32.size());
    out.push_back(u32[0]);
    for (size_t i = 1; i < u32.size(); ++i) {
        std::uint32_t prev = out.back();
        std::uint32_t curr = u32[i];
        if (is_punct_utf32(prev) && is_punct_utf32(curr))
            continue;
        out.push_back(curr);
    }

    return utf32_to_utf8(out);
}

std::string utf8_remove_newlines(std::string s){
  s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

std::string utf8_remove_substring(std::string s, const std::string& sub){
    if (sub.empty()) return s;
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0, found;
    while ((found = s.find(sub, pos)) != std::string::npos) {
        out.append(s, pos, found - pos);
        pos = found + sub.size();
    }
    out.append(s, pos, s.size() - pos);
    return out;
}

std::string utf8_replace_string(const std::string& str, 
                          const std::string& from, 
                          const std::string& to) {
  std::string result = str;
  size_t pos = 0;
  
  while ((pos = result.find(from, pos)) != std::string::npos) {
    result.replace(pos, from.length(), to);
    pos += to.length();
  }
  
  return result;
}

std::string assemble_strings( std::vector<std::string> tokens) {
  std::string s;
  for (size_t i = 0; i < tokens.size(); ++i) {
    s += tokens[i];
    if (i + 1 < tokens.size()) s += ' ';
  }
  return s;
}

std::string strjoin(const std::vector<std::string>& v,
                 std::string_view sep = "") {
    std::string result;
    if (v.empty()) return result;

    size_t total = (v.size() - 1) * sep.size();
    for (const auto& s : v) total += s.size();
    result.reserve(total);

    result += v[0];
    for (size_t i = 1; i < v.size(); ++i) {
        result += sep;
        result += v[i];
    }
    return result;
}

void print_hex(const std::string& s) {
  std::cout << std::hex << std::uppercase << std::setfill('0');
  for (unsigned char c : s) {
    std::cout << std::setw(2) << static_cast<unsigned int>(c) << ' ';
  }
  std::cout << std::dec << '\n';
}
