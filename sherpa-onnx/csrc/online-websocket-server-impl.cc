// sherpa-onnx/csrc/online-websocket-server-impl.cc
//
// Copyright (c)  2022-2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/online-websocket-server-impl.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "sherpa-onnx/csrc/text-utils.h"
#include "sherpa-onnx/csrc/file-utils.h"
#include "sherpa-onnx/csrc/log.h"

#include "sherpa-onnx/csrc/utf8-encap.h"
#include "sherpa-onnx/csrc/helper-encap.h"
#include "sherpa-onnx/csrc/httplib.h"
#include "sherpa-onnx/nlohmann/json.hpp"

#include "sherpa-onnx/csrc/obfuscator_tool.h"
#include "sherpa-onnx/csrc/sdk-helper.h"

using json = nlohmann::json;


#define SWITCHLINE_MIN 128000  //key:  128000, 160000, 240000
#define SWITCHLINE_MAX 320000  //key:  128000, 160000, 240000
#define SILENCE_MAX 32000
#define MIN_ASR_DATA_LENGTH 16
#define MIN_SPEAKER_DATA_LENGTH 8000
#define MAX_SPEAKERS 5

#define COPY_PYTHON 0

namespace sherpa_onnx {
namespace {

class MemoryBackedDeobfuscatedFiles {
 public:
  explicit MemoryBackedDeobfuscatedFiles(std::string key)
      : key_(std::move(key)) {}

  ~MemoryBackedDeobfuscatedFiles() {
#if defined(__linux__)
    for (int fd : fds_) {
      if (fd >= 0) {
        close(fd);
      }
    }
#endif
    secure_zero_string(&key_);
  }

  std::string Add(const std::string& input_file, const std::string& name) {
    std::vector<char> buffer = obf_defuse_to_buffer(key_, input_file);
    int fd = -1;

    try {
      fd = CreateMemoryFile(name);
      WriteAll(fd, buffer.data(), buffer.size());
      secure_zero_buffer(&buffer);
#if defined(__linux__)
      if (lseek(fd, 0, SEEK_SET) < 0) {
        throw std::runtime_error(std::string("Failed to rewind memory file: ") +
                                 std::strerror(errno));
      }

      fds_.push_back(fd);
      return "/proc/self/fd/" + std::to_string(fd);
#else
      (void)fd;
      throw std::runtime_error(
          "Memory-backed deobfuscation requires Linux memfd_create");
#endif
    } catch (...) {
      secure_zero_buffer(&buffer);
#if defined(__linux__)
      if (fd >= 0) {
        close(fd);
      }
#endif
      throw;
    }
  }

 private:
  static int CreateMemoryFile(const std::string& name) {
#if defined(__linux__)
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
    int fd = static_cast<int>(
        syscall(SYS_memfd_create, name.empty() ? "sherpa-onnx-model" : name.c_str(),
                MFD_CLOEXEC));
    if (fd < 0) {
      throw std::runtime_error(std::string("memfd_create failed: ") +
                               std::strerror(errno));
    }
    return fd;
#else
    (void)name;
    throw std::runtime_error(
        "Memory-backed deobfuscation requires Linux memfd_create");
#endif
  }

  static void WriteAll(int fd, const char* data, size_t size) {
#if defined(__linux__)
    size_t offset = 0;
    while (offset < size) {
      ssize_t n = write(fd, data + offset, size - offset);
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error(std::string("Failed to write memory file: ") +
                                 std::strerror(errno));
      }
      if (n == 0) {
        throw std::runtime_error("Failed to write memory file: short write");
      }
      offset += static_cast<size_t>(n);
    }
#else
    (void)fd;
    (void)data;
    (void)size;
    throw std::runtime_error(
        "Memory-backed deobfuscation requires Linux memfd_create");
#endif
  }

  std::string key_;
  std::vector<int> fds_;
};

std::string GetMeetingId(const std::string &resource) {
  const std::string key = "meeting_id=";
  size_t pos = resource.find("?" + key);
  if (pos == std::string::npos) {
    pos = resource.find("&" + key);
  }
  if (pos == std::string::npos) {
    return {};
  }

  pos += key.size() + 1;
  return resource.substr(pos, resource.find('&', pos) - pos);
}

}  // namespace

#ifdef NEWTRANX_OFFLINE_MODEL
OfflineRecognizer* g_offline_recognizer = nullptr;
#endif
OfflinePunctuation* g_offline_punc = nullptr;
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
SpeakerEmbeddingExtractor* g_speaker_extractor = nullptr;
#endif
struct OnlineWebsocketDecoderConfig* g_decoder_config = nullptr; 


void pcmdata_2_file(Connection* c, 
            std::vector<float>& samples, 
            std::string save_path) {
  if (!save_path.empty() && (samples.size() > 0)) {
    encap::create_dir_if_not_exist(save_path);
    
    static int wav_index = 0;
    std::string wav_file(save_path);
    wav_file.append("/");
    wav_file.append(std::to_string(wav_index++)).append(".wav");
    WriteWave(wav_file, c->sample_rate, samples.data(), samples.size());
  }
}

static std::vector<size_t>
diff_greater_than(const std::vector<float>& v, float threshold) {
    std::vector<size_t> res;
    res.reserve(v.size());
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] - v[i - 1] > threshold) res.push_back(i);
    }
    return res;
}

std::string vector_to_string(std::vector<float> v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v[i]);
    s += buf;
    if (i + 1 < v.size()) s += ' ';
  }
  return s;
}

std::string vector_gap_to_string(std::vector<float> v) {
  std::string s;
  for (size_t i = 1; i < v.size(); ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v[i] - v[i - 1]);
    s += buf;
    if (i + 1 < v.size()) s += ' ';
  }
  return s;
}

