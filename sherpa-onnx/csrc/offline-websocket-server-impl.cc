// sherpa-onnx/csrc/offline-websocket-server-impl.cc
//
// Copyright (c)  2022-2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/offline-websocket-server-impl.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/obfuscator_tool.h"
#include "sherpa-onnx/csrc/sdk-helper.h"
#include "sherpa-onnx/csrc/wave-writer.h"

#include "sherpa-onnx/csrc/utf8-encap.h"
#include "sherpa-onnx/nlohmann/json.hpp"

#include "asio/ip/tcp.hpp"

using json = nlohmann::json;

#define MAX_SPEAKERS 5
#define SAMPLES_PER_MS 16

namespace sherpa_onnx {

typedef int32_t (*TtsAudioProgressCallbackWithArg)(
    const float *samples, int32_t n, float p, 
    OfflineWebsocketDecoder& decoder, connection_hdl hdl);

OfflinePunctuation* g_offline_punc = nullptr;
SpeakerEmbeddingExtractor* g_speaker_extractor = nullptr;
std::map<connection_hdl, std::shared_ptr<Connection>, std::owner_less<connection_hdl>> g_connections;
std::mutex g_connections_mutex;

void asr_post_process(OfflineRecognitionResult& result, 
                OfflineWebsocketDecoderConfig config) {
  
  utf8_erase_unk(result.text);

  result.lang = extract_language(result.lang);

  if (config.language_type == "bo") {
    //result.text = utf8_replace_string(result.text, " ", "་");
    drop_leading_tsheg(result.text);
    add_end_tsheg(result.text);

    if (result.lang == "zh") result.lang = "bo-ad";
    if (result.lang == "en") result.lang = "bo-kb";
    if (result.lang == "ko") result.lang = "bo-wz";
  }
}

std::string assemble_offline_recognizer_result(OfflineRecognitionResult& result,
                          OfflineWebsocketDecoderConfig config,
                          std::string speaker_id,
                          uint32_t text_offset_ts = 0,
                          uint32_t text_duration_ts = 0) {
  asr_post_process(result, config);

  std::string offline_str = result.AsJsonString();

  std::string text_offset_str = R"(, "text_offset":)";
  text_offset_str.append("\"").append(std::to_string(text_offset_ts)).append("\"");

  std::string text_duration_str = R"(, "text_duration":)";
  text_duration_str.append("\"").append(std::to_string(text_duration_ts)).append("\"");

  std::string speaker_str = R"(, "speaker":)";
  speaker_str.append("\"").append(speaker_id).append("\"").append("}");

  if (!offline_str.empty()) {
    offline_str.pop_back();
    offline_str += text_offset_str;
    offline_str += text_duration_str;
    offline_str += speaker_str;
  }
  return offline_str;
}

OfflineSpeakerDiarization* create_speaker_diarization(OfflineWebsocketDecoderConfig& config) {
  OfflineSpeakerDiarization* diarization = nullptr;
  if (config.speaker_diarization_config.segmentation.pyannote.model.empty()) return nullptr;
  if (config.speaker_diarization_config.embedding.model.empty()) return nullptr;

  std::filesystem::path model_path_tmp = sherpa_onnx::mount_fs();
  if (!model_path_tmp.empty()) {
    OfflineSpeakerDiarizationConfig speaker_diarization_config = config.speaker_diarization_config;

    std::filesystem::path tmp_m = model_path_tmp / "segmentation.onnx";
    obf_defuse(get_license_key(), 
        config.speaker_diarization_config.segmentation.pyannote.model, tmp_m.string());
    speaker_diarization_config.segmentation.pyannote.model = tmp_m.string();
    speaker_diarization_config.segmentation.num_threads = 5;
    //speaker_diarization_config.segmentation.provider = "cuda";

    tmp_m = model_path_tmp / "embedding.onnx";
    obf_defuse(get_license_key(), 
        config.speaker_diarization_config.embedding.model, tmp_m.string());
    speaker_diarization_config.embedding.model = tmp_m.string();
    speaker_diarization_config.embedding.num_threads = 5;
    //speaker_diarization_config.embedding.provider = "cuda";

    SHERPA_ONNX_LOG(INFO) << speaker_diarization_config.ToString() << "\n";
    diarization = new OfflineSpeakerDiarization(speaker_diarization_config);
    umount_fs(model_path_tmp);
  }

  return diarization;
}

void create_speaker_extractor(OfflineWebsocketDecoderConfig& config) {
  if (nullptr != g_speaker_extractor) return ;
  if (config.speaker_model.empty()) return;

  std::filesystem::path model_path_tmp = sherpa_onnx::mount_fs();
  if (!model_path_tmp.empty()) {
    std::filesystem::path tmp_m = model_path_tmp / "wespeaker.onnx";
    obf_defuse(get_license_key(), config.speaker_model, tmp_m.string());
    config.speaker_extractor_config.model = tmp_m.string();
    config.speaker_extractor_config.num_threads = 5;
    //config.speaker_extractor_config.provider = "cuda";
    SHERPA_ONNX_LOG(INFO) << config.speaker_extractor_config.ToString() << "\n";
    g_speaker_extractor = new SpeakerEmbeddingExtractor(config.speaker_extractor_config);
    umount_fs(model_path_tmp);
  }
}

SpeakerEmbeddingManager* create_speaker_manager() {
  SpeakerEmbeddingManager* speaker_manager = nullptr;

  if (g_speaker_extractor) {
    speaker_manager = new SpeakerEmbeddingManager(g_speaker_extractor->Dim());
  }

  return speaker_manager;
}

void create_offline_punct(OfflineWebsocketDecoderConfig& config) {
  if (nullptr != g_offline_punc) return;
  if (config.offline_punctuation_model_config.ct_transformer.empty()) return;

  std::filesystem::path model_path_tmp = sherpa_onnx::mount_fs();
  if (!model_path_tmp.empty()) {
    std::filesystem::path tmp_m = model_path_tmp / "ct_transformer.onnx";
    
    obf_defuse(get_license_key(), config.offline_punctuation_model_config.ct_transformer, tmp_m.string());
    config.offline_punctuation_model_config.ct_transformer = tmp_m.string();
    config.offline_punctuation_model_config.num_threads = 5;
    config.offline_punctuation_model_config.provider = "cuda";
    SHERPA_ONNX_LOG(INFO) << config.offline_punctuation_model_config.ToString() << "\n";
    OfflinePunctuationConfig offline_punc_config 
          = OfflinePunctuationConfig(config.offline_punctuation_model_config);
    g_offline_punc = new OfflinePunctuation(offline_punc_config);
    umount_fs(model_path_tmp);
  }
}

VoiceActivityDetector* create_offline_vad(OfflineWebsocketDecoderConfig& config) {
  VoiceActivityDetector* vad = nullptr;
  if (config.vad_config.silero_vad.model.empty()) return vad;

  std::filesystem::path model_path_tmp = sherpa_onnx::mount_fs();
  if (!model_path_tmp.empty()) {
    std::filesystem::path tmp_m = model_path_tmp / "silero_vad.onnx";
    obf_defuse(get_license_key(), config.vad_config.silero_vad.model, tmp_m.string());

    VadModelConfig vad_config;
    vad_config = config.vad_config;
    vad_config.silero_vad.model = tmp_m.string();
    vad_config.num_threads = 5;
    //vad_config.provider = "cuda";
    SHERPA_ONNX_LOG(INFO) << vad_config.ToString() << "\n";
    vad = new sherpa_onnx::VoiceActivityDetector(vad_config, 100);
    umount_fs(model_path_tmp);
  }
  return vad;
}

OfflineRecognizer* create_offline_recognizer(OfflineRecognizerConfig& config) {
  OfflineRecognizer* recognizer = nullptr;
  
  std::filesystem::path model_path_tmp = sherpa_onnx::mount_fs();
  if (!model_path_tmp.empty()) {
    std::filesystem::path tmp_tokens = model_path_tmp / "tokens.txt";
    if (!config.model_config.tokens.empty()) {
      obf_defuse(get_license_key(), config.model_config.tokens, tmp_tokens.string());
      config.model_config.tokens = tmp_tokens.string();
    }
    
    std::filesystem::path tmp_m = model_path_tmp / "model.onnx";
    if (!config.model_config.wenet_ctc.model.empty()){
      obf_defuse(get_license_key(), config.model_config.wenet_ctc.model, tmp_m.string());
      config.model_config.wenet_ctc.model = tmp_m.string();
    }
    
    if (!config.model_config.sense_voice.model.empty()){
      obf_defuse(get_license_key(), config.model_config.sense_voice.model, tmp_m.string());
      config.model_config.sense_voice.model = tmp_m.string();
    }
  
    if (!config.model_config.paraformer.model.empty()){
      obf_defuse(get_license_key(), config.model_config.paraformer.model, tmp_m.string());
      config.model_config.paraformer.model = tmp_m.string();
    }

    if (!config.model_config.nemo_ctc.model.empty()){
      obf_defuse(get_license_key(), config.model_config.nemo_ctc.model, tmp_m.string());
      config.model_config.nemo_ctc.model = tmp_m.string();
    }

    if (!config.model_config.tdnn.model.empty()){
      obf_defuse(get_license_key(), config.model_config.tdnn.model, tmp_m.string());
      config.model_config.tdnn.model = tmp_m.string();
    }

    if (!config.model_config.zipformer_ctc.model.empty()){
      obf_defuse(get_license_key(), config.model_config.zipformer_ctc.model, tmp_m.string());
      config.model_config.zipformer_ctc.model = tmp_m.string();
    }

    if (!config.model_config.dolphin.model.empty()){
      obf_defuse(get_license_key(), config.model_config.dolphin.model, tmp_m.string());
      config.model_config.dolphin.model = tmp_m.string();
    }

    if (!config.model_config.omnilingual.model.empty()){
      obf_defuse(get_license_key(), config.model_config.omnilingual.model, tmp_m.string());
      config.model_config.omnilingual.model = tmp_m.string();
    }

    std::filesystem::path tmp_encoder = model_path_tmp / "encoder";
    std::filesystem::path tmp_decoder = model_path_tmp / "decoder";
    if (!config.model_config.whisper.encoder.empty()
        && !config.model_config.whisper.decoder.empty()){
      obf_defuse(get_license_key(), config.model_config.whisper.encoder, tmp_encoder.string());
      config.model_config.whisper.encoder = tmp_encoder.string();

      obf_defuse(get_license_key(), config.model_config.whisper.decoder, tmp_decoder.string());
      config.model_config.whisper.decoder = tmp_decoder.string();
    }
  
    if (!config.model_config.fire_red_asr.encoder.empty()
        && !config.model_config.fire_red_asr.decoder.empty()){
      obf_defuse(get_license_key(), config.model_config.fire_red_asr.encoder, tmp_encoder.string());
      config.model_config.fire_red_asr.encoder = tmp_encoder.string();

      obf_defuse(get_license_key(), config.model_config.fire_red_asr.decoder, tmp_decoder.string());
      config.model_config.fire_red_asr.decoder = tmp_decoder.string();
    }

    if (!config.model_config.canary.encoder.empty()
        && !config.model_config.canary.decoder.empty()){
      obf_defuse(get_license_key(), config.model_config.canary.encoder, tmp_encoder.string());
      config.model_config.canary.encoder = tmp_encoder.string();

      obf_defuse(get_license_key(), config.model_config.canary.decoder, tmp_decoder.string());
      config.model_config.canary.decoder = tmp_decoder.string();
    }

    std::filesystem::path tmp_joiner = model_path_tmp / "joiner";
    if (!config.model_config.transducer.encoder_filename.empty()
        && !config.model_config.transducer.decoder_filename.empty()
        && !config.model_config.transducer.joiner_filename.empty()){
      obf_defuse(get_license_key(), config.model_config.transducer.encoder_filename, tmp_encoder.string());
      config.model_config.transducer.encoder_filename = tmp_encoder.string();

      obf_defuse(get_license_key(), config.model_config.transducer.decoder_filename, tmp_decoder.string());
      config.model_config.transducer.decoder_filename = tmp_decoder.string();

      obf_defuse(get_license_key(), config.model_config.transducer.joiner_filename, tmp_joiner.string());
      config.model_config.transducer.joiner_filename = tmp_joiner.string();
    }
    
    std::filesystem::path tmp_preprocessor = model_path_tmp / "preprocessor";
    std::filesystem::path tmp_uncached_decoder = model_path_tmp / "uncached_decoder";
    std::filesystem::path tmp_cached_decoder = model_path_tmp / "cached_decoder";
    if (!config.model_config.moonshine.preprocessor.empty()
        && !config.model_config.moonshine.encoder.empty()
        && !config.model_config.moonshine.uncached_decoder.empty()
        && !config.model_config.moonshine.cached_decoder.empty()) {
      obf_defuse(get_license_key(), config.model_config.moonshine.preprocessor, tmp_preprocessor.string());
      config.model_config.moonshine.preprocessor = tmp_preprocessor.string();

      obf_defuse(get_license_key(), config.model_config.moonshine.encoder, tmp_encoder.string());
      config.model_config.moonshine.encoder = tmp_encoder.string();

      obf_defuse(get_license_key(), config.model_config.moonshine.uncached_decoder, tmp_uncached_decoder.string());
      config.model_config.moonshine.uncached_decoder = tmp_uncached_decoder.string();

      obf_defuse(get_license_key(), config.model_config.moonshine.cached_decoder, tmp_cached_decoder.string());
      config.model_config.moonshine.cached_decoder = tmp_cached_decoder.string();
    }

    std::filesystem::path tmp_encoder_adaptor = model_path_tmp / "encoder";
    std::filesystem::path tmp_embedding = model_path_tmp / "embedding";
    if (!config.model_config.funasr_nano.encoder_adaptor.empty()
      && !config.model_config.funasr_nano.llm.empty()
      && !config.model_config.funasr_nano.embedding.empty()) {
      obf_defuse(get_license_key(), config.model_config.funasr_nano.encoder_adaptor, tmp_encoder_adaptor.string());
      config.model_config.funasr_nano.encoder_adaptor = tmp_encoder_adaptor.string();
      obf_defuse(get_license_key(), config.model_config.funasr_nano.embedding, tmp_embedding.string());
      config.model_config.funasr_nano.embedding = tmp_embedding.string();
    }
  
    SHERPA_ONNX_LOG(INFO) << config.ToString() << "\n";
    recognizer = new OfflineRecognizer(config); 

    umount_fs(model_path_tmp);
  }

  return recognizer;
}

OfflineTts* create_offline_tts(OfflineWebsocketDecoderConfig& config) {
  OfflineTts* tts = nullptr;
  OfflineTtsConfig offline_tts_config = config.offline_tts_config;
  
  std::filesystem::path model_path_tmp = sherpa_onnx::mount_fs();
  if (!model_path_tmp.empty()) {
    std::filesystem::path tmp_tokens;
    std::filesystem::path tmp_m;
    
    // vits
    tmp_tokens = model_path_tmp / "tokens.txt";
    if (!offline_tts_config.model.vits.tokens.empty()) {
      obf_defuse(get_license_key(), offline_tts_config.model.vits.tokens, tmp_tokens.string());
      offline_tts_config.model.vits.tokens = tmp_tokens.string();
    }

    tmp_m = model_path_tmp / "model.onnx";
    if (!offline_tts_config.model.vits.model.empty()){
      obf_defuse(get_license_key(), offline_tts_config.model.vits.model, tmp_m.string());
      offline_tts_config.model.vits.model = tmp_m.string();
    }
    
    // matcha
    if (!offline_tts_config.model.matcha.tokens.empty()) {
      obf_defuse(get_license_key(), offline_tts_config.model.matcha.tokens, tmp_tokens.string());
      offline_tts_config.model.matcha.tokens = tmp_tokens.string();
    }

    if (!offline_tts_config.model.matcha.acoustic_model.empty()){
      obf_defuse(get_license_key(), offline_tts_config.model.matcha.acoustic_model, tmp_m.string());
      offline_tts_config.model.matcha.acoustic_model = tmp_m.string();
    }

    // kokoro
    if (!offline_tts_config.model.kokoro.tokens.empty()) {
      obf_defuse(get_license_key(), offline_tts_config.model.kokoro.tokens, tmp_tokens.string());
      offline_tts_config.model.kokoro.tokens = tmp_tokens.string();
    }

    if (!offline_tts_config.model.kokoro.model.empty()){
      obf_defuse(get_license_key(), offline_tts_config.model.kokoro.model, tmp_m.string());
      offline_tts_config.model.kokoro.model = tmp_m.string();
    }

    // zipvoice
    if (!offline_tts_config.model.zipvoice.tokens.empty()) {
      obf_defuse(get_license_key(), offline_tts_config.model.zipvoice.tokens, tmp_tokens.string());
      offline_tts_config.model.zipvoice.tokens = tmp_tokens.string();
    }

    if (!offline_tts_config.model.zipvoice.decoder.empty()) {
      obf_defuse(get_license_key(), offline_tts_config.model.zipvoice.decoder, tmp_m.string());
      offline_tts_config.model.zipvoice.decoder = tmp_m.string();
    }

    std::filesystem::path tmp_text_model= model_path_tmp / "text_model.onnx";
    if (!offline_tts_config.model.zipvoice.encoder.empty()) {
      obf_defuse(get_license_key(), offline_tts_config.model.zipvoice.encoder, tmp_text_model.string());
      offline_tts_config.model.zipvoice.encoder = tmp_text_model.string();
    }

    // kitten
    if (!offline_tts_config.model.kitten.tokens.empty()) {
      obf_defuse(get_license_key(), offline_tts_config.model.kitten.tokens, tmp_tokens.string());
      offline_tts_config.model.kitten.tokens = tmp_tokens.string();
    }

    if (!offline_tts_config.model.kitten.model.empty()){
      obf_defuse(get_license_key(), offline_tts_config.model.kitten.model, tmp_m.string());
      offline_tts_config.model.kitten.model = tmp_m.string();
    }

    offline_tts_config.model.provider = "cuda";
    SHERPA_ONNX_LOG(INFO) << offline_tts_config.ToString() << "\n";
    tts = new OfflineTts(offline_tts_config); 

    umount_fs(model_path_tmp);
  }

  return tts;
}

std::shared_ptr<Connection> get_or_create_connection(
    connection_hdl hdl, OfflineWebsocketDecoderConfig& config) {
  std::lock_guard<std::mutex> lock(g_connections_mutex);
  auto it = g_connections.find(hdl);
  if (it != g_connections.end()) {
    return it->second;
  } else {
    // create a new connection
    auto c = std::make_shared<Connection>();
    c->hdl = hdl;
    //c->last_active = std::chrono::steady_clock::now();
    //c->switch_threshold = config_.recognition_time_threshold;
    c->vad = create_offline_vad(config);
    c->vad_window_size = config.vad_config.silero_vad.window_size;
    c->speaker_manager = create_speaker_manager();
    c->speaker_diarization = create_speaker_diarization(config);
    g_connections.insert({hdl, c});
    return c;  
  }
}

std::shared_ptr<Connection> get_connection(connection_hdl hdl) {
  std::lock_guard<std::mutex> lock(g_connections_mutex);
  auto it = g_connections.find(hdl);
  if (it != g_connections.end()) {
    return it->second;
  }  
  return nullptr;
}

void remove_connection(connection_hdl hdl) {
  std::lock_guard<std::mutex> lock(g_connections_mutex);
  g_connections.erase(hdl);
}

std::string speaker_compute_embedding(SpeakerEmbeddingManager* manager, 
            int sample_rate, const std::vector<float>& samples, float threshold) {
  
  std::string speaker;
  std::vector<float> embedding;
  if ((nullptr == g_speaker_extractor) || (nullptr == manager)) {
    SHERPA_ONNX_LOG(ERROR) << "speaker_compute_embedding, extractor or manager is null" << "\n";
    return speaker;
  }

  auto s = g_speaker_extractor->CreateStream();
  s->AcceptWaveform(sample_rate, samples.data(), samples.size());
  s->InputFinished();
  if (g_speaker_extractor->IsReady(s.get())) {
    embedding = g_speaker_extractor->Compute(s.get());

    //auto embedding_stirng = vector_2_string(embedding);
    //SHERPA_ONNX_LOG(INFO) << "embedding value: " << embedding_stirng << "\n";
    
    speaker = manager->Search(embedding.data(), threshold);
    if (speaker.empty()) {
      int speaker_in_manager = manager->NumSpeakers();
      if (speaker_in_manager < MAX_SPEAKERS){
        speaker = "speaker-" + std::to_string(speaker_in_manager);
        manager->Add(speaker, embedding.data());
      }
    } 
  }

  if (speaker.empty()) speaker = "UnKnown";
  //SHERPA_ONNX_LOG(INFO) << "speaker_compute_embedding, speaker id: " << speaker << "\n";
  
  return speaker;
}

std::vector<OfflineSpeakerDiarizationSegment> 
speaker_diarization_ptr(Connection* c, const float *audio, int32_t n) {
  std::vector<OfflineSpeakerDiarizationSegment> segments;

  if (c->speaker_diarization) {
    if (audio && (n > 8000)) {
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

void send_offline_result(OfflineWebsocketDecoder* decoder, 
                          connection_hdl hdl, 
                          OfflineRecognitionResult result, 
                          std::string speaker_id,
                          uint32_t text_offset_ts = 0,
                          uint32_t text_duration_ts = 0) {
  
  uint32_t offset_ts = text_offset_ts;
  uint32_t duration_ts = text_duration_ts;
  auto c = get_connection(hdl);
  if (c) {
    if (nullptr == c->vad) {
      if (result.timestamps.size() > 2) {
        offset_ts = c->last_samples_recognized / SAMPLES_PER_MS 
                      + result.timestamps.front() * 1000;
        duration_ts = (result.timestamps.back() 
                        - result.timestamps.front()) * 1000;
      }
      c->last_samples_recognized = c->samples_received;
    } 
  }
  
  std::string res = assemble_offline_recognizer_result(result, 
                  decoder->GetConfig(), 
                  speaker_id, 
                  offset_ts, 
                  duration_ts);
  
  asio::post(decoder->GetServer()->GetConnectionContext(), [decoder, hdl, res]() {
                  websocketpp::lib::error_code ec;
                  decoder->GetServer()->GetServer().send(hdl, res, websocketpp::frame::opcode::text, ec);
                  if (ec) {
                    decoder->GetServer()->GetServer().get_alog().write(
                    websocketpp::log::alevel::app, ec.message());}
              });
  SHERPA_ONNX_LOG(INFO) << "result: " << res << "\n";
}

std::unique_ptr<OfflineStream> create_offline_stream (
          connection_hdl hdl, 
          OfflineRecognizer* recognizer, 
          OfflineWebsocketDecoderConfig& config,
          int32_t sample_rate, 
          const float *samples, 
          int32_t num_samples, 
          int file_end) {
  std::unique_ptr<OfflineStream> s;
  auto c = get_or_create_connection(hdl, config);
  c->sample_rate = sample_rate;
  c->file_end = file_end;

  if (nullptr == c->vad) {
    s = recognizer->CreateStream();
    s->AcceptWaveform(sample_rate, samples, num_samples);
    c->vad_segments.insert(c->vad_segments.end(), samples, samples + num_samples);
    c->samples_received += num_samples;
  } else {
    int process_length = 0;
    std::vector<float> pcm_data_process = {};
    if ((num_samples > 0)) {
      pcm_data_process.swap(c->pcm_data_left);
      pcm_data_process.insert(pcm_data_process.end(), 
            samples, 
            samples + num_samples);
    }

    for (; (process_length + c->vad_window_size) < pcm_data_process.size();
                                         process_length += c->vad_window_size) {
      c->vad->AcceptWaveform(pcm_data_process.data() + process_length, c->vad_window_size);
      c->silent_data_length += c->vad_window_size;
      c->samples_vaded += c->vad_window_size;

      if (!c->started && c->vad->IsSpeechDetected()) {
        c->started = true;
        c->started_time = std::chrono::steady_clock::now();
      }
      if (c->vad->IsSpeechDetected()) {
        c->silent_data_length = 0;
      }
    }
    
    int left_data_length = pcm_data_process.size() - process_length; 
    if (left_data_length > 0) {
      if (c->file_end) {
        c->vad->AcceptWaveform(pcm_data_process.data() + process_length, left_data_length);
      } else {
        c->pcm_data_left.insert(c->pcm_data_left.end(), 
                      pcm_data_process.begin() + process_length, 
                      pcm_data_process.end());
      }
    }
    
    // 
    if (c->file_end) c->vad->Flush();
    while(!c->vad->Empty()) {
      const auto &segment = c->vad->Front();
      c->vad_segments.insert(c->vad_segments.end(),
                        segment.samples.begin(),
                        segment.samples.end());
      c->vad->Pop();
      //if (0 == c->samples_vaded_first)
      //  c->samples_vaded_first = segment.start;
    }
    
    if (c->vad_segments.size()>0) {
      s = recognizer->CreateStream();
      s->AcceptWaveform(sample_rate, c->vad_segments.data(), c->vad_segments.size());
    }
  }

  return s;
}

void decode_offline_stream (
          OfflineWebsocketDecoder* decoder,
          connection_hdl hdl, 
          OfflineRecognizer* recognizer, 
          OfflineWebsocketDecoderConfig& config,
          int32_t sample_rate, 
          const float *samples, 
          int32_t num_samples, 
          int file_end) {
  std::unique_ptr<OfflineStream> s;
  auto c = get_or_create_connection(hdl, config);
  c->sample_rate = sample_rate;
  c->file_end = file_end;

  int process_length = 0;
  if (!(num_samples > 0)) return;
  for (; (process_length + c->vad_window_size) < num_samples;
                                        process_length += c->vad_window_size) {
    c->vad->AcceptWaveform(samples + process_length, c->vad_window_size);
  }
  
  // 
  std::string speaker_id;
  if (1 == c->file_end) c->vad->Flush();
  while(!c->vad->Empty()) {
    const auto &segment = c->vad->Front();
    
    if (c->speaker_diarization && (segment.samples.size() > 16000)) {
      
      auto before = std::chrono::steady_clock::now();
      auto speakers = speaker_diarization(c.get(), segment.samples);
      auto after = std::chrono::steady_clock::now();
      const float elapsed_milliseconds
              = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();
      SHERPA_ONNX_LOG(INFO) << "speaker_diarization, elapsed_milliseconds: " << elapsed_milliseconds 
                            << ", samples size: " << segment.samples.size() << "\n";
      
      if (speakers.size() < 2) {
        speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, 
                        segment.samples, c->speaker_similarity_threshold);
        
        auto s = recognizer->CreateStream();
        s->AcceptWaveform(sample_rate, segment.samples.data(), segment.samples.size());
        recognizer->DecodeStream(s.get());

        auto asr_result = s->GetResult();
        send_offline_result(decoder, hdl, asr_result, speaker_id, segment.start / SAMPLES_PER_MS, 
                segment.samples.size() / SAMPLES_PER_MS);
      } else {
        for (auto itr = speakers.begin(); itr != speakers.end(); ++itr) {
          SHERPA_ONNX_LOG(INFO) << itr->ToString() << ", " << itr->Duration() << "\n";

          std::vector<float> samples_tmp;
          samples_tmp.insert(samples_tmp.end(), 
                              segment.samples.begin() + itr->Start() * c->sample_rate,
                              segment.samples.begin() + itr->End() * c->sample_rate);
          
          speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, samples_tmp, 0.5);

          auto s = recognizer->CreateStream();
          s->AcceptWaveform(sample_rate, samples_tmp.data(), samples_tmp.size());
          recognizer->DecodeStream(s.get());

          auto asr_result = s->GetResult();
          send_offline_result(decoder, hdl, asr_result, speaker_id, 
                                segment.start / SAMPLES_PER_MS + itr->Start() * 1000, 
                                itr->Duration() * 1000);
        }
      }
    } else {
      auto s = recognizer->CreateStream();
      s->AcceptWaveform(sample_rate, segment.samples.data(), segment.samples.size());
      recognizer->DecodeStream(s.get());

      auto asr_result = s->GetResult();
      speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, segment.samples, 0.5);
      send_offline_result(decoder, hdl, asr_result, speaker_id, segment.start / SAMPLES_PER_MS, 
                segment.samples.size() / SAMPLES_PER_MS);
    }
    
    c->vad->Pop();
  }
  
  if (1 == c->file_end) {
    OfflineRecognitionResult res;
    res.event = std::string("audio_file_end");

    send_offline_result(decoder, hdl, res, speaker_id);
    c->vad->Reset();
  }
}

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
        //SHERPA_ONNX_LOGE("tokens[i]: %s, %f", asr_result.tokens[i].data(), timestamp);
        break;
      }
    }
  } 

  return std::make_tuple(previous_timestamp, timestamp, next_timestamp);
}