template <typename T>
std::string VecToString(const std::vector<T> &vec, int32_t precision = 6) {
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
std::string VecToString<std::string>(const std::vector<std::string> &vec,
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

void create_offline_punct(OnlineWebsocketDecoderConfig& config) {
  if (nullptr != g_offline_punc) return;
  if (config.offline_punctuation_model_config.ct_transformer.empty()) return;

  MemoryBackedDeobfuscatedFiles files(get_license_key());
  config.offline_punctuation_model_config.ct_transformer =
      files.Add(config.offline_punctuation_model_config.ct_transformer,
                "ct_transformer.onnx");
  config.offline_punctuation_model_config.num_threads = 5;
  //config.offline_punctuation_model_config.provider = "cuda";
  OfflinePunctuationConfig offline_punc_config
        = OfflinePunctuationConfig(config.offline_punctuation_model_config);
  g_offline_punc = new OfflinePunctuation(offline_punc_config);
}

VoiceActivityDetector* create_vad(OnlineWebsocketDecoderConfig& config, Connection* c) {
  VoiceActivityDetector* vad = nullptr;
  VadModelConfig vad_config;

  MemoryBackedDeobfuscatedFiles files(get_license_key());
  if (!config.vad_config.silero_vad.model.empty()) {
    vad_config = config.vad_config;
    vad_config.silero_vad.model =
        files.Add(config.vad_config.silero_vad.model, "vad.onnx");
    c->vad_window_size = config.vad_config.silero_vad.window_size;
  }

  if (!config.vad_config.ten_vad.model.empty()) {
    vad_config = config.vad_config;
    vad_config.ten_vad.model =
        files.Add(config.vad_config.ten_vad.model, "vad.onnx");
    c->vad_window_size = config.vad_config.ten_vad.window_size;
  }
  vad_config.num_threads = 5;
  //vad_config.provider = "cuda";

  SHERPA_ONNX_LOG(INFO) << vad_config.ToString() << "\n";
  vad = new sherpa_onnx::VoiceActivityDetector(vad_config, 100);
  return vad;
}

#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
OfflineSpeakerDiarization* create_speaker_diarization(OnlineWebsocketDecoderConfig& config) {
  OfflineSpeakerDiarization* diarization = nullptr;
  if (config.speaker_diarization_config.segmentation.pyannote.model.empty()) return nullptr;
  if (config.speaker_diarization_config.embedding.model.empty()) return nullptr;

  MemoryBackedDeobfuscatedFiles files(get_license_key());
  OfflineSpeakerDiarizationConfig speaker_diarization_config =
      config.speaker_diarization_config;

  speaker_diarization_config.segmentation.pyannote.model =
      files.Add(config.speaker_diarization_config.segmentation.pyannote.model,
                "segmentation.onnx");
  speaker_diarization_config.segmentation.num_threads = 5;
  //speaker_diarization_config.segmentation.provider = "cuda";

  speaker_diarization_config.embedding.model =
      files.Add(config.speaker_diarization_config.embedding.model,
                "embedding.onnx");
  speaker_diarization_config.embedding.num_threads = 5;
  //speaker_diarization_config.embedding.provider = "cuda";

  SHERPA_ONNX_LOG(INFO) << speaker_diarization_config.ToString() << "\n";
  diarization = new OfflineSpeakerDiarization(speaker_diarization_config);

  return diarization;
}

void create_speaker_extractor(OnlineWebsocketDecoderConfig& config) {
  if (nullptr != g_speaker_extractor) return ;
  if (config.speaker_model.empty()) return;

  MemoryBackedDeobfuscatedFiles files(get_license_key());
  config.speaker_extractor_config.model =
      files.Add(config.speaker_model, "wespeaker.onnx");
  config.speaker_extractor_config.num_threads = 5;
  config.speaker_extractor_config.provider = "cuda";

  SHERPA_ONNX_LOG(INFO) << config.speaker_extractor_config.ToString() << "\n";
  g_speaker_extractor = new SpeakerEmbeddingExtractor(config.speaker_extractor_config);
}

SpeakerEmbeddingManager* create_speaker_manager() {
  SpeakerEmbeddingManager* speaker_manager = nullptr;

  if (g_speaker_extractor) {
    speaker_manager = new SpeakerEmbeddingManager(g_speaker_extractor->Dim());
  }

  return speaker_manager;
}
#endif

#ifdef NEWTRANX_OFFLINE_MODEL
void create_offline_recognizer(OfflineRecognizerConfig& config) {
  if (nullptr != g_offline_recognizer) return;

  MemoryBackedDeobfuscatedFiles files(get_license_key());
  if (!config.model_config.tokens.empty()) {
    config.model_config.tokens =
        files.Add(config.model_config.tokens, "tokens.txt");
  }

  if (!config.model_config.sense_voice.model.empty()){
    config.model_config.sense_voice.model =
        files.Add(config.model_config.sense_voice.model, "model.onnx");
  }

  if (!config.model_config.nemo_ctc.model.empty()){
    config.model_config.nemo_ctc.model =
        files.Add(config.model_config.nemo_ctc.model, "model.onnx");
  }

  if (!config.model_config.dolphin.model.empty()){
    config.model_config.dolphin.model =
        files.Add(config.model_config.dolphin.model, "model.onnx");
  }

  if (!config.model_config.omnilingual.model.empty()){
    config.model_config.omnilingual.model =
        files.Add(config.model_config.omnilingual.model, "model.onnx");
  }

  if (!config.model_config.transducer.encoder_filename.empty()
      && !config.model_config.transducer.decoder_filename.empty()
      && !config.model_config.transducer.joiner_filename.empty()){
    config.model_config.transducer.encoder_filename =
        files.Add(config.model_config.transducer.encoder_filename, "encoder");
    config.model_config.transducer.decoder_filename =
        files.Add(config.model_config.transducer.decoder_filename, "decoder");
    config.model_config.transducer.joiner_filename =
        files.Add(config.model_config.transducer.joiner_filename, "joiner");
  }

  if (!config.model_config.funasr_nano.encoder_adaptor.empty()
    && !config.model_config.funasr_nano.llm.empty()
    && !config.model_config.funasr_nano.embedding.empty()) {
    config.model_config.funasr_nano.encoder_adaptor =
        files.Add(config.model_config.funasr_nano.encoder_adaptor, "encoder");
    config.model_config.funasr_nano.embedding =
        files.Add(config.model_config.funasr_nano.embedding, "embedding");
  }

  SHERPA_ONNX_LOG(INFO) << config.ToString() << "\n";
  g_offline_recognizer = new OfflineRecognizer(config);
}
#endif

std::unique_ptr<sherpa_onnx::OnlineRecognizer> create_online_recognizer(OnlineRecognizerConfig& config) {

  std::unique_ptr<sherpa_onnx::OnlineRecognizer> online_recognizer;
  MemoryBackedDeobfuscatedFiles files(get_license_key());
  if (!config.model_config.tokens.empty()) {
    config.model_config.tokens =
        files.Add(config.model_config.tokens, "tokens.txt");
  }

  if (!config.model_config.wenet_ctc.model.empty()) {
    config.model_config.wenet_ctc.model =
        files.Add(config.model_config.wenet_ctc.model, "model.onnx");
  }

  if (!config.model_config.zipformer2_ctc.model.empty()) {
    config.model_config.zipformer2_ctc.model =
        files.Add(config.model_config.zipformer2_ctc.model, "model.onnx");
  }

  if (!config.model_config.nemo_ctc.model.empty()) {
    config.model_config.nemo_ctc.model =
        files.Add(config.model_config.nemo_ctc.model, "model.onnx");
  }

  if (!config.model_config.t_one_ctc.model.empty()) {
    config.model_config.t_one_ctc.model =
        files.Add(config.model_config.t_one_ctc.model, "model.onnx");
  }

  if (!config.model_config.paraformer.encoder.empty()
      && !config.model_config.paraformer.decoder.empty()){
    config.model_config.paraformer.encoder =
        files.Add(config.model_config.paraformer.encoder, "encoder");
    config.model_config.paraformer.decoder =
        files.Add(config.model_config.paraformer.decoder, "decoder");
  }

  if (!config.model_config.transducer.encoder.empty()
      && !config.model_config.transducer.decoder.empty()
      && !config.model_config.transducer.joiner.empty()){
    config.model_config.transducer.encoder =
        files.Add(config.model_config.transducer.encoder, "encoder");
    config.model_config.transducer.decoder =
        files.Add(config.model_config.transducer.decoder, "decoder");
    config.model_config.transducer.joiner =
        files.Add(config.model_config.transducer.joiner, "joiner");
  }

  SHERPA_ONNX_LOG(INFO) << config.ToString() << "\n";
  online_recognizer = std::make_unique<OnlineRecognizer>(config);

  return online_recognizer;
}

#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
std::vector<OfflineSpeakerDiarizationSegment> 
speaker_diarization_ptr(Connection* c, const float *audio, int32_t n) {
  std::vector<OfflineSpeakerDiarizationSegment> segments;

  if (c->speaker_diarization) {
    if (audio && (n > MIN_SPEAKER_DATA_LENGTH)) {
      auto result = c->speaker_diarization->Process(audio, n);
      segments = result.SortByStartTime();

       /*
      for (auto& itr : segments) {
        SHERPA_ONNX_LOG(INFO) << "speaker_diarization, start: " << itr.Start() 
                      << ", speaker id: " << itr.Speaker() 
                      //<< ", text: " << itr.Text() 
                      << "\n";
      }
      // */
    } else {
      SHERPA_ONNX_LOG(ERROR) << "audio samples is null or data length too short: " 
                      << n << "\n";
    }
  }
  
  return segments;
}

std::vector<OfflineSpeakerDiarizationSegment> 
speaker_diarization(Connection* c, std::vector<float> samples) {
  return speaker_diarization_ptr(c, samples.data(), samples.size());
}

std::string speaker_compute_embedding_ptr(SpeakerEmbeddingManager* manager, 
            int sample_rate, const float *samples, int32_t samples_length, float threshold) {
  
  std::string speaker = "UnKnown";
  std::vector<float> embedding;
  if ((nullptr == g_speaker_extractor) || (nullptr == manager)) {
    return speaker;
  } // no logger

  if ((nullptr == samples) 
          || (samples_length < MIN_SPEAKER_DATA_LENGTH)) {
    SHERPA_ONNX_LOG(ERROR) << "audio samples is 0 or data length too short: " 
                      << samples << "\n";
    return speaker;
  }

  auto s = g_speaker_extractor->CreateStream();
  if (!s) {
    SHERPA_ONNX_LOG(ERROR) << "g_speaker_extractor create OnlineStream error" << "\n";
    return speaker;
  }
  
  s->AcceptWaveform(sample_rate, samples, samples_length);
  s->InputFinished();
  if (g_speaker_extractor->IsReady(s.get())) {
    embedding = g_speaker_extractor->Compute(s.get());
    if (!embedding.empty()) {
      speaker = manager->Search(embedding.data(), threshold);
      if (speaker.empty()) {
        int speaker_in_manager = manager->NumSpeakers();
        if (speaker_in_manager < MAX_SPEAKERS) {
          speaker = "speaker-" + std::to_string(speaker_in_manager);
          manager->Add(speaker, embedding.data());
        } else {
          speaker = "UnKnown";
        }
      }
    } 
  }

  return speaker;
}

std::string speaker_compute_embedding(SpeakerEmbeddingManager* manager, 
            int sample_rate, std::vector<float> samples, float threshold) {
  
  return speaker_compute_embedding_ptr(manager, sample_rate, 
                    samples.data(), samples.size(), threshold);
}
#endif

void process_punctuation(OnlineRecognizerResult& result,
                  OnlineWebsocketDecoderConfig& config) {
  if ("" != config.punctuation_server) {
    if ((result.is_final) || (result.is_eof)) {
      httplib::Client cli(config.punctuation_server);
      std::string url_tmp = "/punctuate?text=";
      url_tmp.append(result.text);
      auto res = cli.Post(url_tmp);
      if (res) {
        json j = json::parse(res->body);
        std::string data = j["punctuated"];
        result.text = utf8_remove_substring(data, "\\n ");

        SHERPA_ONNX_LOG(INFO) << "punctuate: " << result.text << "\n";
      }
    }
  } else if (nullptr != g_offline_punc) {
    result.text = g_offline_punc->AddPunctuation(result.text);
  }
}

void asr_post_process(OnlineRecognizerResult& online_result, 
                OnlineWebsocketDecoderConfig& config) {
  while (!online_result.text.empty()) {
    if (!drop_leading_special_character(online_result.text))
      break;
  }
  
  if (online_result.text.empty()) return;

  utf8_erase_unk(online_result.text);

  if (config.language_type == "bo") {
    //online_result.text = utf8_replace_string(online_result.text, " ", "་");
    drop_leading_tsheg(online_result.text);
    if (online_result.is_final || online_result.is_eof)
      add_end_tsheg(online_result.text);
  }

  if (online_result.is_final || online_result.is_eof) {
    process_punctuation(online_result, config);
  }

  online_result.text = utf8_remove_repeated_punct(online_result.text);
}

std::string format_newtranx_result_string(OnlineRecognizerResult& online_result,
                OnlineWebsocketDecoderConfig& config,
                std::string speaker_id="", 
                std::string language="") {
  
  asr_post_process(online_result, config);

  if (online_result.text.empty()) return "";

  std::string online_str = online_result.AsJsonString();

  if (config.language_type == "bo") {
    if (language == "zh") language = "bo-ad";
    if (language == "en") language = "bo-kb";
    if (language == "ko") language = "bo-wz";
  }

  std::string language_str = R"(, "lang":)";
  language_str.append("\"").append(language).append("\"");
  std::string speaker_str = R"(, "speaker":)";
  speaker_str.append("\"").append(speaker_id).append("\"").append("}");

  if (!online_str.empty()) {
    online_str.pop_back();
    online_str += language_str;
    online_str += speaker_str;
  }

  //SHERPA_ONNX_LOG(INFO) << online_str << "\n";
  if (online_result.is_final || online_result.is_eof) {
    SHERPA_ONNX_LOG(INFO) << speaker_id << " : " << online_result.text << "\n";
  }
  
  return online_str;
}

#ifdef NEWTRANX_OFFLINE_MODEL

struct Rune {
    char32_t value;
    int      len;   // 字节长度，0 表示无效
};

//using CodePoint = char32_t;
struct Seq {
    std::vector<char32_t> cp;
    std::vector<int>      offset;   // offset[i] = 第 i 个码点的字节偏移
};

struct SplitResult {
    std::string head;      // 前段（含标点）
    std::string tail;      // 后段（可能空）
    std::size_t pos;       // 被切标点的首字节下标（原串）
    std::size_t rev_pos;   // 从尾部倒数的位置（1 起算，0 表示未找到）
};

struct Ret {
    std::string front_punc_text;   // 0
    std::string back_punc_text;    // 1
    std::string longest_common;    // 2
    int         punc_start;        // 3
    int         punc_end;          // 4
    std::string punctuated_text;   // 5
};

// utf8 split
struct SplitResult
split_last_if_cn_punct(const std::string& text, int range_from_end) {
    const char32_t target[] = { U'？', U'。', U'！', U'，', U'、', 0 };
    auto is_target = [&](char32_t cp) {
        for (const char32_t* p = target; *p; ++p) if (cp == *p) return true;
        return false;
    };

    std::vector<size_t> off = utf8_offsets(text);
    size_t total = off.size() - 1;                  // 字符个数
    size_t start = (total > range_from_end) ? total - range_from_end : 0;     // 只看后字符

    size_t split_char_idx = size_t(-1);             // 在 off 数组中的序号
    for (size_t i = total; i-- > start; ) {         // 逆序扫
        size_t b   = off[i];
        size_t nxt = off[i + 1];
        char32_t cp = 0;
        unsigned char c = static_cast<unsigned char>(text[b]);
        if ((c & 0x80) == 0) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && nxt - b == 2) {
            cp = ((c & 0x1F) << 6) | (text[b + 1] & 0x3F);
        } else if ((c & 0xF0) == 0xE0 && nxt - b == 3) {
            cp = ((c & 0x0F) << 12) | ((text[b + 1] & 0x3F) << 6)
               | (text[b + 2] & 0x3F);
        } else if ((c & 0xF8) == 0xF0 && nxt - b == 4) {
            cp = ((c & 0x07) << 18) | ((text[b + 1] & 0x3F) << 12)
               | ((text[b + 2] & 0x3F) << 6) | (text[b + 3] & 0x3F);
        }
        if (is_target(cp)) { split_char_idx = i; break; } 
    }

    // 未找到
    if (split_char_idx == size_t(-1))
        return { text, "", std::string::npos, 0 };

    size_t split_byte      = off[split_char_idx];          // 首字节下标
    size_t tail_start      = off[split_char_idx + 1];      // 标点之后
    size_t rev_char_pos    = total - split_char_idx;       // 从尾部倒数第几个字符
    return { text.substr(0, tail_start),
             text.substr(tail_start),
             split_byte,
             rev_char_pos };
}

inline Rune decode_utf8(const char* p) {
    uint8_t b0 = static_cast<uint8_t>(p[0]);
    if (b0 < 0x80) return { static_cast<char32_t>(b0), 1 };
    if ((b0 & 0xE0) == 0xC0) {
        uint8_t b1 = static_cast<uint8_t>(p[1]);
        if ((b1 & 0xC0) != 0x80) return { 0, 0 };
        return { static_cast<char32_t>(
                   (static_cast<char32_t>(b0 & 0x1F) << 6) |
                   (b1 & 0x3F)), 2 };
    }
    if ((b0 & 0xF0) == 0xE0) {
        uint8_t b1 = static_cast<uint8_t>(p[1]);
        uint8_t b2 = static_cast<uint8_t>(p[2]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return { 0, 0 };
        return { static_cast<char32_t>(
                   (static_cast<char32_t>(b0 & 0x0F) << 12) |
                   (static_cast<char32_t>(b1 & 0x3F) << 6)  |
                   (b2 & 0x3F)), 3 };
    }
    if ((b0 & 0xF8) == 0xF0) {
        uint8_t b1 = static_cast<uint8_t>(p[1]);
        uint8_t b2 = static_cast<uint8_t>(p[2]);
        uint8_t b3 = static_cast<uint8_t>(p[3]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
            return { 0, 0 };
        return { static_cast<char32_t>(
                   (static_cast<char32_t>(b0 & 0x07) << 18) |
                   (static_cast<char32_t>(b1 & 0x3F) << 12) |
                   (static_cast<char32_t>(b2 & 0x3F) << 6)  |
                   (b3 & 0x3F)), 4 };
    }
    return { 0, 0 };
}

inline int encode_utf8(char32_t cp, char* dst) {
    if (cp < 0x80) { dst[0] = static_cast<char>(cp); return 1; }
    if (cp < 0x800) {
        dst[0] = static_cast<char>(0xC0 | (cp >> 6));
        dst[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        dst[0] = static_cast<char>(0xE0 | (cp >> 12));
        dst[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        dst[0] = static_cast<char>(0xF0 | (cp >> 18));
        dst[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        dst[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        dst[3] = static_cast<char>(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0; // 无效
}

inline std::string encode_utf8_string(char32_t cp) {
    char buf[4];
    int  n = 0;
    if (cp < 0x80) {
        buf[n++] = static_cast<char>(cp);
    } else if (cp < 0x800) {
        buf[n++] = static_cast<char>(0xC0 | (cp >> 6));
        buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf[n++] = static_cast<char>(0xE0 | (cp >> 12));
        buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        buf[n++] = static_cast<char>(0xF0 | (cp >> 18));
        buf[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    return {buf, static_cast<size_t>(n)};
}

inline void encode_utf8_string_sss(char32_t cp, std::string& out) {
    char buf[4];
    int  n = 0;
    if (cp < 0x80) {
        buf[n++] = static_cast<char>(cp);
    } else if (cp < 0x800) {
        buf[n++] = static_cast<char>(0xC0 | (cp >> 6));
        buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf[n++] = static_cast<char>(0xE0 | (cp >> 12));
        buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        buf[n++] = static_cast<char>(0xF0 | (cp >> 18));
        buf[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    out.append(buf, n);
}

inline Seq to_seq(const std::string& s) {
    Seq seq;
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end) {
        seq.offset.push_back(static_cast<int>(p - s.data()));
        Rune r = decode_utf8(p);
        if (r.len == 0) {          // 非法字节，跳过
            ++p;
            continue;
        }
        seq.cp.push_back(r.value);
        p += r.len;
    }
    return seq;
}

// 工具：按 Unicode 字符数取子串（from 字符，取 count 字符）
static std::string utf8_substr_chars(const std::string& s,
                                     size_t from,
                                     size_t count = size_t(-1)) {
    auto off = utf8_offsets(s);
    if (from >= off.size()) return {};
    size_t start = off[from];
    size_t end   = (count == size_t(-1) || from + count >= off.size())
                   ? s.size()
                   : off[from + count];
    return s.substr(start, end - start);
}

static void utf8_pop_back_chars(std::string& s, size_t n) {
    auto off = utf8_offsets(s);
    if (n >= off.size()) { s.clear(); return; }
    s = utf8_substr_chars(s, 0, off.size() - n);
}

// 工具：按 UTF-8 字符迭代，返回每字符首字节偏移（自动跳过非法字节）
static std::vector<size_t> utf8_offsets_filter(const std::string& s) {
    std::vector<size_t> off;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0)       len = 1;   // 0xxxxxxx
        else if ((c & 0xE0) == 0xC0) len = 2; // 110xxxxx
        else if ((c & 0xF0) == 0xE0) len = 3; // 1110xxxx
        else if ((c & 0xF8) == 0xF0) len = 4; // 11110xxx
        else { ++i; continue; }               // 非法首字节，跳过
        if (i + len > s.size()) break;        // 截断，结束
        off.push_back(i);                     // 记录合法字符首字节
        i += len;
    }
    return off;
}

// 工具：按 UTF-8 字符数取子串（from 字符，取 count 字符，自动跳过非法）
static std::string utf8_substr_chars_filter(const std::string& s,
                                            size_t from,
                                            size_t count = size_t(-1)) {
    auto off = utf8_offsets_filter(s);
    if (from >= off.size()) return {};
    size_t start = off[from];
    size_t end   = (count == size_t(-1) || from + count >= off.size())
                   ? s.size()
                   : off[from + count];
    return s.substr(start, end - start);
}

static std::string clear_punc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);                  // 预分配，避免多次 realloc
    const char* p = s.data();
    const char* end = p + s.size();

    while (p < end) {
        Rune r = decode_utf8(p);
        if (r.len == 0) { ++p; continue; }       // 非法字节跳过

        if (is_chinese_punc(r.value)) {
            // 中文标点：直接丢弃
        }
        else if (is_english_punc(r.value)) {
            // 英文标点 → 单空格
            out.push_back(' ');
        }
        else {
            // 其他字符原样输出
            char buf[4];
            int n = encode_utf8(r.value, buf);
            out.append(buf, n);
        }
        p += r.len;
    }
    return out;
}

/* ---------- 取首尾 ASCII 字母 ---------- */
// 返回 0 表示非字母或无效
static char32_t utf8_front(const std::string& s) {
    if (s.empty()) return 0;
    Rune r = decode_utf8(s.data());
    return r.len ? r.value : 0;
}

static char32_t utf8_back(const std::string& s) {
    if (s.empty()) return 0;
    // 从结尾向前找首字节
    auto it = s.end();
    do --it; while (it != s.begin() && ((static_cast<uint8_t>(*it) & 0xC0) == 0x80));
    Rune r = decode_utf8(&*it);
    return r.len ? r.value : 0;
}

static std::string maybe_add_space(const std::string& prev,
                                   const std::string& curr) {
    if (prev.empty() || curr.empty())
        return prev + curr;

    char32_t last  = utf8_back(prev);
    char32_t first = utf8_front(curr);

    if (is_ascii_alpha(last) && is_ascii_alpha(first))
        return prev + " " + curr;
    return prev + curr;
}

/* ---------- 2. 对码点序列求最长公共子串 ---------- */
std::tuple<std::string, int, int>
find_longest_common_substring_utf8(const std::string& a, const std::string& b) {
    Seq sa = to_seq(a);
    Seq sb = to_seq(b);
    int m = static_cast<int>(sa.cp.size());
    int n = static_cast<int>(sb.cp.size());
    if (m == 0 || n == 0) return {"", -1, -1};

    /* DP 只求上一行，空间 O(n) */
    std::vector<int> dp(n + 1, 0);
    int max_len = 0, end_a = -1;   // end_a 是码点下标

    for (int i = 1; i <= m; ++i) {
        for (int j = n; j >= 1; --j) {   // 倒序避免覆盖
            if (sa.cp[i - 1] == sb.cp[j - 1]) {
                dp[j] = dp[j - 1] + 1;
                if (dp[j] > max_len) {
                    max_len = dp[j];
                    end_a = i;           // 子串在 a 中的结束码点序号
                }
            } else {
                dp[j] = 0;
            }
        }
    }
    if (max_len == 0) return {"", -1, -1};

    int start_a = end_a - max_len;
    /* 把码点序号映射回字节偏移 */
    int byte_start = sa.offset[start_a];
    //int byte_end   = sa.offset[end_a - 1] + 1;   // 子串最后一个码点的尾后偏移
    int byte_end   = (end_a < static_cast<int>(sa.offset.size()))
                     ? sa.offset[end_a]
                     : static_cast<int>(a.size());
    return {a.substr(byte_start, byte_end - byte_start), byte_start, byte_end};
}

// 工具：去掉非法 UTF-8 字节（只保留合法序列）
static std::string filter_invalid_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0)       len = 1;   // 0xxxxxxx
        else if ((c & 0xE0) == 0xC0) len = 2; // 110xxxxx
        else if ((c & 0xF0) == 0xE0) len = 3; // 1110xxxx
        else if ((c & 0xF8) == 0xF0) len = 4; // 11110xxx
        else { ++i; continue; }               // 非法首字节，跳过
        if (i + len > s.size()) break;        // 截断，结束
        out.append(s, i, len);                // 保留合法序列
        i += len;
    }
    return out;
}

// 1. 与 Python 版语义一致：合并连续英文字母+空格，首尾去空格
#if 1
std::string reformat_mixed_text_english(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    const char* p = text.data();
    const char* end = p + text.size();

    bool in_en = false;
    std::string en_block; // 暂存英文字母+空格序列（字节级）

    auto flush_en = [&] {
        if (en_block.empty()) return;
        // 首尾去空格（字节级即可，空格是 ASCII）
        size_t st = en_block.find_first_not_of(' ');
        size_t ed = en_block.find_last_not_of(' ');
        if (st != std::string::npos) out += en_block.substr(st, ed - st + 1);
        en_block.clear();
    };

    while (p < end) {
        Rune r = decode_utf8(p);
        if (r.len == 0) { // 非法字节，原样留
            flush_en();
            out.push_back(*p++);
            continue;
        }
        // 英文字母或空格
        if ((r.value >= 'a' && r.value <= 'z') || (r.value >= 'A' && r.value <= 'Z') || r.value == ' ') {
            if (!in_en) { in_en = true; }
            en_block.append(p, r.len);
        } else {
            flush_en();
            in_en = false;
            encode_utf8_string_sss(r.value, out);
        }
        p += r.len;
    }
    flush_en(); // 末尾剩余
    return out;
}
#endif
#if 0
std::string reformat_mixed_text_english(const std::string& text) {
    std::vector<std::string> words;          // 每个元素是一个英文单词（无前后空格）
    std::string current_word;
    const char* p = text.data();
    const char* end = p + text.size();

    auto flush_word = [&] {
        if (!current_word.empty()) {
            words.push_back(current_word);
            current_word.clear();
        }
    };

    while (p < end) {
        Rune r = decode_utf8(p);
        if (r.len == 0) { // 非法字节，当分隔符处理
            flush_word();
            p += 1;
            continue;
        }
        // 英文字母 → 拼成单词
        if ((r.value >= 'a' && r.value <= 'z') || (r.value >= 'A' && r.value <= 'Z')) {
            current_word.append(p, r.len);
        }
        // 空格 → 单词结束
        else if (r.value == ' ') {
            flush_word();
        }
        // 其他字符（中文、标点）→ 单词结束 + 原样输出
        else {
            flush_word();
            // 如果它是英文标点，且前面有空格，**保留一个空格**
            // 这里简单处理：直接原样追加，不再手动加空格
            // 如需更精细的“标点后空格”逻辑，可在此处插入
        }
        p += r.len;
    }
    flush_word();   // 末尾剩余

    // 用单空格重新拼接所有单词
    if (words.empty()) return "";
    std::string out;
    out.reserve(text.size());
    out += words[0];
    for (size_t i = 1; i < words.size(); ++i) {
        out += ' ';
        out += words[i];
    }
    return out;
}
#endif

std::string filter_jp_ko(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end) {
        Rune r = decode_utf8(p);
        if (r.len == 0) { out.push_back(*p++); continue; }
        // 快速区间：日文假名、韩文音节
        if ((r.value >= 0x3040 && r.value <= 0x309F) || // 平假名
            (r.value >= 0x30A0 && r.value <= 0x30FF) || // 片假名
            (r.value >= 0xAC00 && r.value <= 0xD7AF))   // 韩文音节
        {
            p += r.len; // 丢弃
            continue;
        }
        encode_utf8_string_sss(r.value, out);
        p += r.len;
    }
    return out;
}


// 工具：按 UTF-8 字符取最后一个字符（非法时返回空串）
static std::string utf8_last_char(const std::string& s) {
    if (s.empty()) return "";
    std::vector<size_t> off;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0)       len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else { ++i; continue; }                 // 非法，跳过
        if (i + len > s.size()) break;
        off.push_back(i);
        i += len;
    }
    if (off.empty()) return "";
    size_t start = off.back();
    return s.substr(start);
}

// 工具：按 UTF-8 字符取第 idx 个字符（idx<0 返回空）
static std::string utf8_char_at(const std::string& s, int idx) {
    if (idx < 0) return "";
    std::vector<size_t> off;
    for (size_t i = 0; i < s.size(); ) {
        off.push_back(i);
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0)       len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else { ++i; continue; }               // 非法，跳过
        if (i + len > s.size()) break;
        i += len;
    }
    if (idx >= static_cast<int>(off.size())) return "";
    size_t start = off[idx];
    size_t next  = (idx + 1 < off.size()) ? off[idx + 1] : s.size();
    return s.substr(start, next - start);
}

// 主逻辑：与 Python if-else 完全一致
void update_tmp_punc(const std::string& longest_common,
                     int punc_start,
                     int punc_end,
                     const std::string& punctuated_text,
                     std::string& tmp_punc) {
  // 1. 三值均为空（Python 用 None，C++ 用 -1）
  if (longest_common.empty() && punc_start == -1 && punc_end == -1) {
      tmp_punc.clear();                       // self.tmp_punc = ""
      return;
  }

  // 2. 否则检查 punctuated_text[punc_start - 1] 字符
  std::string prev_char = utf8_char_at(punctuated_text, punc_start - 1);
  if (prev_char == "，" || prev_char == "！" || prev_char == "？" ||
      prev_char == "。" || prev_char == "、") {
      if (prev_char != tmp_punc) {
          tmp_punc = prev_char;               // 更新缓存
          return;
      }
  }
  tmp_punc.clear();                           // 不满足条件则清空
}

void check_two_text(const std::string& result,
                    const std::string& back_punc_text) {
    std::string check1 = clear_punc(result);
    std::string check2 = clear_punc(back_punc_text);

    if (check1 == check2) {
        // 对齐，什么都不做（与 Python pass 一致）
        return;
    }

    // 不对齐：打印两行，然后模拟 ipdb.set_trace()
    std::cout << "Mismatch detected:\n"
              << "check1: " << check1 << '\n'
              << "check2: " << check2 << '\n'
              << "Press Enter to continue (simulating ipdb.set_trace)..." << std::endl;
}

void extract_puncts_utf8(const std::string& punctuated_text,
                         std::vector<std::string>& matched_text_puncts,
                         std::vector<int>&       matched_text_puncts_index,
                         std::vector<int>&       prev_char_index) {
    matched_text_puncts.clear();
    matched_text_puncts_index.clear();
    prev_char_index.clear();                 // 清空

    int char_idx = 0;                        // 当前码点序号
    const char* p = punctuated_text.data();
    const char* end = p + punctuated_text.size();

    while (p < end) {
        Rune r = decode_utf8(p);
        if (r.len == 0) {                    // 非法字节，跳过
            ++p;
            continue;
        }
        if (is_punc_common(r.value)) {
            /* 记录标点本身 */
            char buf[4];
            int  n = 0;
            if (r.value < 0x80) {
                buf[n++] = static_cast<char>(r.value);
            } else if (r.value < 0x800) {
                buf[n++] = static_cast<char>(0xC0 | (r.value >> 6));
                buf[n++] = static_cast<char>(0x80 | (r.value & 0x3F));
            } else if (r.value < 0x10000) {
                buf[n++] = static_cast<char>(0xE0 | (r.value >> 12));
                buf[n++] = static_cast<char>(0x80 | ((r.value >> 6) & 0x3F));
                buf[n++] = static_cast<char>(0x80 | (r.value & 0x3F));
            } else {
                buf[n++] = static_cast<char>(0xF0 | (r.value >> 18));
                buf[n++] = static_cast<char>(0x80 | ((r.value >> 12) & 0x3F));
                buf[n++] = static_cast<char>(0x80 | ((r.value >> 6) & 0x3F));
                buf[n++] = static_cast<char>(0x80 | (r.value & 0x3F));
            }
            matched_text_puncts.emplace_back(buf, n);
            matched_text_puncts_index.push_back(char_idx);
            prev_char_index.push_back(char_idx - 1);   // ← 前一个字符的码点序号
        } else {
            ++char_idx;
        }
        p += r.len;
    }
}

std::string format_punctuated_text(const std::string& result) {
    std::string out;
    out.reserve(result.size());

    const char* p = result.data();
    const char* end = p + result.size();

    while (p < end) {
        Rune r = decode_utf8(p);
        if (r.len == 0) { // 非法字节，原样留
            out.push_back(*p++);
            continue;
        }
        char32_t cp = r.value;
        const char* next = p + r.len;
        Rune rn = decode_utf8(next);
        char32_t next_cp = (next < end && rn.len > 0) ? rn.value : 0;

        /* 规则顺序与 Python 保持一致 */
        // 1. " %" -> "%"
        if (cp == ' ' && next_cp == '%') {
            out.push_back('%');
            p = next + rn.len; // 跳过整个 "%"
            continue;
        }
        // 2. " . " -> "."
        if (cp == ' ' && next_cp == '.' && next + rn.len < end) {
            Rune rnn = decode_utf8(next + rn.len);
            if (rnn.value == ' ') { // 完整 " . "
                out.push_back('.');
                p = next + rn.len + rnn.len; // 跳过 " . "
                continue;
            }
        }
        // 3. " ." -> "."
        if (cp == ' ' && next_cp == '.') {
            out.push_back('.');
            p = next + rn.len; // 跳过 " ."
            continue;
        }
        // 4. "- " -> "-"
        if (cp == '-' && next_cp == ' ') {
            out.push_back('-');
            p = next + rn.len; // 跳过 "- "
            continue;
        }

        /* 无匹配，输出当前码点 */
        encode_utf8_string_sss(cp, out);
        p += r.len;
    }
    return out;
}

/* ---------- UTF-8 安全插入标点 ---------- */
void insert_puncts_utf8(std::string& punc_text_front,
                        const std::vector<std::string>& matched_text_puncts,
                        const std::vector<int>& matched_text_puncts_index) {
    // 1. 建立码点 -> 字节偏移映射
    std::vector<int> offset_map;          // offset_map[码点序号] = 字节偏移
    const char* p = punc_text_front.data();
    const char* begin = p;
    const char* end = p + punc_text_front.size();
    while (p < end) {
        offset_map.push_back(static_cast<int>(p - begin));
        Rune r = decode_utf8(p);
        if (r.len == 0) { ++p; continue; }   // 非法字节跳过
        p += r.len;
    }
    // 尾后偏移也存一份，方便 substr
    offset_map.push_back(static_cast<int>(p - begin));

    // 2. 从后往前插入，避免偏移失效
    int k = static_cast<int>(matched_text_puncts.size()) - 1;
    for (; k >= 0; --k) {
        int idx = matched_text_puncts_index[k];
        if (idx >= static_cast<int>(offset_map.size()) - 1) continue; // 越界忽略
        // 把标点转成 UTF-8
        char32_t cp = 0;
        // 这里演示：matched_text_puncts[k] 是单码点字符串，先解码
        Rune r = decode_utf8(matched_text_puncts[k].data());
        if (r.len == 0) continue;
        std::string punct_utf8 = encode_utf8_string(r.value);

        // 在字节偏移处插入
        int byte_pos = offset_map[idx + 1]; // 插到该码点之后
        punc_text_front.insert(byte_pos, punct_utf8);
    }
}

OnlineRecognizerResult online_result_from_offline(
    OfflineRecognitionResult asr_result, 
    bool is_final, 
    bool is_eof) {
  OnlineRecognizerResult online_result;

  //online_result.context_scores = asr_result.
  online_result.is_eof = is_eof;
  online_result.is_final = is_final;
  //online_result.lm_probs = asr_result.
  //online_result.segment = asr_result.
  //online_result.start_time = asr_result.
  online_result.text = asr_result.text;
  online_result.timestamps = asr_result.timestamps;
  online_result.tokens = asr_result.tokens;
  online_result.words = asr_result.words;
  //online_result.ys_probs = asr_result.

  return online_result;
}

#if 0
std::string send_message(Connection* c, std::string text) {

  std::ostringstream os;
  os << "{ ";
  os << "\"retCode\": " << "0" << ", ";   //返回码 0成功 -1 失败
  os << "\"retMsg\": " << "-" << ", ";    //返回信息 显示实际出现异常的信息
  os << "\"meetingId\": " << c->meeting_id << ", ";
  os << "\"text\": " << text << ", ";
  os << "\"segment\": " << "" << ", ";
  os << "\"operation\": " << "<ASR>" << ", ";
  os << "\"speaker\": " << "unknown" << ", ";
  os << "\"duration\": " << "None" << ", ";
  os << "\"finished\": " << std::to_string(c->line_finished);
  os << "}";
  return os.str();
}
#endif

OfflineRecognitionResult decode_waveform_to_text(
            int sample_rate, 
            std::vector<float> vad_samples) {
  OfflineRecognitionResult asr_result;
#ifndef NEWTRANX_OFFLINE_MODEL
  (void)sample_rate;
  (void)vad_samples;
  return asr_result;
#else
  if (nullptr == g_offline_recognizer) return asr_result;
  if (vad_samples.size() < MIN_ASR_DATA_LENGTH) {
    SHERPA_ONNX_LOG(ERROR) << "audio samples data length is too short: " 
            << vad_samples.size() << "\n";
    return asr_result;
  } 

  //std::unique_ptr<OfflineStream> offline_stream = offline_recognizer_->CreateStream();
  std::unique_ptr<OfflineStream> offline_stream = g_offline_recognizer->CreateStream();
  if (!offline_stream) {
    SHERPA_ONNX_LOG(ERROR) << "g_offline_recognizer create offline_stream error" << "\n";
    return asr_result;
  }

  offline_stream->AcceptWaveform(sample_rate, vad_samples.data(), vad_samples.size());
  
  auto current_time = std::chrono::steady_clock::now();    
  //offline_recognizer_->DecodeStream(offline_stream.get());
  g_offline_recognizer->DecodeStream(offline_stream.get());
/*
  try {
    g_offline_recognizer->DecodeStream(offline_stream.get());
  } catch (const Ort::Exception &ex) {
    SHERPA_ONNX_LOGE("\n\nCaught exception:\n\n%s\n\nReturn an empty result",
                      ex.what());
    //return;
  }
*/
  asr_result = offline_stream->GetResult();

  asr_result.lang = extract_language(asr_result.lang);
#if 0 
  const float elapsed_milliseconds
            = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - current_time).count();
   
  SHERPA_ONNX_LOG(INFO) << "decode_waveform_to_text result: " << asr_result.text 
                        << ", elapsed_ms:" << elapsed_milliseconds 
                        << ", size: " << vad_samples.size() << "\n"; 
#endif

  return asr_result;
#endif
}