OfflineRecognitionResult decode_waveform_to_text(
        OfflineRecognizer* recognizer,
        OfflineWebsocketDecoderConfig& config,
        int sample_rate, 
        std::vector<float>& vad_front_samples) {
  OfflineRecognitionResult asr_result;
  if (nullptr == recognizer) return asr_result;
  if (vad_front_samples.empty()) return asr_result;

  std::unique_ptr<OfflineStream> offline_stream = recognizer->CreateStream();
  if (!offline_stream) return asr_result;
  
  offline_stream->AcceptWaveform(sample_rate, vad_front_samples.data(), vad_front_samples.size());
  
  //auto current_time = std::chrono::steady_clock::now();
  recognizer->DecodeStream(offline_stream.get());

  /*
  const float elapsed_milliseconds
            = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - current_time).count();
  */
  asr_result = offline_stream->GetResult();
  
  //SHERPA_ONNX_LOG(INFO) << "result: " << asr_result.text.data() 
  //                << ", elapsed_ms: " << elapsed_milliseconds 
  //                << ", size: " << vad_front_samples.size() << "\n"; 
  
  return asr_result;
}

void process_offline_reslut(OfflineWebsocketDecoder* decoder,
                connection_hdl hdl, OfflineRecognitionResult result) {

  OfflineWebsocketDecoderConfig config = decoder->GetConfig();
  auto c = get_connection(hdl);
  std::string speaker_id;
  if (config.recognizer_config.model_config.sense_voice.use_itn) {
    if (c->file_end == 1) {
      speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, c->vad_segments, 0.5);
      c->vad_segments.clear();
      send_offline_result(decoder, hdl, result, speaker_id);
    } else {
      auto[previous_timestamp, timestamp, next_timestamp] = get_line_timestamp(result);
      std::vector<float> front(c->vad_segments.begin(), c->vad_segments.begin() + next_timestamp*c->sample_rate);
      c->vad_segments.erase(c->vad_segments.begin(), c->vad_segments.begin() + timestamp*c->sample_rate);
      c->samples_vaded -= c->vad_segments.size();
      auto eof_result = decode_waveform_to_text(decoder->GetRecognizer(), config, c->sample_rate, front);
      speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, front, 0.5);
      send_offline_result(decoder, hdl, eof_result, speaker_id); 
    }
  } else {
    speaker_id = speaker_compute_embedding(c->speaker_manager, c->sample_rate, c->vad_segments, 0.5);
    c->vad_segments.clear();

    if (g_offline_punc) {
      result.text = g_offline_punc->AddPunctuation(result.text);
    }

    send_offline_result(decoder, hdl, result, speaker_id); 
  }
  
  if (c->file_end == 1) {
    OfflineRecognitionResult res;
    res.event = std::string("audio_file_end");

    send_offline_result(decoder, hdl, res, speaker_id);
    c->samples_vaded_first = 0;
    c->vad->Clear();
  }
}