OnlineRecognizerResult format_message(
    std::string& message,
    bool is_final, 
    bool is_eof) {
  
  OnlineRecognizerResult online_result;
  if (!message.empty()) {
    online_result.text = message;  
    online_result.is_eof = is_eof;
    online_result.is_final = is_final;
  }

  return std::move(online_result);
}

void send_text_result(void* doer, Connection* c, std::string& asr_text) {
  if (!asr_text.empty()) {
    if (c->is_android){
      reinterpret_cast<StreamRecognizer*>(doer)->PutText(asr_text);
    } else {
      reinterpret_cast<OnlineWebsocketDecoder*>(doer)->SocketSend(c, asr_text);
    }
  }
}

#if COPY_PYTHON
void limit_buffer_when_not_started(Connection* c) {
  if (!c->started) {
    if (c->buffer.size() > 10 * c->vad_window_size) {
      c->offset -= c->buffer.size() - 10 * c->vad_window_size;
      c->offset = (c->offset > 0) ? c->offset : 0; // add to fix bug
      c->buffer = {c->buffer.end() - 10 * c->vad_window_size, c->buffer.end()};
    }
  }
}

void process_asr_text_line_for_display (Connection* c){
  std::string split_text_by_punc_in_end_region;
  if (c->joined_vad_segments.size() >= c->max_line_result_length){
    // 开始 check 本行末尾一定区域是否含标点，若含，则截断这部分，用于下一行显示
    auto [a, split_text_by_punc_in_end_region, pos, rev_pos] 
            = split_last_if_cn_punct(c->line_result.back(), c->end_region_length);
    c->joined_vad_segments = utf8_drop_back(c->joined_vad_segments, rev_pos);
    
    c->line_result = {};
    c->line_result.push_back(split_text_by_punc_in_end_region);

    split_text_by_punc_in_end_region = "";
    //c->final_combined_text.append(c->joined_vad_segments).append("\n");
    c->line_segment += 1;
  } else {
    c->line_result = {};
  }
}

std::string get_recent_vad_texts_combined(Connection* c, std::size_t history_chunks) {
  if (c->vad_punc_texts.size() < c->max_punct_chunks) {
      // 拼接全部
      std::string result;
      result.reserve(c->vad_punc_texts.size() * 16); // 粗略预分配
      for (const auto& txt : c->vad_punc_texts) 
        result += txt;
      return result;
  } else {
      // 只拼接最后 history_chunks 段
      std::string result;
      result.reserve(history_chunks * 16);
      auto start = c->vad_punc_texts.cend() - history_chunks;
      for (auto it = start; it != c->vad_punc_texts.cend(); ++it) result += *it;
      return result;
  }
}

std::string format_asr_text(const std::string& result) {
    std::string s = result;
    s = clear_punc(s);
    s = reformat_mixed_text_english(s);
    s = filter_jp_ko(s);
    // % 后加空格：%1 → % 1  （ASCII 场景，无需改动）
    static const std::regex pct(R"(%(?=\d))");
    s = std::regex_replace(s, pct, "% ");
    return s;
}

// 对 VAD 文本与标点进行对齐和分段
Ret align_and_segment_vad_text_with_punctuation(Connection* c,
                  const std::string& result, std::size_t history_chunks) {
    
  // 1. 获取 recent vad 文本
  std::string prev_vad_result = get_recent_vad_texts_combined(c, history_chunks); ///

  // 2. 首尾字母检查 → 加空格
  std::string raw_text_for_punctuation = maybe_add_space(prev_vad_result, result);

  // 3. 清洗标点 → 标点模型 → 再清洗（用于长度 check）
  raw_text_for_punctuation = clear_punc(raw_text_for_punctuation);
  //raw_text_for_punctuation = clear_punc_transform(raw_text_for_punctuation);
  
  std::string punctuated_text = g_offline_punc->AddPunctuation(raw_text_for_punctuation);
  punctuated_text = utf8_drop_back(punctuated_text, 1);

  // 有一个惊人的BUG:
  // 当"否则无法形成有序的牛熊大周期波动拉长周期看"作为标点模型的输入，它的输出会吞掉"看"
  // 如上，"未来一段时间生产消费需求不振争将是宏观经济主要矛盾中国将处在低利率环境和降息通道中" 吞掉"中"
  // 还有一个更过分的现象:  "到3倍PB的行业最高估值另外不容忽视的一点是", 吞掉"另外不容忽视的一点是"
  // 针对上述问题，我做了以下 check 部分, 若发现长度不对齐，直接：prev_vad_result + result
  std::string punct_check = reformat_mixed_text_english(clear_punc(punctuated_text));
  //std::string punct_check = reformat_mixed_text_english(clear_punc_transform(punctuated_text));
  if (raw_text_for_punctuation.size() != punct_check.size()) {
    // 长度不一致，回退
    //SHERPA_ONNX_LOGE("raw_text_for_punctuation: %s", raw_text_for_punctuation.data());
    //SHERPA_ONNX_LOGE("punctuated_text: %s", punctuated_text.data());
    //SHERPA_ONNX_LOGE("punctuated_text 2: %s", clear_punc_transform(punctuated_text).data());
    //SHERPA_ONNX_LOGE("punct_check: %s", punct_check.data());

    punctuated_text = prev_vad_result + result;
    SHERPA_ONNX_LOGE("长度不一致，回退");
  }

  punctuated_text = format_punctuated_text(punctuated_text); 

  // 4. 若 history 不足，直接返回
  if (c->vad_punc_texts.size() < history_chunks) {
      return {"", punctuated_text, "", -1, -1, punctuated_text};
  }

  // 有punc的时候记录前一个字的下标
  std::vector<std::string> matched_text_puncts;
  std::vector<int> matched_text_puncts_index;
  std::vector<int> prev_char_index;
  std::string matched_text_source = punctuated_text;
  extract_puncts_utf8(matched_text_source, matched_text_puncts, matched_text_puncts_index, prev_char_index);
  
  matched_text_puncts_index.swap(prev_char_index);

  // ​target_substring
  std::string target_substring = result;
  matched_text_source = clear_punc(matched_text_source);
  auto [longest_common_substring, punc_start, punc_end] 
        = find_longest_common_substring_utf8(matched_text_source, target_substring);

  //SHERPA_ONNX_LOGE("matched_text_source: %s", matched_text_source.data()); 
  //SHERPA_ONNX_LOGE("target_substring: %s", target_substring.data());
  //SHERPA_ONNX_LOGE("longest_common_substring: %s", longest_common_substring.data()); 


  std::string punc_text_front = (longest_common_substring.empty() && punc_start == -1 && punc_end == -1) ?
                                matched_text_source :
                                matched_text_source.substr(0, punc_start);

  // 7. 把标点插回 front
  insert_puncts_utf8(punc_text_front, matched_text_puncts, matched_text_puncts_index);

  std::string front_punc_text = punc_text_front;
  std::string back_punc_text  = punctuated_text.substr(front_punc_text.size());

  //SHERPA_ONNX_LOGE("front_punc_text: %s", front_punc_text.data());
  //SHERPA_ONNX_LOGE("back_punc_text: %s", back_punc_text.data());


  return {front_punc_text,
          back_punc_text,
          longest_common_substring,
          punc_start,
          punc_end,
          punctuated_text};
}

void compose_vad_lines_with_max_length(Connection* c, 
            const std::string& front_punc_text, const std::string& back_punc_text) {
  for (auto& seg : c->line_result) 
    seg = filter_invalid_utf8(seg);
  
  std::string front_filt = filter_invalid_utf8(front_punc_text);
  std::string back_filt  = filter_invalid_utf8(back_punc_text);

  if (c->line_result.size() < c->max_punct_chunks) {
    c->line_result.push_back(std::move(back_filt));
  } else {
    c->line_result.pop_back();
    c->line_result.push_back(std::move(front_filt));
    c->line_result.push_back(std::move(back_filt));
  }

  std::string joined;
  for (const auto& seg : c->line_result) 
    joined += seg;

  c->joined_vad_segments = joined;
}

void update_vad_punc_texts_with_tmp_punc(
        Connection* c, 
        const std::string& front_punc_text,
        const std::string& back_punc_text) {

  std::string front_filt = filter_invalid_utf8(front_punc_text);
  std::string back_filt  = filter_invalid_utf8(back_punc_text);

  c->tmp_punc = "";
  if (front_filt.empty()) {
    c->vad_punc_texts.push_back(std::move(back_filt));
    
  } else {
    c->vad_punc_texts.pop_back();
    c->vad_punc_texts.push_back(std::move(front_filt));
    c->vad_punc_texts.push_back(std::move(back_filt));

    std::string last_char = utf8_last_char(front_filt);
    if (last_char == "？" || last_char == "，" || last_char == "。" ||
        last_char == "、" || last_char == "！") {
        c->tmp_punc = last_char;
    } 
  }
}

std::string return_asr_line_based_on_silence(Connection* c) {

   process_asr_text_line_for_display(c);

  if ((c->current_silent_length > c->rule2_min_trailing_silence) 
    && (c->current_silent_length < c->rule1_min_trailing_silence)) {
    c->joined_vad_segments = c->joined_vad_segments + "，";
  } else if (c->current_silent_length > c->rule1_min_trailing_silence) {
    c->joined_vad_segments = c->joined_vad_segments + "。";
  }

  c->line_finished = 1;
  std::string message = format_message(c->joined_vad_segments, c->line_finished, c->line_finished).AsJsonString();
  c->current_result = "";
  c->joined_vad_segments = "";
  //c->current_silent_length = 0;
  return message;
}

std::string process_detect_vad_and_return_lines(Connection* c) {
  
  std::string message;  
  c->vad->AcceptWaveform(c->buffer.data() + c->offset, c->vad_window_size);
  c->current_silent_length += c->vad_window_size;

  if (!c->started && c->vad->IsSpeechDetected()) {
    c->started = true;
    c->started_time = std::chrono::steady_clock::now();
    // 当 started 为 true, 这意味着要检测新的vad，这时候可以返回之前的内容了(行级别)
    if (!c->joined_vad_segments.empty()) {
      message = return_asr_line_based_on_silence(c);
    }
  }
  
  if (c->vad->IsSpeechDetected()) c->current_silent_length = 0; // added by yangxiaoming

  return message;
}

std::string check_and_return_timeout_line(Connection* c) {
  std::string message;
  // 如果还没开始且上一段文本不为空
  if (!c->started && !c->current_result.empty()) {
    // 如果静音段太长
    if (c->current_silent_length > c->line_switch_timeout) {
      c->line_finished = 1;
      c->line_result = {};
      //c->final_combined_text.append(c->current_result);
      c->line_segment += 1;
      c->current_result.append("。  (silent too long, go next line)");
      message = format_message(c->current_result, c->line_finished, c->line_finished).AsJsonString();
      c->current_result = "";
      c->joined_vad_segments = "";
      c->current_silent_length = 0;
      c->multi_vad_segment = {};
      c->vad_segment += 1;
    }
  }
  return message;
}