void send_tts_audio(OfflineWebsocketDecoder* decoder, connection_hdl hdl, 
                int32_t sample_rate, const float* samples, int32_t n, float progress) {

  if ((samples == nullptr) || (n <= 0)) return;

  std::vector<int16_t> samples_int16_ch0(n);
  for (int32_t i = 0; i != n; ++i) {
    samples_int16_ch0[i] = std::min<int32_t>(samples[i] * 32767, 32767);
  }

  int size = n*2 + 3 * 4;
  char* data = (char*)malloc(size);
  if (nullptr == data) {
    SHERPA_ONNX_LOG(ERROR) << "send_tts_audio, malloc error" << "\n";
    return ;
  }
 
  *reinterpret_cast<int32_t *>(data) = sample_rate;
  *reinterpret_cast<int32_t *>(data + 4) = size;

  uint32_t u_progress;
  std::memcpy(&u_progress, &progress, sizeof(u_progress));
  *reinterpret_cast<int32_t *>(data + 8) = u_progress;
  
  memcpy(data + 12, samples_int16_ch0.data(), samples_int16_ch0.size() * 2);

  asio::post(decoder->GetServer()->GetConnectionContext(), 
            [decoder, hdl, data, size]() {
              websocketpp::lib::error_code ec;
              decoder->GetServer()->GetServer().send(hdl, data, size, 
                                  websocketpp::frame::opcode::binary, ec);
              if (ec) {
                decoder->GetServer()->GetServer().get_alog()
                        .write(websocketpp::log::alevel::app, ec.message());
              }

              SHERPA_ONNX_LOG(INFO) << "send_tts_audio, send data length: " << size << "\n";
              free(data);
            });

}