std::string process_and_send_online_asr_result(Connection* c) {
  std::string message;
  std::chrono::steady_clock::time_point current_time 
    = std::chrono::steady_clock::now();
  const float elapsed_seconds
            = std::chrono::duration_cast<std::chrono::seconds>(
              current_time - c->started_time).count();

  if (c->started && (elapsed_seconds > 0.2)) {
    c->current_silent_length = 0;
    c->line_finished = 0;
    std::vector<float> multi_vad_segment_tmp;
    multi_vad_segment_tmp.reserve(c->multi_vad_segment.size() + c->buffer.size());
    multi_vad_segment_tmp.insert(multi_vad_segment_tmp.end(), c->multi_vad_segment.begin(), c->multi_vad_segment.end());
    multi_vad_segment_tmp.insert(multi_vad_segment_tmp.end(), c->buffer.begin(), c->buffer.end());
    
    std::string result = decode_waveform_to_text(c->sample_rate, multi_vad_segment_tmp).text;
    result = utf8_drop_back(result, 1); // result = result[:-1]
    result = format_asr_text(result);

    SHERPA_ONNX_LOGE("process_and_send_online_asr_result:  %s", result.data());

    // 取 vad_punc_texts 的后2个
    int history_chunks = 2;
    auto [front_punc_text, back_punc_text, longest_common_substring, punc_start, punc_end, punctuated_text]
        = align_and_segment_vad_text_with_punctuation(c, result, history_chunks);

    std::string online_result;
    if (front_punc_text.empty()) {
      online_result = result;
    } else {
      online_result = back_punc_text;
      update_tmp_punc(longest_common_substring, punc_start, punc_end, punctuated_text, c->tmp_punc);
    }

    // 下面加split_text_by_punc有一个弊端，就是没放入punc模型，没起到充分利用前文的效果，但可以暂时不管
    if (c->line_result.empty()){
      message = format_message(online_result, c->line_finished, c->line_finished).AsJsonString();
      c->current_result = online_result;   // deleted by yangxiaoming@20250928
    } else {
      std::string res_tmp = strjoin(c->line_result, c->tmp_punc);
      res_tmp.append(online_result);
      message = format_message(res_tmp, c->line_finished, c->line_finished).AsJsonString();
      c->current_result = res_tmp;    // deleted by yangxiaoming@20250928
    }
    
    c->started_time = std::chrono::steady_clock::now();
  } else {
    //SHERPA_ONNX_LOGE("process_and_send_online_asr_result <= 0.02");
  }

  return message;
}

std::string process_vad_queue_and_send_asr_result(Connection* c) {
  std::string message;
  if (!c->vad->Empty()) {
    const auto &segment = c->vad->Front();
    
    // 在 multi_vad_segment 前面加点静音段，可抑制开头吞字现象
    // current_samples_np = np.array(self.current_samples)  # 列表转 NumPy 数组
    // silence_samples = np.zeros(int(0.1 * 16000), dtype=current_samples_np.dtype)
    // self.multi_vad_segment = np.concatenate([silence_samples, self.multi_vad_segment, self.current_samples])
    c->current_samples = {segment.samples.begin(), segment.samples.end()};
    c->multi_vad_segment.insert(c->multi_vad_segment.end(),
                                  c->current_samples.begin(),
                                  c->current_samples.end());
    c->vad->Pop();
    
    c->buffer.erase(c->buffer.begin(), c->buffer.begin() + c->offset);
    c->offset = 0;
    c->started = false;

    std::string result;
    if (c->multi_vad_segment.size() <= 128000) { // <= 8s, this step maybe lost the last data.
      //c->buffer = {};
      //c->offset = 0;
      //c->started = false;
      SHERPA_ONNX_LOGE("process_vad_queue_and_send_asr_result:  < 8s");
      return message;
    } else {
      result = decode_waveform_to_text(c->sample_rate, c->multi_vad_segment).text; 
      c->multi_vad_segment = {};
    }

    result = utf8_drop_back(result, 1); // result = result[:-1]
    result = format_asr_text(result);

    SHERPA_ONNX_LOGE("process_vad_queue_and_send_asr_result:  %s", result.data());

    auto [front_punc_text, back_punc_text, longest_common_substring, punc_start, punc_end, punctuated_text]
        = align_and_segment_vad_text_with_punctuation(c, result, 1);
    
    // append 进 vad_punc_texts, 以及更新tmp punc(此用于 online recognition)
    update_vad_punc_texts_with_tmp_punc(c, front_punc_text, back_punc_text);

    SHERPA_ONNX_LOGE("process_vad_queue_and_send_asr_result front_punc_text:  %s", front_punc_text.data());
    SHERPA_ONNX_LOGE("process_vad_queue_and_send_asr_result back_punc_text:  %s", back_punc_text.data());

    // 此处确认 识别结果 与 处理后的识别结果 是否能对齐上 (此部分不用于实际部署):
    //check_two_text(result, back_punc_text);
    // 处理 当前行的pop与append操作，超出长度的处理等
    compose_vad_lines_with_max_length(c, front_punc_text, back_punc_text);

    c->line_finished = 0;
    c->current_result = c->joined_vad_segments;
    message = format_message(c->joined_vad_segments, c->line_finished, c->line_finished).AsJsonString();
    
    //c->buffer = {};
    //c->offset = 0;
    //c->started = false;
    c->started_time = std::chrono::steady_clock::time_point::max();
    c->vad_segment += 1;
    // audio_segment_with_metadata.append(self.line_segment)
    // segments_with_asr_and_timing[self.vad_segment] = audio_segment_with_metadata
  }
  return message;
}

void asr_decode_and_process(
    void* decoder,
    Connection* c,
    std::vector<float>& pcm_data_process) {

  c->buffer.insert(c->buffer.end(), pcm_data_process.begin(), pcm_data_process.end());

  std::string message;
  for (; (c->offset + c->vad_window_size) < c->buffer.size(); ) {
    // 处理 vad 接收以及返回message
    message = process_detect_vad_and_return_lines(c);    
    if (!message.empty()) {
      SHERPA_ONNX_LOGE("检测到start, vad结束: %s", message.data());
      send_text_result(decoder, c, message);
    }
    
    // 检查并处理静音段过长的
    message = check_and_return_timeout_line(c);
    if (!message.empty()) {
      SHERPA_ONNX_LOGE("检测到静音段, vad结束: %s", message.data());
      send_text_result(decoder, c, message);
    }
    
    c->offset += c->vad_window_size;
  }

  limit_buffer_when_not_started(c);

  // 此处是一行里的 online 识别
  message = process_and_send_online_asr_result(c);
  if (!message.empty()) {
    SHERPA_ONNX_LOGE("online 识别: %s", message.data());
    send_text_result(decoder, c, message);
  }

  // 检测到vad，检测是否大于 multi_vad_segment ，如果大于，表明当前行结束(started = False)，
  // 但为了在末尾加合适的标点，后面需进入 process_detect_vad_and_return_lines 函数加标点
  message = process_vad_queue_and_send_asr_result(c);
  if (!message.empty()) {
    SHERPA_ONNX_LOGE("vad 识别: %s", message.data());
    send_text_result(decoder, c, message);
  }
}
#endif

std::tuple<float, float, float> get_line_timestamp(OfflineRecognitionResult& asr_result) {
  float timestamp = 0.0f;
  float previous_timestamp = 0.0f;
  float next_timestamp = 0.0f;
  if (asr_result.tokens.size() > 2) {
    //SHERPA_ONNX_LOGE("asr_result  str: %s", asr_result.AsJsonString().data());
    for (int i = static_cast<int>(asr_result.tokens.size()) - 2; i > 0; --i) {
      if ("。" == asr_result.tokens[i]
              || "！" == asr_result.tokens[i]
              || "？" == asr_result.tokens[i]
              || "." == asr_result.tokens[i]
              || "!" == asr_result.tokens[i]
              || "?" == asr_result.tokens[i]) {

        if ("." == asr_result.tokens[i]) {
          //if ((i>1) 
          //        && is_utf8_digit(asr_result.tokens[i-1]) 
          //        && is_utf8_digit(asr_result.tokens[i+1]))
          if (!is_utf8_starts_with_space(asr_result.tokens[i+1]))
            continue;
        }
        
        timestamp = asr_result.timestamps[i];
        next_timestamp = asr_result.timestamps[i+1];
        previous_timestamp = asr_result.timestamps[i-1];
        SHERPA_ONNX_LOG(INFO) << "tokens[i]: " << asr_result.tokens[i].data() << ", " <<  timestamp << "\n";
        break;
      }
    }
  } 

  return std::make_tuple(previous_timestamp, timestamp, next_timestamp);
}

int count_common_punct(OfflineRecognitionResult& asr_result) {
  int count = 0;
  if (asr_result.tokens.size() > 2) {
    //SHERPA_ONNX_LOGE("asr_result  str: %s", asr_result.AsJsonString().data());
    for (int i = static_cast<int>(asr_result.tokens.size()) - 2; i > 0; --i) {
      if ("。" == asr_result.tokens[i]
              || "！" == asr_result.tokens[i]
              || "？" == asr_result.tokens[i]
              || "." == asr_result.tokens[i]
              || "!" == asr_result.tokens[i]
              || "?" == asr_result.tokens[i]) {

        if ("." == asr_result.tokens[i]) {
          //if ((i>1) 
          //        && is_utf8_digit(asr_result.tokens[i-1]) 
          //        && is_utf8_digit(asr_result.tokens[i+1]))
          if (!is_utf8_starts_with_space(asr_result.tokens[i+1]))
            continue;
        }
        
        count++;
      }
    }
  } 

  return count;
}

std::vector<std::vector<float>> 
get_punct_segments(OfflineRecognitionResult& asr_result, std::vector<float> data) {
  std::vector<std::vector<float>> segments = {};
  float last_timestamp = 0.0f;
  if (data.empty()) return segments;

  for (int i = 0; i < static_cast<int>(asr_result.tokens.size()); ++i) {
    if ("。" == asr_result.tokens[i]
            || "！" == asr_result.tokens[i]
            || "？" == asr_result.tokens[i]
            //|| "，" == asr_result.tokens[i]
            //|| "," == asr_result.tokens[i]
            || "." == asr_result.tokens[i]
            || "!" == asr_result.tokens[i]
            || "?" == asr_result.tokens[i]) {
      if ("." == asr_result.tokens[i]) {
        if ((i+1) < static_cast<int>(asr_result.tokens.size()))
          if (!is_utf8_starts_with_space(asr_result.tokens[i+1]))
            continue;
      }

      int j = i+1;
      std::vector<float> segment;
      if (!(j < static_cast<int>(asr_result.tokens.size()))) {
        segment.insert(segment.end(), data.begin() + last_timestamp * 16000, 
                              data.end());
      } else {
        segment.insert(segment.end(), data.begin() + last_timestamp * 16000, 
                              data.begin() + asr_result.timestamps[j] * 16000);
      }
      last_timestamp = asr_result.timestamps[i];

      segments.push_back(segment);
    }
  }

  return segments;
}

void offline_accept_data(Connection* c, 
                std::vector<float>& pcm_data_process, 
                std::vector<float>* voice_segments = nullptr) {
  int process_length = 0;

  for (; (process_length + c->vad_window_size) < pcm_data_process.size(); process_length += c->vad_window_size) {
    c->vad->AcceptWaveform(pcm_data_process.data() + process_length, c->vad_window_size);
    c->silent_data_length += c->vad_window_size;

    if (!c->started && c->vad->IsSpeechDetected()) {
      c->started = true;
      c->started_time = std::chrono::steady_clock::now();
    }
    if (c->vad->IsSpeechDetected()) {
      c->silent_data_length = 0;
      if (voice_segments) {
        voice_segments->insert(voice_segments->end(), 
            pcm_data_process.data() + process_length, 
            pcm_data_process.data() + process_length + c->vad_window_size);
      }
    }
  }
  
  int left_data_length = pcm_data_process.size() - process_length; 
  if (left_data_length > 0) {
    if (c->eof) {
      c->vad->AcceptWaveform(pcm_data_process.data() + process_length, left_data_length);
    } else {
      c->pcm_data_left.insert(c->pcm_data_left.end(), 
                    pcm_data_process.begin() + process_length, 
                    pcm_data_process.end());
    }
  }
}

void offline_result_output(void* decoder, 
                    Connection* c, 
                    OnlineWebsocketDecoderConfig& config,
                    OfflineRecognitionResult& eof_result, 
                    std::string speaker_id, 
                    bool is_final) {
  if ((is_final && !eof_result.text.empty()) || c->eof) {
    auto online_result = online_result_from_offline(eof_result, is_final, c->eof);
    std::string asr_text = format_newtranx_result_string(online_result, config, speaker_id, eof_result.lang);
    //SHERPA_ONNX_LOG(INFO) << asr_text << "\n" ;
    send_text_result(decoder, c, asr_text);

    if (is_final) {
      c->started_time = std::chrono::steady_clock::time_point::max();
      c->started = false; 
      c->switch_threshold = SWITCHLINE_MIN;
      //c->asr_history = eof_result.text;
    }
  }
}

void split_by_punctuation(void* decoder, 
                    Connection* c, 
                    OnlineWebsocketDecoderConfig& config,
                    OfflineRecognitionResult eof_result, 
                    std::vector<float>& data) {
  std::string speaker_id;

  auto segments = get_punct_segments(eof_result, data);
  if (segments.empty()) {
    SHERPA_ONNX_LOG(ERROR) << R"(no punctuation, not supported! use "--speaker-recognize-type=3")" << "\n";
    return;
  }

  auto segments_itr = segments.begin();
  std::vector<float> out_data;
  for (; segments_itr < segments.end(); ++segments_itr) {
    if (segments_itr->size() > c->speaker_threshold) {
      std::string new_speaker_id = speaker_compute_embedding_ptr(c->speaker_manager, c->sample_rate, 
                      segments_itr->data(), segments_itr->size(), c->speaker_similarity_threshold);
      if (!new_speaker_id.empty()) {
        if (c->speaker_id_last.empty()){
          c->speaker_id_last = new_speaker_id;
        } else {
          if (c->speaker_id_last != new_speaker_id) {
            if (!out_data.empty()) {
              speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                      out_data, c->speaker_similarity_threshold);
              OfflineRecognitionResult speaker_result = decode_waveform_to_text(c->sample_rate, out_data);
              offline_result_output(decoder, c, config, speaker_result, speaker_id, true);
              out_data = {};
            }
            c->speaker_id_last = new_speaker_id;
          }
        }
      }
    }

    out_data.insert(out_data.end(), segments_itr->begin(), segments_itr->end());
  }

  if (!out_data.empty()) {
    speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                      out_data, c->speaker_similarity_threshold);
    OfflineRecognitionResult speaker_result = decode_waveform_to_text(c->sample_rate, out_data);
    offline_result_output(decoder, c, config, speaker_result, speaker_id, true);
    out_data = {};
  }
}