void send_tts_result(OfflineWebsocketDecoder* decoder, connection_hdl hdl, 
                  GeneratedAudio audio) {
  
  send_tts_audio(decoder, hdl, 
    audio.sample_rate, audio.samples.data(), audio.samples.size(), 1.0f);
  /*  
  float duration = audio.samples.size() / static_cast<float>(audio.sample_rate);

  fprintf(stderr, "Audio duration: %.3f s\n", duration);
  std::string output_filename = "tts_output.wav";


  bool ok = sherpa_onnx::WriteWave(output_filename, audio.sample_rate,
                                   audio.samples.data(), audio.samples.size());

  if (!ok) {
    fprintf(stderr, "Failed to write wave to %s\n", output_filename.c_str());
    exit(EXIT_FAILURE);
  }
//  */
}

void process_tts(OfflineWebsocketDecoder& decoder,
                connection_hdl hdl, 
                std::string data,
                int64_t sid = 0, 
                float speed = 1.0,
                int num_steps = 4) {
  if (!decoder.offline_tts) {
    SHERPA_ONNX_LOGE("TTS model is not initialized");
    return;
  }
  
  asio::post(decoder.GetServer()->GetConnectionContext(),
      [&decoder, hdl, data, sid, speed]() {
        OfflineTts* tts = decoder.offline_tts;
        
        int32_t sample_rate = tts->SampleRate(); 
        auto audio = tts->Generate(data, sid, speed, 
          [&decoder, hdl, sample_rate](const float* samples, int32_t n,
                          float progress) -> int32_t {
            send_tts_audio(&decoder, hdl, sample_rate, samples, n, progress);
            return 1;
          });
        //send_tts_result(&decoder, hdl, audio);
      });
}