void split_by_speaker_diarization(void* decoder, 
                      Connection* c,
                      OnlineWebsocketDecoderConfig& config, 
                      std::vector<float>& data) {
  std::string speaker_id;

  if (data.empty()) return;

  auto speakers = speaker_diarization(c, data);
  
  if (speakers.size() < 2) {
    speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                    data, c->speaker_similarity_threshold);
    OfflineRecognitionResult speaker_result = decode_waveform_to_text(c->sample_rate, data);
    offline_result_output(decoder, c, config, speaker_result, speaker_id, true);
  } else {
    int begin = 0;
    std::vector<float> out_data;
    bool out_flag = false;
    for (auto itr = speakers.begin(); itr != speakers.end(); ++itr) {
      SHERPA_ONNX_LOG(INFO) << itr->ToString() << " : " << itr->Duration() 
                                       <<" : " << data.size() << "\n";

      if (std::next(itr) != speakers.end()) {
        auto insert_end = std::next(itr)->Start() * c->sample_rate;

        if (insert_end > data.size()) insert_end = data.size();
        out_data.insert(out_data.end(), data.begin() + begin,  data.begin() + insert_end);
        begin = insert_end;

        if (itr->Speaker() != std::next(itr)->Speaker()) {
          if (out_data.size() > c->speaker_threshold)
            out_flag = true;
        } 
        
      } else {
        out_data.insert(out_data.end(), data.begin() + begin,  data.end());
        out_flag = true;
      }

      if (out_flag) {
        speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                        out_data, c->speaker_similarity_threshold);
        OfflineRecognitionResult speaker_result = decode_waveform_to_text(c->sample_rate, out_data);
        offline_result_output(decoder, c, config, speaker_result, speaker_id, true);
        out_data = {};
        out_flag = false;
      }
    }
  }
}

void offline_recognize_stream(void* decoder, 
                      Connection* c, 
                      OnlineWebsocketDecoderConfig& config,
                      std::vector<float>* voice_segments = nullptr) {
  auto current_time = std::chrono::steady_clock::now();
  const float elapsed_milliseconds
    = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - c->started_time).count();
  
  if (c->started && (elapsed_milliseconds > 200)) {
    c->voice_segments = c->vad_segments;
    if (!voice_segments) {
      auto segment = c->vad->CurrentSpeechSegment();
      c->voice_segments.insert(c->voice_segments.end(), segment.samples.begin(), segment.samples.end());
    } else {
      c->voice_segments.insert(c->voice_segments.end(), voice_segments->begin(), voice_segments->end());
    }
    
    if (c->voice_segments.size() > MIN_ASR_DATA_LENGTH) {
      OfflineRecognitionResult stream_result = decode_waveform_to_text(c->sample_rate, c->voice_segments);
      stream_result.text = utf8_drop_back(stream_result.text, 1);
      auto online_result = online_result_from_offline(stream_result, false, false);
      std::string stream_text = format_newtranx_result_string(online_result, config, "", stream_result.lang);
      send_text_result(decoder, c, stream_text);
      c->started_time = std::chrono::steady_clock::now();
    }
  }
}

void offline_recognize(void* decoder, 
              Connection* c, 
              OnlineWebsocketDecoderConfig& config,
              bool speaker_changed) {
  bool is_final = false; 
  OfflineRecognitionResult eof_result;
  std::string speaker_id;

  if ((speaker_changed) ||
        (c->silent_data_length > c->silence_threshold) || c->eof) {
    if (c->vad_segments.size() > 0) {
      eof_result = decode_waveform_to_text(c->sample_rate, c->vad_segments);
      if (1 == c->speaker_diarization_enable) {
        speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                              c->vad_segments, c->speaker_similarity_threshold);
      }
      c->vad_segments = {};
      is_final = true;
    }
  } else if ((c->vad_segments.size() > c->switch_threshold) && (!c->eof)) {
    OfflineRecognitionResult asr_result = decode_waveform_to_text(c->sample_rate, c->vad_segments);

    auto[previous_timestamp, timestamp, next_timestamp] = get_line_timestamp(asr_result);
    if (timestamp > 0.01f) {
      std::vector<float> front(c->vad_segments.begin(), c->vad_segments.begin() + next_timestamp*16000);
      eof_result = decode_waveform_to_text(c->sample_rate, front);
      if (eof_result.tokens.size() > 5) {
        if (1 == c->speaker_diarization_enable) {
          speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                            front, c->speaker_similarity_threshold);
        }
        c->vad_segments.erase(c->vad_segments.begin(), c->vad_segments.begin() + timestamp*16000);
        is_final = true;
      } else {
        c->switch_threshold += 16000;
      }
    } else {
      c->switch_threshold += 8000;
    }

    if (c->vad_segments.size() > SWITCHLINE_MAX) {
      if (1 == c->speaker_diarization_enable) {
        speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                                c->vad_segments, c->speaker_similarity_threshold);
      }
      
      eof_result = asr_result;
      c->vad_segments = {};
      is_final = true;
    }
  }

  offline_result_output(decoder, c, config, eof_result, speaker_id, is_final);
}

void offline_recognize_speaker(void* decoder, 
                        Connection* c,
                        OnlineWebsocketDecoderConfig& config) {
  OfflineRecognitionResult asr_result;
  OfflineRecognitionResult eof_result;
  std::string speaker_id;
  
  if ((c->vad_segments.size() > c->switch_threshold) && (!c->eof)) {
    asr_result = decode_waveform_to_text(c->sample_rate, c->vad_segments);

    auto[previous_timestamp, timestamp, next_timestamp] = get_line_timestamp(asr_result);
    if (timestamp > 0.01f) {
      std::vector<float> front(c->vad_segments.begin(), c->vad_segments.begin() + next_timestamp*16000);
      eof_result = decode_waveform_to_text(c->sample_rate, front);
      if (eof_result.tokens.size() > 5) {
        //split_by_punctuation(decoder, c, eof_result, front);
        split_by_speaker_diarization(decoder, c, config, front);
        c->vad_segments.erase(c->vad_segments.begin(), c->vad_segments.begin() + timestamp*16000);
      } else {
        c->switch_threshold += 16000;
      }
    } else {
      c->switch_threshold += 8000;
    }
  }

  if ((c->vad_segments.size() > SWITCHLINE_MAX) 
                || (c->silent_data_length > c->silence_threshold) || c->eof) {
    if (c->vad_segments.size() > 0) {
      //split_by_punctuation(decoder, c, asr_result, c->vad_segments);
      split_by_speaker_diarization(decoder, c, config, c->vad_segments);
      c->vad_segments = {};
    }
  }
}

void offline_recognize_sliding_vad(void* decoder, 
                        Connection* c,
                        OnlineWebsocketDecoderConfig& config) {
  int process_length = 0;
  int steps = 1600; //

  if (c->speaker_segments.size() > c->speaker_threshold) {
    for (;process_length + c->speaker_threshold < c->speaker_segments.size(); process_length += steps) {
      bool speaker_changed = false;
      std::vector<float> data;
      data.insert(data.end(), 
              c->speaker_segments.begin() + process_length, 
              c->speaker_segments.begin() + process_length + c->speaker_threshold);
      
      std::string vad_speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                            data, c->speaker_similarity_threshold);

      if (c->speaker_id_last.empty()) {
        c->speaker_id_last = vad_speaker_id;
      } else {
        if (c->speaker_id_last != vad_speaker_id) {
          if (c->vad_segments.size() > c->speaker_threshold) {
            speaker_changed = true;
            c->speaker_id_last = vad_speaker_id;
            offline_recognize(decoder, c, config, speaker_changed);
          }
        } 
      }

      c->vad_segments.insert(c->vad_segments.end(), 
                      c->speaker_segments.begin(),  
                      c->speaker_segments.begin() + process_length);

      c->speaker_segments.erase(c->speaker_segments.begin(), c->speaker_segments.begin() + process_length);
      process_length = 0;
    }  
  }
  if ((c->silent_data_length > c->silence_threshold) || c->eof) {
    if (c->speaker_segments.size() > 0) {
      c->vad_segments.insert(c->vad_segments.end(), 
                          c->speaker_segments.begin(),  
                          c->speaker_segments.end());
      c->speaker_segments = {};
    }
  }
  offline_recognize(decoder, c, config, false);
}

void offline_recognize_vad(void* decoder, 
                  Connection* c, 
                  OnlineWebsocketDecoderConfig& config,
                  std::vector<std::vector<float>> samples_segments) {

  for (auto itr = samples_segments.begin(); itr < samples_segments.end(); ++itr) {
    bool speaker_changed = false;

    //if ((itr->size() % 16) != 0)
    //  SHERPA_ONNX_LOGE("may be audio vad frame size is error");

    if (itr->size() > c->speaker_threshold) {
      std::string vad_speaker_id = speaker_compute_embedding_ptr(c->speaker_manager, c->sample_rate, 
                            itr->data(), itr->size(), c->speaker_similarity_threshold);
      if (c->speaker_id_last.empty()) {
        c->speaker_id_last = vad_speaker_id;
      } else {
        if (c->speaker_id_last != vad_speaker_id) {
          speaker_changed = true;
          c->speaker_id_last = vad_speaker_id;
          offline_recognize(decoder, c, config, speaker_changed);
        } 
      }
    }
    
    c->vad_segments.insert(c->vad_segments.end(), 
                    itr->begin(),  
                    itr->end());
  }
  
  offline_recognize(decoder, c, config, false);
}

void get_data_from_vad(Connection* c, 
              std::vector<std::vector<float>>* segments = nullptr) {
  
  std::vector<float> data2save;
  //
  if ((c->silent_data_length > c->silence_threshold) || c->eof)
    if (c->vad->Empty()) c->vad->Flush();

  while(!c->vad->Empty()) {
    const auto &segment = c->vad->Front();
    if (segments) {
      segments->push_back(segment.samples);
    } else {
      c->vad_segments.insert(c->vad_segments.end(),
                        segment.samples.begin(),
                        segment.samples.end());
    }
#ifndef __ANDROID__
    if (!g_decoder_config->save_data_path.empty()) {
      data2save.insert(data2save.end(), segment.samples.begin(),
                        segment.samples.end());
    }
#endif
    c->vad->Pop();
  }
#ifndef __ANDROID__
  pcmdata_2_file(c, data2save, g_decoder_config->save_data_path);
#endif
}

void vad_asr(void* decoder, 
                Connection* c, 
                OnlineWebsocketDecoderConfig& config,
                std::vector<float>& pcm_data_process) {
  offline_accept_data(c, pcm_data_process);

  get_data_from_vad(c);
  offline_recognize(decoder, c, config, false);

  offline_recognize_stream(decoder, c, config);
}

void vad_asr_speaker(void* decoder, 
                Connection* c, 
                OnlineWebsocketDecoderConfig& config,
                std::vector<float>& pcm_data_process) {
  offline_accept_data(c, pcm_data_process);

  //
  get_data_from_vad(c);
  offline_recognize_speaker(decoder, c, config);
  
  offline_recognize_stream(decoder, c, config);
}

void vad_asr_speaker_0(void* decoder, 
                Connection* c, 
                OnlineWebsocketDecoderConfig& config,
                std::vector<float>& pcm_data_process) {
  offline_accept_data(c, pcm_data_process);

  get_data_from_vad(c);
  if ((c->vad_segments.size() > c->speaker_threshold) || 
        (c->silent_data_length > c->silence_threshold) || c->eof) {
    offline_recognize(decoder, c, config, true);
  }

  offline_recognize_stream(decoder, c, config);
}

void vad_asr_speaker_1(void* decoder, 
                Connection* c, 
                OnlineWebsocketDecoderConfig& config,
                std::vector<float>& pcm_data_process) {
  //
  offline_accept_data(c, pcm_data_process);

  //
  get_data_from_vad(c);
  //
  offline_recognize_sliding_vad(decoder, c, config);
  //
  offline_recognize_stream(decoder, c, config);
}

void vad_asr_speaker_2(void* decoder, 
                Connection* c, 
                OnlineWebsocketDecoderConfig& config,
                std::vector<float>& pcm_data_process) {
  offline_accept_data(c, pcm_data_process);
  //
  std::vector<std::vector<float>> samples_segments;
  get_data_from_vad(c, &samples_segments);
  //
  offline_recognize_vad(decoder, c, config, samples_segments);

  offline_recognize_stream(decoder, c, config);
}

void vad_asr_speaker_3(void* decoder, 
                Connection* c, 
                OnlineWebsocketDecoderConfig& config,
                std::vector<float>& pcm_data_process) {
  offline_accept_data(c, pcm_data_process);

  get_data_from_vad(c);

  if ((c->vad_segments.size() > c->speaker_threshold) || 
        (c->silent_data_length > c->silence_threshold) || c->eof) {
    split_by_speaker_diarization(decoder, c, config, c->vad_segments);
    c->vad_segments = {};
  }

  offline_recognize_stream(decoder, c, config);
}
#endif

void OnlineWebsocketDecoder::SocketSend (
    Connection* c,
    OnlineRecognizerResult online_result) {

  if (!online_result.text.empty()) {
    SocketSend(c, online_result.AsJsonString());
  }
}

void OnlineWebsocketDecoder::SocketSend (
    Connection* c,
    std::string message) {
  
  if (!message.empty()) {
    if (server_) {
      asio::post(server_->GetConnectionContext(),
        [this, hdl = c->hdl, str = message]() {
          server_->Send(hdl, str);
      });
    }
  }
}

void OnlineWebsocketDecoderConfig::Register(ParseOptions *po) {
#ifdef NEWTRANX_OFFLINE_MODEL
  offline_recognizer_config.Register(po);
  //po->Register("silero-vad-model", &silero_vad_model, "offline silero vad ");
  //po->Register("models-path", &models_path, "models path"); 
  po->Register("line-time-threshold", &line_time_threshold, "line switch time threshold"); 
  po->Register("line-chars-threshold", &line_chars_threshold, "line switch chars threshold"); 
  
#else
  recognizer_config.Register(po);
#endif
  vad_config.Register(po);
  offline_punctuation_model_config.Register(po);
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  speaker_extractor_config.Register(po);
  speaker_diarization_config.Register(po);
#endif
  po->Register("switch-threshold", &switch_threshold, "recognize time threshold");
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  po->Register("speaker-threshold", &speaker_threshold, "recognize speaker id threshold");
#endif
  po->Register("silence-threshold", &silence_threshold, "silence threshold");
  
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  po->Register("speaker-similarity-threshold", &speaker_similarity_threshold, "speaker similarity threshold");
  po->Register("speaker-model", &speaker_model, "speaker model");
#endif
  po->Register("language-type", &language_type, "language type");
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  po->Register("speaker-recognize-type", &speaker_recognize_type, "language type");
#endif
  po->Register("asr-streaming-type", &asr_streaming_type, "asr streaming type");
  po->Register("punctuation-server", &punctuation_server, "punctuation server");
  po->Register("save-data-path", &save_data_path, "path save data for debug");
  
  po->Register("loop-interval-ms", &loop_interval_ms,
               "It determines how often the decoder loop runs. ");

  po->Register("max-batch-size", &max_batch_size,
               "Max batch size for recognition.");

  po->Register("end-tail-padding", &end_tail_padding,
               "It determines the length of tail_padding at the end of audio.");
}

void OnlineWebsocketDecoderConfig::Validate() const {
#ifdef NEWTRANX_OFFLINE_MODEL
#else
  recognizer_config.Validate();
#endif
  SHERPA_ONNX_CHECK_GT(loop_interval_ms, 0);
  SHERPA_ONNX_CHECK_GT(max_batch_size, 0);
  SHERPA_ONNX_CHECK_GT(end_tail_padding, 0);
}

void OnlineWebsocketServerConfig::Register(sherpa_onnx::ParseOptions *po) {
  decoder_config.Register(po);

  po->Register("log-file", &log_file,
               "Path to the log file. Logs are "
               "appended to this file");

  po->Register("ping-interval-ms", &ping_interval_ms,
               "How often to ping WebSocket clients.");

  po->Register("pong-timeout-ms", &pong_timeout_ms,
               "How long to wait for a pong before closing the connection.");
}

void OnlineWebsocketServerConfig::Validate() const {
  decoder_config.Validate();
  SHERPA_ONNX_CHECK_GT(ping_interval_ms, 0);
  SHERPA_ONNX_CHECK_GT(pong_timeout_ms, 0);
}

OnlineWebsocketDecoder::OnlineWebsocketDecoder(OnlineWebsocketServer *server)
    : server_(server),
      config_(server->GetConfig().decoder_config),
      timer_(server->GetWorkContext()) {
  g_decoder_config = &config_;
  create_offline_punct(config_);
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  create_speaker_extractor(config_);
#endif
#ifdef NEWTRANX_OFFLINE_MODEL
  create_offline_recognizer(config_.offline_recognizer_config);
#else
  //recognizer_ = std::make_unique<OnlineRecognizer>(config_.recognizer_config);
  recognizer_ = create_online_recognizer(config_.recognizer_config);
#endif
}

OnlineWebsocketDecoder::~OnlineWebsocketDecoder() {
/*
#ifdef NEWTRANX_OFFLINE_MODEL
  if (g_offline_recognizer) {
    delete g_offline_recognizer;
    g_offline_recognizer = nullptr;
  }
#endif
  if (g_offline_punc) {
    delete g_offline_punc;
    g_offline_punc = nullptr;
  }
  if (g_speaker_extractor) {
    delete g_speaker_extractor;
    g_speaker_extractor = nullptr;
  }
*/
}

std::shared_ptr<Connection> OnlineWebsocketDecoder::GetOrCreateConnection(
    connection_hdl hdl) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = connections_.find(hdl);
  if (it != connections_.end()) {
    return it->second;
  } else {
    // create a new connection
#ifdef NEWTRANX_OFFLINE_MODEL
    auto c = std::make_shared<Connection>();
    c->hdl = hdl;
    c->last_active = std::chrono::steady_clock::now();
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
    c->speaker_threshold = config_.speaker_threshold;
#endif
    c->switch_threshold = config_.switch_threshold;
    //c->vad_window_size = config_.vad_config.silero_vad.window_size;
    c->vad = create_vad(config_, c.get());
#else
    std::shared_ptr<OnlineStream> s = recognizer_->CreateStream();
    auto c = std::make_shared<Connection>(hdl, s);
#endif
    c->silence_threshold = config_.silence_threshold;
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
    c->speaker_similarity_threshold = config_.speaker_similarity_threshold;
    c->speaker_manager = create_speaker_manager();
    c->speaker_diarization = create_speaker_diarization(config_);
#endif

    connections_.insert({hdl, c});
    return c;  
  }
}

void OnlineWebsocketDecoder::RemoveConnection(connection_hdl hdl) {
  std::lock_guard<std::mutex> lock(mutex_);
  connections_.erase(hdl);
  active_.erase(hdl);
}

void OnlineWebsocketDecoder::AcceptWaveform(Connection* c) {
  std::lock_guard<std::mutex> lock(c->mutex);
#ifdef NEWTRANX_OFFLINE_MODEL
  float sample_rate = config_.offline_recognizer_config.feat_config.sampling_rate;
#else
  float sample_rate = config_.recognizer_config.feat_config.sampling_rate;
#endif
  while (!c->samples.empty()) {
    const auto &s = c->samples.front();

      /*
    static int wav_index = 0;
    std::string wav_file("./vad_before/w-");
    wav_file.append(std::to_string(wav_index++)).append(".wav");
    WriteWave(wav_file, sample_rate, s.data(), s.size() * sizeof(float));
  // */

//    c->sample_rate = sample_rate;    
#ifdef NEWTRANX_OFFLINE_MODEL
    std::unique_lock<std::mutex> pcm_lock(c->pcm_data_mutex);
    c->pcm_data_receive.insert(c->pcm_data_receive.end(), s.begin(), s.end());
    pcm_lock.unlock();
#else
    c->s->AcceptWaveform(sample_rate, s.data(), s.size());
#endif
    c->samples.pop_front();
  }
}

void OnlineWebsocketDecoder::InputFinished(Connection* c) {
  std::lock_guard<std::mutex> lock(c->mutex);
#ifdef NEWTRANX_OFFLINE_MODEL
  float sample_rate = config_.offline_recognizer_config.feat_config.sampling_rate;
#else
  float sample_rate = config_.recognizer_config.feat_config.sampling_rate;
#endif

  while (!c->samples.empty()) {
    const auto &s = c->samples.front();

#ifdef NEWTRANX_OFFLINE_MODEL
    std::unique_lock<std::mutex> pcm_lock(c->pcm_data_mutex);
    c->pcm_data_receive.insert(c->pcm_data_receive.end(), s.begin(), s.end());
    pcm_lock.unlock();
#else
    c->s->AcceptWaveform(sample_rate, s.data(), s.size());
#endif
    c->samples.pop_front();
  }

  std::vector<float> tail_padding(
      static_cast<int64_t>(config_.end_tail_padding * sample_rate));

  
#ifdef NEWTRANX_OFFLINE_MODEL
  std::unique_lock<std::mutex> pcm_lock(c->pcm_data_mutex);
  c->pcm_data_receive.insert(c->pcm_data_receive.end(), tail_padding.begin(), tail_padding.end());
  pcm_lock.unlock();
#else
  c->s->AcceptWaveform(sample_rate, tail_padding.data(), tail_padding.size());
  c->s->InputFinished();
#endif
  
  c->eof = true;
}

void OnlineWebsocketDecoder::Warmup() const {
#ifdef NEWTRANX_OFFLINE_MODEL
#else
  recognizer_->WarmpUpRecognizer(config_.recognizer_config.model_config.warm_up,
                                 config_.max_batch_size);
#endif
}

void OnlineWebsocketDecoder::Run() {
  timer_.expires_after(std::chrono::milliseconds(config_.loop_interval_ms));

  timer_.async_wait(
      [this](const asio::error_code &ec) { ProcessConnections(ec); });
}

void OnlineWebsocketDecoder::ProcessConnections(const asio::error_code &ec) {
  if (ec) {
    SHERPA_ONNX_LOG(FATAL) << "The decoder loop is aborted!";
  }

  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<connection_hdl> to_remove;
  for (auto &p : connections_) {
    auto hdl = p.first;
    auto c = p.second;
    
    // The order of `if` below matters!
    if (!server_->Contains(hdl)) {
      // If the connection is disconnected, we stop processing it
      to_remove.push_back(hdl);
      continue;
    }

    if (active_.count(hdl)) {
      // Another thread is decoding this stream, so skip it
      continue;
    }
#ifdef NEWTRANX_OFFLINE_MODEL
    if (c->pcm_data_receive.empty() && c->eof) {
      // We won't receive samples from the client, so send a Done! to client
      c->close_scheduled = true;
      asio::post(server_->GetConnectionContext(),
                 [this, hdl = c->hdl]() {
                   server_->Send(hdl, "Done!");
                   server_->Close(hdl, websocketpp::close::status::normal,
                                  "Done");
                 });

      to_remove.push_back(hdl);
      continue;
    }
#else
    if (!recognizer_->IsReady(c->s.get()) && !c->eof) {
      // this stream has not enough frames to decode, so skip it
      continue;
    }

    if (!recognizer_->IsReady(c->s.get()) && c->eof) {
      // We won't receive samples from the client, so send a Done! to client
      if (!c->close_scheduled) {
        c->close_scheduled = true;
        asio::post(server_->GetConnectionContext(),
                   [this, hdl = c->hdl]() {
                     server_->Send(hdl, "Done!");
                     server_->Close(hdl, websocketpp::close::status::normal,
                                    "Done");
                   });
      }

      to_remove.push_back(hdl);
      continue;
    }
#endif
    // TODO(fangun): If the connection is timed out, we need to also
    // add it to `to_remove`

    // this stream has enough frames and is currently not processed by any
    // threads, so put it into the ready queue
    ready_connections_.push_back(c);

    // In `Decode()`, it will remove hdl from `active_`
    active_.insert(c->hdl);
  }

  for (auto hdl : to_remove) {
    connections_.erase(hdl);
  }

  if (!ready_connections_.empty()) {
    asio::post(server_->GetWorkContext(), [this]() { Decode(); });
  }

  // Schedule another call
  timer_.expires_after(std::chrono::milliseconds(config_.loop_interval_ms));

  timer_.async_wait(
      [this](const asio::error_code &ec) { ProcessConnections(ec); });
}

void OnlineWebsocketDecoder::Decode() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (ready_connections_.empty()) {
    // There are no connections that are ready for decoding,
    // so we return directly
    return;
  }

  std::vector<std::shared_ptr<Connection>> c_vec;
  std::vector<OnlineStream *> s_vec;
  while (!ready_connections_.empty() &&
         static_cast<int32_t>(s_vec.size()) < config_.max_batch_size) {
    auto c = ready_connections_.front();
    ready_connections_.pop_front();

    c_vec.push_back(c);
#ifdef NEWTRANX_OFFLINE_MODEL
#else
    s_vec.push_back(c->s.get());
#endif
  }

  if (!ready_connections_.empty()) {
    // there are too many ready connections but this thread can only handle
    // max_batch_size connections at a time, so we schedule another call
    // to Decode() and let other threads to process the ready connections
    asio::post(server_->GetWorkContext(), [this]() { Decode(); });
  }

#ifdef NEWTRANX_OFFLINE_MODEL
  std::for_each(c_vec.begin(), c_vec.end(), [this](std::shared_ptr<Connection> c) {
    std::vector<float> pcm_data_process = {};
    int vad_window_size = c->vad_window_size;
    
    std::unique_lock<std::mutex> pcm_lock(c->pcm_data_mutex);
    if ((c->pcm_data_receive.size() > 0) || (1 == c->eof)) {
      //SHERPA_ONNX_LOGE("c->pcm_data_receive.size(): %ld", c->pcm_data_receive.size());
      pcm_data_process.swap(c->pcm_data_left);
      pcm_data_process.insert(pcm_data_process.end(), 
            c->pcm_data_receive.begin(), 
            c->pcm_data_receive.end());
      c->pcm_data_receive = {};
    } else {
        active_.erase(c->hdl);
        return;
    }
    pcm_lock.unlock();

    if ((pcm_data_process.size() > vad_window_size) || (1 == c->eof)) {
#if COPY_PYTHON
      asr_decode_and_process((void*)this, c.get(), pcm_data_process);
#else
      if (g_speaker_extractor) {
        if ((config_.speaker_recognize_type == 3) 
            || (!config_.offline_recognizer_config.model_config.sense_voice.use_itn)) {
          vad_asr_speaker_3((void*)this, c.get(), this->config_, pcm_data_process);
          SHERPA_ONNX_LOG(INFO) << "asr mode: vad_asr_speaker_3" << "\n";
        } else if (config_.speaker_recognize_type == 2) {
          vad_asr_speaker_2((void*)this, c.get(), this->config_, pcm_data_process);
          SHERPA_ONNX_LOG(INFO) << "asr mode: vad_asr_speaker_2" << "\n";
        } else if (config_.speaker_recognize_type == 1) {
          vad_asr_speaker_1((void*)this, c.get(), this->config_, pcm_data_process);
          SHERPA_ONNX_LOG(INFO) << "asr mode: vad_asr_speaker_1" << "\n";
        } else if (config_.speaker_recognize_type == 0) {
          vad_asr_speaker_0((void*)this, c.get(), this->config_, pcm_data_process);
          SHERPA_ONNX_LOG(INFO) << "asr mode: vad_asr_speaker_0" << "\n";
        } else {
          vad_asr_speaker((void*)this, c.get(), this->config_, pcm_data_process);
          SHERPA_ONNX_LOG(INFO) << "asr mode: vad_asr_speaker" << "\n";
        }
      } else
        vad_asr((void*)this, c.get(), this->config_, pcm_data_process);
#endif
    } else {
      c->pcm_data_left.insert(c->pcm_data_left.end(), 
                    pcm_data_process.begin(), 
                    pcm_data_process.end());
    }
    active_.erase(c->hdl);
  });
#else
  
  lock.unlock();
  recognizer_->DecodeStreams(s_vec.data(), s_vec.size());
  lock.lock();

  for (auto c : c_vec) {
    auto result = recognizer_->GetResult(c->s.get());

    if (recognizer_->IsEndpoint(c->s.get())) {
      result.is_final = true;
      recognizer_->Reset(c->s.get());
    }

    if (!recognizer_->IsReady(c->s.get()) && c->eof) {
      result.is_final = true;
      result.is_eof = true;
      c->close_scheduled = true;
    }

    //asio::post(server_->GetConnectionContext(),
    //           [this, hdl = c->hdl, str = result.AsJsonString()]() {
    //              server_->Send(hdl, str);
    //           });
    
    asio::post(server_->GetConnectionContext(),
                [this, hdl = c->hdl, result]() {
                  OnlineRecognizerResult res_tmp = result;
                  
                  std::string str = format_newtranx_result_string(res_tmp, this->config_, "");
                  server_->Send(hdl, str);
                  if (result.is_eof) {
                    server_->Send(hdl, "Done!");
                    server_->Close(hdl, websocketpp::close::status::normal,
                                   "Done");
                  }
                });
    
    active_.erase(c->hdl);
  }