void process_websocket_text(OfflineWebsocketDecoder& decoder,
                connection_hdl hdl, 
                std::string payload) {
  SHERPA_ONNX_LOGE("Number of active connections: %s", payload.data());
        
  json j = json::parse(payload);
  std::string data = j.value("text", "");
  int sid = j.value("sid", 0);
  float speed = j.value("speed", 1.0f);
  int32_t num_steps = j.value("num_steps", 4);
  if (!data.empty()) {
    process_tts(decoder, hdl, data, sid, speed, num_steps);
  }
}

void OfflineWebsocketDecoderConfig::Register(ParseOptions *po) {
  recognizer_config.Register(po);
  vad_config.Register(po);
  speaker_extractor_config.Register(po);
  speaker_diarization_config.Register(po);
  offline_punctuation_model_config.Register(po);
  offline_tts_config.Register(po);

  po->Register("speaker-model", &speaker_model, "speaker model"); 
  po->Register("language-type", &language_type, "language type");
  po->Register("recognize-captions", &recognize_captions, "recognize captions");
  po->Register("speaker-similarity-threshold", &speaker_similarity_threshold, "speaker similarity threshold");
  po->Register("max-batch-size", &max_batch_size,
               "Max batch size for decoding.");
  po->Register("enable-tts", &enable_tts, "enable tts");


  po->Register(
      "max-utterance-length", &max_utterance_length,
      "Max utterance length in seconds. If we receive an utterance "
      "longer than this value, we will reject the connection. "
      "If you have enough memory, you can select a large value for it.");
}

void OfflineWebsocketDecoderConfig::Validate() const {
  /*
  if (!recognizer_config.Validate()) {
    SHERPA_ONNX_LOGE("Error in recongizer config");
    exit(-1);
  }
  */

  if (max_batch_size <= 0) {
    SHERPA_ONNX_LOGE("Expect --max-batch-size > 0. Given: %d", max_batch_size);
    exit(-1);
  }

  if (max_utterance_length <= 0) {
    SHERPA_ONNX_LOGE("Expect --max-utterance-length > 0. Given: %f",
                     max_utterance_length);
    exit(-1);
  }
}

OfflineWebsocketDecoder::OfflineWebsocketDecoder(OfflineWebsocketServer *server)
    : config_(server->GetConfig().decoder_config),
      server_(server) {
  if (config_.enable_tts) {
    offline_tts = create_offline_tts(config_);  
  } else {
    recognizer_ = create_offline_recognizer(config_.recognizer_config);
    create_offline_punct(config_);
    create_speaker_extractor(config_);
  }
}

OfflineWebsocketDecoder::~OfflineWebsocketDecoder() {
  if (recognizer_) {
    delete recognizer_; 
    recognizer_ = nullptr;
  }
  if (g_offline_punc) {
    delete g_offline_punc;
    g_offline_punc = nullptr;
  }
  if (g_speaker_extractor) {
    delete g_speaker_extractor;
    g_speaker_extractor = nullptr;
  }
}

void OfflineWebsocketDecoder::Push(connection_hdl hdl, ConnectionDataPtr d) {
  std::lock_guard<std::mutex> lock(stream_mutex);
  streams_.push_back({hdl, d});
}

void OfflineWebsocketDecoder::DecodeCaptions() {
  std::unique_lock<std::mutex> lock(stream_mutex);
  if (recognizer_ == nullptr) {
    SHERPA_ONNX_LOGE("recognizer_ is null");
    return;
  }

  if (streams_.empty()) {
    return;
  }

  int32_t size =
      std::min(static_cast<int32_t>(streams_.size()), config_.max_batch_size);
  
  // We first lock the mutex for streams_, take items from it, and then
  // unlock the mutex; in doing so we don't need to lock the mutex to
  // access hdl and connection_data later.
  std::vector<connection_hdl> handles(size);

  // Store connection_data here to prevent the data from being freed
  // while we are still using it.
  std::vector<ConnectionDataPtr> connection_data(size);

  std::vector<const float *> samples(size);
  //std::vector<int32_t> samples_length(size);
  std::vector<std::unique_ptr<OfflineStream>> ss(size);
  std::vector<OfflineStream *> p_ss(size);

  for (int32_t i = 0; i != size; ++i) {
    auto &p = streams_.front();
    handles[i] = p.first;
    connection_data[i] = p.second;
    streams_.pop_front();

    auto sample_rate = connection_data[i]->sample_rate;
    auto samples =
        reinterpret_cast<const float *>(&connection_data[i]->data[0]);
    //auto num_samples = connection_data[i]->expected_byte_size / sizeof(float);
    auto num_samples = connection_data[i]->data.size() / sizeof(float);
    decode_offline_stream(this, handles[i], recognizer_, config_, 
                    sample_rate, samples, num_samples, connection_data[i]->file_end);

    //ss[i] = std::move(s);
    //p_ss[i] = ss[i].get();
  }

}