#endif
}

OnlineWebsocketServer::OnlineWebsocketServer(
    asio::io_context &io_conn, asio::io_context &io_work,
    const OnlineWebsocketServerConfig &config)
    : config_(config),
      io_conn_(io_conn),
      io_work_(io_work),
      ping_timer_(io_conn_),
      decoder_(this) {
  SetupLog();

  server_.init_asio(&io_conn_);

  server_.set_open_handler([this](connection_hdl hdl) { OnOpen(hdl); });

  server_.set_close_handler([this](connection_hdl hdl) { OnClose(hdl); });

  server_.set_pong_timeout(config_.pong_timeout_ms);
  server_.set_pong_timeout_handler(
      [this](connection_hdl hdl, const std::string &) { OnPongTimeout(hdl); });

  server_.set_message_handler(
      [this](connection_hdl hdl, server::message_ptr msg) {
        OnMessage_(hdl, msg);
      });

#ifndef __ANDROID__
#ifdef SHERPA_ONNX_ENABLE_CHECK
  encap::install_coredump_handlers();
#endif
#endif

  /*
  server_.set_open_handshake_timeout(0);
  server_.set_close_handshake_timeout(0);
  server_.set_pong_timeout(0);
  //server_.set_max_message_size(0);
  //server_.set_max_http_body_size(0);
  server_.set_fail_handler([this](auto hdl) {
    auto con = server_.get_con_from_hdl(hdl);
    SHERPA_ONNX_LOGE("protocol fail: %s", con->get_ec().message().data());
  });
  server_.set_pong_handler([this](auto, auto){ });
  // */
}

void OnlineWebsocketServer::Run(uint16_t port) {
  server_.set_reuse_addr(true);
  server_.listen(asio::ip::tcp::v4(), port);
  server_.start_accept();
  SchedulePing();
#ifdef NEWTRANX_OFFLINE_MODEL
#else  
  auto recognizer_config = config_.decoder_config.recognizer_config;
  int32_t warm_up = recognizer_config.model_config.warm_up;
  const std::string &model_type = recognizer_config.model_config.model_type;
  if (0 < warm_up && warm_up < 100) {
    if (model_type == "zipformer2") {
      decoder_.Warmup();
      SHERPA_ONNX_LOGE("Warm up completed : %d times.", warm_up);
    } else {
      SHERPA_ONNX_LOGE("Only Zipformer2 has warmup support for now.");
      SHERPA_ONNX_LOGE("Given: %s", model_type.c_str());
      exit(0);
    }
  } else if (warm_up == 0) {
    SHERPA_ONNX_LOGE("Starting without warmup!");
  } else {
    SHERPA_ONNX_LOGE("Invalid Warm up Value!. Expected 0 < warm_up < 100");
    exit(0);
  }
#endif
  decoder_.Run();
}

void OnlineWebsocketServer::BeginRestart() {
  std::vector<connection_hdl> connections;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    restarting_ = true;
    connections.assign(connections_.begin(), connections_.end());
  }

  websocketpp::lib::error_code ec;
  server_.stop_listening(ec);
  if (ec) {
    SHERPA_ONNX_LOGE("Failed to stop listening for restart: %s",
                     ec.message().c_str());
  }
  for (const auto &hdl : connections) {
    ec.clear();
    server_.close(hdl, websocketpp::close::status::service_restart,
                  "Service restart", ec);
    if (ec) {
      SHERPA_ONNX_LOGE("Failed to close connection for restart: %s",
                       ec.message().c_str());
    }
  }
}

size_t OnlineWebsocketServer::ActiveConnections() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.size();
}

void OnlineWebsocketServer::ForceCloseConnections() {
  std::vector<connection_hdl> connections;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connections.assign(connections_.begin(), connections_.end());
  }
  for (const auto &hdl : connections) {
    auto con = server_.get_con_from_hdl(hdl);
    if (con) {
      websocketpp::lib::error_code ec;
      con->get_socket().close(ec);
      if (ec) {
        SHERPA_ONNX_LOGE("Failed to force close connection: %s",
                         ec.message().c_str());
      }
    }
  }
}

void OnlineWebsocketServer::SetupLog() {
  server_.clear_access_channels(websocketpp::log::alevel::all);
  server_.set_access_channels(websocketpp::log::alevel::connect);
  server_.set_access_channels(websocketpp::log::alevel::disconnect);
  server_.set_access_channels(websocketpp::log::alevel::app);

  server_.get_alog().set_ostream(&std::cout);
  server_.get_elog().set_ostream(&std::cerr);
}

void OnlineWebsocketServer::SchedulePing() {
  ping_timer_.expires_after(
      std::chrono::milliseconds(config_.ping_interval_ms));
  ping_timer_.async_wait([this](const asio::error_code &ec) {
    if (ec) {
      return;
    }

    std::vector<connection_hdl> connections;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      connections.assign(connections_.begin(), connections_.end());
    }

    for (const auto &hdl : connections) {
      websocketpp::lib::error_code ping_ec;
      server_.ping(hdl, "", ping_ec);
      if (ping_ec) {
        server_.get_alog().write(websocketpp::log::alevel::app,
                                 ping_ec.message());
      }
    }

    SchedulePing();
  });
}

void OnlineWebsocketServer::OnPongTimeout(connection_hdl hdl) {
  websocketpp::lib::error_code ec;
  server_.close(hdl, websocketpp::close::status::going_away,
                "pong timeout", ec);
  if (ec) {
    server_.get_alog().write(websocketpp::log::alevel::app, ec.message());
  }
}

void OnlineWebsocketServer::Send(connection_hdl hdl, const std::string &text) {
  websocketpp::lib::error_code ec;
  if (!Contains(hdl) || text.empty()) {
    return;
  }

  //SHERPA_ONNX_LOG(INFO) << "send result to client: " << text << "\n";

  server_.send(hdl, text, websocketpp::frame::opcode::text, ec);
  if (ec) {
    server_.get_alog().write(websocketpp::log::alevel::app, ec.message());
  }
}

void OnlineWebsocketServer::OnOpen(connection_hdl hdl) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto max_connections = get_max_connection();
  auto con = server_.get_con_from_hdl(hdl);

  if (restarting_) {
    server_.close(hdl, websocketpp::close::status::service_restart,
                  "Service restart");
    return;
  }

  if (connections_.size() >= max_connections) {
    server_.close(hdl, websocketpp::close::status::policy_violation,
              "too many connections");
    server_.get_alog().write(
        websocketpp::log::alevel::app,
        "Connection rejected. Active connections: " +
            std::to_string(connections_.size()) + "/" +
            std::to_string(max_connections));
    return; 
  }

  connections_.insert(hdl);

  const std::string &resource = con->get_resource();
  websocketpp::lib::error_code endpoint_ec;
  auto endpoint = con->get_socket().local_endpoint(endpoint_ec);
  std::string local_endpoint = endpoint_ec
                                   ? con->get_request_header("Host")
                                   : endpoint.address().to_string() + ":" +
                                         std::to_string(endpoint.port());
  std::string meeting_id = GetMeetingId(resource);
  if (meeting_id.empty()) {
    meeting_id = "未提供";
  } else {
    meeting_id = local_endpoint + "-" + meeting_id;
  }
  connection_log_states_[hdl] = {meeting_id, false};

  std::ostringstream os;
  os << "New connection: "
     << con->get_remote_endpoint() << ". "
     << "Active connections: " << connections_.size() << "/"
     << max_connections << ".\n"
     << "Meeting request: ws://" << con->get_request_header("Host")
     << resource << ".\n"
     << "meeting_id=" << meeting_id << ".\n";
  server_.get_alog().write(websocketpp::log::alevel::app, os.str());
}

void OnlineWebsocketServer::OnClose(connection_hdl hdl) {
  auto con = server_.get_con_from_hdl(hdl);
  size_t connection_count;
  std::pair<std::string, bool> state{"未提供", false};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(hdl);
    auto state_iter = connection_log_states_.find(hdl);
    if (state_iter != connection_log_states_.end()) {
      state = state_iter->second;
      connection_log_states_.erase(state_iter);
    }
    connection_count = connections_.size();
  }

  decoder_.RemoveConnection(hdl);

  auto close_code = con->get_remote_close_code();
  if (close_code == websocketpp::close::status::no_status) {
    close_code = con->get_local_close_code();
  }

  std::ostringstream reason;
  std::string level;
  if (state.second) {
    level = "INFO";
    reason << "已收到 Done，识别正常完成 (OK)";
  } else if (con->get_remote_close_code() ==
             websocketpp::close::status::normal) {
    level = "WARNING";
    reason << "客户端在识别完成前主动关闭连接";
  } else {
    level = "ERROR";
    reason << "识别完成前连接异常中断";
  }
  reason << "，关闭码：" << static_cast<int>(close_code) << " ("
         << websocketpp::close::status::get_string(close_code) << ")";
  if (con->get_ec()) {
    reason << "，错误：" << con->get_ec().message();
  }

  std::ostringstream os;
  os << level << " 连接已断开：" << con->get_remote_endpoint() << "。"
     << "meeting_id：" << state.first << "。"
     << "断开原因：" << reason.str() << "。"
     << "当前连接数：" << connection_count << "/" << get_max_connection();
  server_.get_alog().write(websocketpp::log::alevel::app, os.str());
}

bool OnlineWebsocketServer::Contains(connection_hdl hdl) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.count(hdl);
}

void OnlineWebsocketServer::OnMessage_(connection_hdl hdl,
                                      server::message_ptr msg) {
  auto c = decoder_.GetOrCreateConnection(hdl);

  const std::string &payload = msg->get_payload();

  switch (msg->get_opcode()) {
    case websocketpp::frame::opcode::text:
      if (payload == "Done") {
        asio::post(io_work_, [this, c]() { decoder_.InputFinished(c.get()); });
      }
      break;
    case websocketpp::frame::opcode::binary: {
      auto p = reinterpret_cast<const float *>(payload.data());
      int32_t num_samples = payload.size() / sizeof(float);
      std::vector<float> samples(p, p + num_samples);

      {
        std::lock_guard<std::mutex> lock(c->mutex);
        c->samples.push_back(std::move(samples));
      }

      asio::post(io_work_, [this, c]() { decoder_.AcceptWaveform(c.get()); });
      break;
    }
    default:
      break;
  }
}

void OnlineWebsocketServer::OnMessage(connection_hdl hdl,
                                      server::message_ptr msg) {
  auto c = decoder_.GetOrCreateConnection(hdl);

  const std::string &payload = msg->get_payload();

  switch (msg->get_opcode()) {
    case websocketpp::frame::opcode::text:
      if (payload == "Done") {
        asio::post(io_work_, [this, c]() { decoder_.InputFinished(c.get()); });
      }
      break;
    case websocketpp::frame::opcode::binary: {
      auto p = reinterpret_cast<const float *>(payload.data());
      int32_t num_samples = payload.size() / sizeof(float);
      std::vector<float> samples(p, p + num_samples);

      {
        std::lock_guard<std::mutex> lock(c->mutex);
        c->samples.push_back(std::move(samples));
      }

      asio::post(io_work_, [this, c]() { decoder_.AcceptWaveform(c.get()); });
      break;
    }
    default:
      break;
  }
}

void OnlineWebsocketServer::Close(connection_hdl hdl,
                                  websocketpp::close::status::value code,
                                  const std::string &reason) {
  auto con = server_.get_con_from_hdl(hdl);

  if (reason == "Done") {
    std::lock_guard<std::mutex> lock(mutex_);
    auto state_iter = connection_log_states_.find(hdl);
    if (state_iter != connection_log_states_.end()) {
      state_iter->second.second = true;
    }
  }

  std::ostringstream os;
  os << "Closing " << con->get_remote_endpoint() << " with reason: " << reason
     << "\n";

  websocketpp::lib::error_code ec;
  server_.close(hdl, code, reason, ec);
  if (ec) {
    os << "Failed to close" << con->get_remote_endpoint() << ". "
       << ec.message() << "\n";
  }
  server_.get_alog().write(websocketpp::log::alevel::app, os.str());
}


#ifdef NEWTRANX_OFFLINE_MODEL
StreamRecognizer::StreamRecognizer(
      VoiceActivityDetector* vad, 
      OfflineRecognizer* recognizer,
      OfflinePunctuation* punctuation) {
  if (!vad || !recognizer)
    return;

  connection.is_android = true;
  connection.vad = vad;
  connection.vad_window_size = 512;
  connection.sample_rate = 16000;

  g_offline_recognizer = recognizer;
  g_offline_punc = punctuation;
  process_thread = std::move(std::thread([this](){process_voice();}));
}

StreamRecognizer::~StreamRecognizer() {
  running = 0;

  process_thread.join();
}

void StreamRecognizer::process_voice() {
  int vad_window_size = connection.vad_window_size;
  OnlineWebsocketDecoderConfig config;
  running = 1;

  while (running) {
    std::vector<float> pcm_data_process = {};
    
    std::unique_lock<std::mutex> frame_lock(frame_mutex);
    if (frame.size() > 0) {
      pcm_data_process.swap(connection.pcm_data_left);
      pcm_data_process.insert(pcm_data_process.end(), 
            frame.begin(), frame.end());
      frame = {};
    }
    frame_lock.unlock();

    if (pcm_data_process.size() > connection.vad_window_size) {
#if COPY_PYTHON
      asr_decode_and_process((void*)this, &connection, pcm_data_process);
#else
      vad_asr((void*)this, &connection, config, pcm_data_process);
#endif
    } else {
      connection.pcm_data_left.insert(connection.pcm_data_left.end(), 
                            pcm_data_process.begin(), 
                            pcm_data_process.end());
    }

    if (0 == running) break;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

int StreamRecognizer::WriteAudio(const float* samples, int32_t n) {
  if (samples && (n>0)) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    frame.insert(frame.end(), samples, samples+n);
  }
  return 0;
}

void StreamRecognizer::PutText(std::string text) {
  
  if (!text.empty()) {
    std::unique_lock<std::mutex> text_lock(text_mutex);
    recognize_text.push_back(text);
  }
}

string StreamRecognizer::ReadText() {
  std::string str;

  std::lock_guard<std::mutex> lock(text_mutex);  
  if (!recognize_text.empty()) {
    str = std::move(recognize_text.front());
    recognize_text.erase(recognize_text.begin()); 
  }
  return str;
}

#endif

}  // namespace sherpa_onnx