void OfflineWebsocketDecoder::Decode_() {
  std::unique_lock<std::mutex> lock(stream_mutex);
  if (recognizer_ == nullptr) {
    SHERPA_ONNX_LOGE("recognizer_ is null");
    return;
  }

  if (streams_.empty()) {
    return;
  }

  int32_t size =
      std::min(static_cast<int32_t>(streams_.size()), config_.max_batch_size);
  
  // We first lock the mutex for streams_, take items from it, and then
  // unlock the mutex; in doing so we don't need to lock the mutex to
  // access hdl and connection_data later.
  std::vector<connection_hdl> handles(size);

  // Store connection_data here to prevent the data from being freed
  // while we are still using it.
  std::vector<ConnectionDataPtr> connection_data(size);

  std::vector<const float *> samples(size);
  //std::vector<int32_t> samples_length(size);
  std::vector<std::unique_ptr<OfflineStream>> ss(size);
  std::vector<OfflineStream *> p_ss(size);

  for (int32_t i = 0; i != size; ++i) {
    auto &p = streams_.front();
    handles[i] = p.first;
    connection_data[i] = p.second;
    streams_.pop_front();

    auto sample_rate = connection_data[i]->sample_rate;
    auto samples =
        reinterpret_cast<const float *>(&connection_data[i]->data[0]);
    //auto num_samples = connection_data[i]->expected_byte_size / sizeof(float);
    auto num_samples = connection_data[i]->data.size() / sizeof(float);
    auto s = create_offline_stream(handles[i], recognizer_, config_, 
                    sample_rate, samples, num_samples, connection_data[i]->file_end);

    ss[i] = std::move(s);
    p_ss[i] = ss[i].get();
  }

  //lock.unlock();

  // Note: DecodeStreams is thread-safe
  recognizer_->DecodeStreams(p_ss.data(), size);

  for (int32_t i = 0; i != size; ++i) {
    connection_hdl hdl = handles[i];
    auto result = ss[i]->GetResult();
    process_offline_reslut(this, hdl, result); 
  }
}

void OfflineWebsocketDecoder::Decode() {
  std::unique_lock<std::mutex> lock(stream_mutex);
  if (streams_.empty()) {
    return;
  }

  int32_t size =
      std::min(static_cast<int32_t>(streams_.size()), config_.max_batch_size);

  // We first lock the mutex for streams_, take items from it, and then
  // unlock the mutex; in doing so we don't need to lock the mutex to
  // access hdl and connection_data later.
  std::vector<connection_hdl> handles(size);

  // Store connection_data here to prevent the data from being freed
  // while we are still using it.
  std::vector<ConnectionDataPtr> connection_data(size);

  std::vector<const float *> samples(size);
  std::vector<int32_t> samples_length(size);
  std::vector<std::unique_ptr<OfflineStream>> ss(size);
  std::vector<OfflineStream *> p_ss(size);

  for (int32_t i = 0; i != size; ++i) {
    auto &p = streams_.front();
    handles[i] = p.first;
    connection_data[i] = p.second;
    streams_.pop_front();

    auto sample_rate = connection_data[i]->sample_rate;
    auto samples =
        reinterpret_cast<const float *>(&connection_data[i]->data[0]);
    //auto num_samples = connection_data[i]->expected_byte_size / sizeof(float);
    auto num_samples = connection_data[i]->data.size() / sizeof(float);
    //auto s = recognizer_.CreateStream();
    auto s = recognizer_->CreateStream();
    s->AcceptWaveform(sample_rate, samples, num_samples);

    ss[i] = std::move(s);
    p_ss[i] = ss[i].get();
  }

  lock.unlock();

  // Note: DecodeStreams is thread-safe
  std::unique_lock<std::mutex> lock_decode(decode_mutex);
  //recognizer_.DecodeStreams(p_ss.data(), size);
  recognizer_->DecodeStreams(p_ss.data(), size);

  for (int32_t i = 0; i != size; ++i) {
    connection_hdl hdl = handles[i];
    asio::post(server_->GetConnectionContext(),
               [this, hdl, result = ss[i]->GetResult(), connection_data = connection_data[i]]() {
                  websocketpp::lib::error_code ec;
                  //SHERPA_ONNX_LOGE("result: %s", result.AsJsonString().data());
                  server_->GetServer().send(hdl, result.AsJsonString(),
                                           websocketpp::frame::opcode::text,
                                           ec);

                  if (connection_data->file_end == 1) {
                    OfflineRecognitionResult res;
                    res.event = std::string("audio_file_end");
                    //SHERPA_ONNX_LOGE("result: %s", res.AsJsonString().data());
                    server_->GetServer().send(hdl, res.AsJsonString(),
                                           websocketpp::frame::opcode::text,
                                           ec);
                  }

                  if (ec) {
                    server_->GetServer().get_alog().write(
                        websocketpp::log::alevel::app, ec.message());
                  }
                });
  }
}

void OfflineWebsocketServerConfig::Register(ParseOptions *po) {
  decoder_config.Register(po);
  po->Register("log-file", &log_file,
               "Path to the log file. Logs are "
               "appended to this file");
}

void OfflineWebsocketServerConfig::Validate() const {
  decoder_config.Validate();
}

OfflineWebsocketServer::OfflineWebsocketServer(
    asio::io_context &io_conn,  // NOLINT
    asio::io_context &io_work,  // NOLINT
    const OfflineWebsocketServerConfig &config)
    : io_conn_(io_conn),
      io_work_(io_work),
      config_(config),
      log_(config.log_file, std::ios::app),
      tee_(std::cout, log_),
      decoder_(this) {
  SetupLog();

  server_.init_asio(&io_conn_);

  server_.set_open_handler([this](connection_hdl hdl) { OnOpen(hdl); });

  server_.set_close_handler([this](connection_hdl hdl) { OnClose(hdl); });

  server_.set_message_handler(
      [this](connection_hdl hdl, server::message_ptr msg) {
        //OnMessage(hdl, msg);
        OnMessage_(hdl, msg);
      });
  
  /*  
  server_.set_socket_init_handler([](websocketpp::connection_hdl, 
                                asio::ip::tcp::socket& s) {
    asio::socket_base::send_buffer_size option(65536);
    s.set_option(option);
  });
  
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
//  */
}

void OfflineWebsocketServer::SetupLog() {
  server_.clear_access_channels(websocketpp::log::alevel::all);
  server_.set_access_channels(websocketpp::log::alevel::connect);
  server_.set_access_channels(websocketpp::log::alevel::disconnect);

  // So that it also prints to std::cout and std::cerr
  server_.get_alog().set_ostream(&tee_);
  server_.get_elog().set_ostream(&tee_);
}

void OfflineWebsocketServer::OnOpen(connection_hdl hdl) {
  std::lock_guard<std::mutex> lock(mutex_);

  SHERPA_ONNX_LOGE("connections: %ld, %d", connections_.size(), get_max_connection());

  if (connections_.size() > get_max_connection()) {
    server_.close(hdl, websocketpp::close::status::policy_violation,
              "too many connections");
    return; 
  }

  connections_.emplace(hdl, std::make_shared<ConnectionData>());

  SHERPA_ONNX_LOGE("Number of active connections: %d",
                   static_cast<int32_t>(connections_.size()));

  server::connection_ptr con = server_.get_con_from_hdl(hdl);
  asio::ip::tcp::socket& socket = con->get_raw_socket();
  socket.set_option(asio::ip::tcp::no_delay(true));
}

void OfflineWebsocketServer::OnClose(connection_hdl hdl) {
  std::lock_guard<std::mutex> lock(mutex_);
  connections_.erase(hdl);

  SHERPA_ONNX_LOGE("Number of active connections: %d",
                   static_cast<int32_t>(connections_.size()));
}

void OfflineWebsocketServer::OnMessage_(connection_hdl hdl,
                                       server::message_ptr msg) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto connection_data = connections_.find(hdl)->second;
  lock.unlock();
  const std::string &payload = msg->get_payload();

  switch (msg->get_opcode()) {
    case websocketpp::frame::opcode::text:
      if (payload == "Done") {
        // The client will not send any more data. We can close the
        // connection now.
        Close(hdl, websocketpp::close::status::normal, "Done");
      } else {

        if (payload.size() > 0) {
          process_websocket_text(decoder_, hdl, payload);
        }
        
        //Close(hdl, websocketpp::close::status::normal,
        //      std::string("Invalid payload: ") + payload);
      }
      break;

    case websocketpp::frame::opcode::binary: {
      auto p = reinterpret_cast<const int8_t *>(payload.data());

      if (connection_data->expected_byte_size == 0) {
        if (payload.size() < 8) {
          Close(hdl, websocketpp::close::status::normal,
                "Payload is too short");
          break;
        }

        connection_data->sample_rate = *reinterpret_cast<const int32_t *>(p);

        connection_data->expected_byte_size =
            *reinterpret_cast<const int32_t *>(p + 4);

        max_byte_size_ = decoder_.GetConfig().max_utterance_length *
                                 connection_data->sample_rate * sizeof(float);
        
        connection_data->data.insert(connection_data->data.end() , payload.begin(), payload.end());
        connection_data->cur = payload.size() - 8;
        connection_data->slice_cur = connection_data->cur;
      } else {
        connection_data->data.insert(connection_data->data.end() , payload.begin(), payload.end());
        connection_data->cur += payload.size();
        connection_data->slice_cur += payload.size();

        if (connection_data->slice_cur > max_byte_size_) {
          std::shared_ptr<ConnectionData> d = std::make_shared<ConnectionData>();
          d->data.swap(connection_data->data);
          d->sample_rate = connection_data->sample_rate;
          connection_data->slice_cur = 0;

          decoder_.Push(hdl, d);
          if (decoder_.GetConfig().recognize_captions)
            asio::post(io_work_, [this]() { decoder_.DecodeCaptions(); });
          else
            asio::post(io_work_, [this]() { decoder_.Decode_(); });
        }
      }

      if (connection_data->expected_byte_size == connection_data->cur) {
        auto d = std::make_shared<ConnectionData>(std::move(*connection_data));
        // Clear it so that we can handle the next audio file from the client.
        // The client can send multiple audio files for recognition without
        // the need to create another connection.
        connection_data->sample_rate = 0;
        connection_data->expected_byte_size = 0;
        connection_data->cur = 0;
        connection_data->slice_cur = 0;

        d->file_end = 1;
        decoder_.Push(hdl, d);

        connection_data->Clear();

        if (decoder_.GetConfig().recognize_captions)
          asio::post(io_work_, [this]() { decoder_.DecodeCaptions(); });
        else
          asio::post(io_work_, [this]() { decoder_.Decode_(); });
      }
      break;
    }

    default:
      // Unexpected message, ignore it
      break;
  }
}

void OfflineWebsocketServer::OnMessage(connection_hdl hdl,
                                       server::message_ptr msg) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto connection_data = connections_.find(hdl)->second;
  lock.unlock();
  const std::string &payload = msg->get_payload();

  switch (msg->get_opcode()) {
    case websocketpp::frame::opcode::text:
      if (payload == "Done") {
        // The client will not send any more data. We can close the
        // connection now.
        Close(hdl, websocketpp::close::status::normal, "Done");
      } else {
        Close(hdl, websocketpp::close::status::normal,
              std::string("Invalid payload: ") + payload);
      }
      break;

    case websocketpp::frame::opcode::binary: {
      auto p = reinterpret_cast<const int8_t *>(payload.data());

      if (connection_data->expected_byte_size == 0) {
        if (payload.size() < 8) {
          Close(hdl, websocketpp::close::status::normal,
                "Payload is too short");
          break;
        }

        connection_data->sample_rate = *reinterpret_cast<const int32_t *>(p);

        connection_data->expected_byte_size =
            *reinterpret_cast<const int32_t *>(p + 4);

        int32_t max_byte_size_ = decoder_.GetConfig().max_utterance_length *
                                 connection_data->sample_rate * sizeof(float);
        if (connection_data->expected_byte_size > max_byte_size_) {
          //float num_samples =
          //    connection_data->expected_byte_size / sizeof(float);

          //float duration = num_samples / connection_data->sample_rate;

          std::ostringstream os;
          os << "Max utterance length is configured to "
             << decoder_.GetConfig().max_utterance_length
             << " seconds, "
             << "Payload is too large!";
          Close(hdl, websocketpp::close::status::message_too_big, os.str());
          break;
        }

        connection_data->data.resize(connection_data->expected_byte_size);
        std::copy(payload.begin() + 8, payload.end(),
                  connection_data->data.data());
        connection_data->cur = payload.size() - 8;
      } else {
        std::copy(payload.begin(), payload.end(),
                  connection_data->data.data() + connection_data->cur);
        connection_data->cur += payload.size();
      }

      if (connection_data->expected_byte_size == connection_data->cur) {
        auto d = std::make_shared<ConnectionData>(std::move(*connection_data));
        // Clear it so that we can handle the next audio file from the client.
        // The client can send multiple audio files for recognition without
        // the need to create another connection.
        connection_data->sample_rate = 0;
        connection_data->expected_byte_size = 0;
        connection_data->cur = 0;

        d->file_end = 1;
        decoder_.Push(hdl, d);

        connection_data->Clear();

        asio::post(io_work_, [this]() { decoder_.Decode(); });
      }
      break;
    }

    default:
      // Unexpected message, ignore it
      break;
  }
}

void OfflineWebsocketServer::Close(connection_hdl hdl,
                                   websocketpp::close::status::value code,
                                   const std::string &reason) {
  remove_connection(hdl);

  auto con = server_.get_con_from_hdl(hdl);

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

void OfflineWebsocketServer::Run(uint16_t port) {
  server_.set_reuse_addr(true);
  server_.listen(asio::ip::tcp::v4(), port);
  server_.start_accept();
}

}  // namespace sherpa_onnx
