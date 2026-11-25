// sherpa-onnx/csrc/online-websocket-server-impl.h
//
// Copyright (c)  2022-2023  Xiaomi Corporation

#ifndef SHERPA_ONNX_CSRC_ONLINE_WEBSOCKET_SERVER_IMPL_H_
#define SHERPA_ONNX_CSRC_ONLINE_WEBSOCKET_SERVER_IMPL_H_

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <regex>
#include <tuple>
#include <algorithm>
#include <atomic>

#include "asio.hpp"  // NOLINT
#include "sherpa-onnx/csrc/online-recognizer.h"
#include "sherpa-onnx/csrc/online-stream.h"
#include "sherpa-onnx/csrc/parse-options.h"
#include "websocketpp/config/asio_no_tls.hpp"  // TODO(fangjun): support TLS
#include "websocketpp/server.hpp"
#include "sherpa-onnx/csrc/wave-reader.h"
#include "sherpa-onnx/csrc/wave-writer.h"
#include "sherpa-onnx/csrc/offline-punctuation.h"
#include "sherpa-onnx/csrc/offline-recognizer.h"
#include "sherpa-onnx/csrc/vad-model-config.h"
#include "sherpa-onnx/csrc/voice-activity-detector.h"
#ifdef NEWTRANX_OFFLINE_MODEL
#include "sherpa-onnx/csrc/offline-stream.h"
#endif
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
#include "sherpa-onnx/csrc/speaker-embedding-extractor.h"
#include "sherpa-onnx/csrc/speaker-embedding-manager.h"
#include "sherpa-onnx/csrc/offline-speaker-diarization.h"
#endif


// #define NEWTRANX_OFFLINE_MODEL 1

/*
struct my_config : public websocketpp::config::asio {
  static const long timeout_pong = 0;   // 0 = 禁用
};
using server = websocketpp::server<my_config>;
// */
using server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;

namespace sherpa_onnx {

struct Connection {
  // handle to the connection. We can use it to send messages to the client
  connection_hdl hdl;
  std::shared_ptr<OnlineStream> s;

  // set it to true when InputFinished() is called
  bool eof = false;
  bool close_scheduled = false;

  // The last time we received a message from the client
  // TODO(fangjun): Use it to disconnect from a client if it is inactive
  // for a specified time.
  std::chrono::steady_clock::time_point last_active;

  std::mutex mutex;  // protect samples

  // Audio samples received from the client.
  //
  // The I/O threads receive audio samples into this queue
  // and invoke work threads to compute features
  std::deque<std::vector<float>> samples;

#ifdef NEWTRANX_OFFLINE_MODEL
  std::vector<float> pcm_data_left;
  
  int min_speech_duration_ms = 250;
  int min_silence_duration_ms = 500;
  int max_speech_duration_s = 20;
  
  std::vector<float> vad_segments;
  std::vector<float> voice_segments;
  std::vector<float> speaker_segments;
  std::string asr_history; 
  int process_length = 0;
  bool started = false;
  int silent_data_length = 0;
  std::chrono::steady_clock::time_point started_time 
    = std::chrono::steady_clock::time_point::min();
  
  int current_silent_length = 0;
  float rule2_min_trailing_silence = 0.6 * 16000;
  float rule1_min_trailing_silence = 1.4 * 16000;
  int line_switch_timeout = 2.5 * 16000;
  std::string joined_vad_segments;
  std::vector<std::string> line_result;
  std::string final_combined_text;
  std::string line_segment;
  int vad_segment = 1;
  int max_line_result_length = 30;
  int end_region_length = 4;
  int line_finished = 0;
  std::string current_result;
  int max_punct_chunks = 2;
  std::string meeting_id;
  int offset = 0;
  std::vector<float> multi_vad_segment = {};
  std::vector<float> current_samples = {};
  std::vector<float> buffer = {};
  std::vector<std::string> vad_punc_texts;
  std::string tmp_punc;
#endif
  int sample_rate = 16000;
  int vad_window_size = 512;
  std::mutex pcm_data_mutex;
  std::vector<float> pcm_data_receive;
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  SpeakerEmbeddingManager* speaker_manager = nullptr;
  OfflineSpeakerDiarization* speaker_diarization = nullptr;
#endif
  VoiceActivityDetector* vad = nullptr;
  int switch_threshold = 128000; 
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  std::string speaker_id_last;
  float speaker_similarity_threshold = 0.5;
  int speaker_threshold = 16000;
  std::atomic<int> speaker_diarization_enable = 1;
#endif
  int silence_threshold = 32000;

  bool is_android = false;
  ~Connection() {
    if (!is_android) {
      if (vad) {
        delete vad;
        vad = nullptr;
      }
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
      if (speaker_manager) {
        delete speaker_manager;
        speaker_manager = nullptr;
      }
      if (speaker_diarization) {
        delete speaker_diarization;
        speaker_diarization = nullptr;
      }
#endif
    }
  }

  Connection() = default;
  Connection(connection_hdl hdl, std::shared_ptr<OnlineStream> s)
      : hdl(hdl), s(s), last_active(std::chrono::steady_clock::now()) {}
};

struct OnlineWebsocketDecoderConfig {
#ifdef NEWTRANX_OFFLINE_MODEL
  int line_time_threshold = 320000;
  int line_chars_threshold;
  std::string choose_langage;
#endif
  OnlineRecognizerConfig recognizer_config;
  OfflineRecognizerConfig offline_recognizer_config;
  VadModelConfig vad_config;
  OfflinePunctuationModelConfig offline_punctuation_model_config;
  int switch_threshold = 128000;
#if SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION
  std::string speaker_model;
  SpeakerEmbeddingExtractorConfig speaker_extractor_config;
  OfflineSpeakerDiarizationConfig speaker_diarization_config;
  float speaker_similarity_threshold = 0.5f;
  int speaker_threshold = 16000;
  int speaker_recognize_type = 0;
#endif
  int silence_threshold = 32000;
  std::string language_type;
  std::string asr_streaming_type;
  std::string punctuation_server = "";
  std::string save_data_path = "";

  // It determines how often the decoder loop runs.
  int32_t loop_interval_ms = 10;

  int32_t max_batch_size = 5;

  float end_tail_padding = 0.8;

  void Register(ParseOptions *po);
  void Validate() const;
};

class OnlineWebsocketServer;

class OnlineWebsocketDecoder {
 public:
  /**
   * @param server  Not owned.
   */
  explicit OnlineWebsocketDecoder(OnlineWebsocketServer *server);

  ~OnlineWebsocketDecoder();

  std::shared_ptr<Connection> GetOrCreateConnection(connection_hdl hdl);
  void RemoveConnection(connection_hdl hdl);

  // Compute features for a stream given audio samples
  void AcceptWaveform(Connection* c);

  // signal that there will be no more audio samples for a stream
  void InputFinished(Connection* c);

  void Warmup() const;

  void Run();

  void SocketSend (
    Connection* c,
    std::string message);

  void SocketSend (
    Connection* c,
    OnlineRecognizerResult online_result);

 private:
  void ProcessConnections(const asio::error_code &ec);

  /** It is called by one of the worker thread.
   */
  void Decode();

 private:
  OnlineWebsocketServer *server_;  // not owned
  std::unique_ptr<OnlineRecognizer> recognizer_;
  OnlineWebsocketDecoderConfig config_;
  asio::steady_timer timer_;

  // It protects `connections_`, `ready_connections_`, and `active_`
  std::mutex mutex_;

  std::map<connection_hdl, std::shared_ptr<Connection>,
           std::owner_less<connection_hdl>>
      connections_;

  // Whenever a connection has enough feature frames for decoding, we put
  // it in this queue
  std::deque<std::shared_ptr<Connection>> ready_connections_;

  // If we are decoding a stream, we put it in the active_ set so that
  // only one thread can decode a stream at a time.
  std::set<connection_hdl, std::owner_less<connection_hdl>> active_;
};

struct OnlineWebsocketServerConfig {
  OnlineWebsocketDecoderConfig decoder_config;

  std::string log_file = "./ug.log";
  int32_t ping_interval_ms = 30000;
  int32_t pong_timeout_ms = 10000;
  
  void Register(sherpa_onnx::ParseOptions *po);
  void Validate() const;
};

class OnlineWebsocketServer {
 public:
  explicit OnlineWebsocketServer(asio::io_context &io_conn,  // NOLINT
                                 asio::io_context &io_work,  // NOLINT
                                 const OnlineWebsocketServerConfig &config);

  void Run(uint16_t port);
  void BeginRestart();
  size_t ActiveConnections() const;
  void ForceCloseConnections();

  const OnlineWebsocketServerConfig &GetConfig() const { return config_; }
  asio::io_context &GetConnectionContext() { return io_conn_; }
  asio::io_context &GetWorkContext() { return io_work_; }
  server &GetServer() { return server_; }

  void Send(connection_hdl hdl, const std::string &text);

  // Close a websocket connection with given code and reason
  void Close(connection_hdl hdl, websocketpp::close::status::value code,
             const std::string &reason);

  bool Contains(connection_hdl hdl) const;

 private:
  void SetupLog();
  void SchedulePing();
  void OnPongTimeout(connection_hdl hdl);

  // When a websocket client is connected, it will invoke this method
  // (Not for HTTP)
  void OnOpen(connection_hdl hdl);

  // When a websocket client is disconnected, it will invoke this method
  void OnClose(connection_hdl hdl);

  void OnMessage(connection_hdl hdl, server::message_ptr msg);

  void OnMessage_(connection_hdl hdl, server::message_ptr msg);
 private:
  OnlineWebsocketServerConfig config_;
  asio::io_context &io_conn_;
  asio::io_context &io_work_;
  server server_;
  asio::steady_timer ping_timer_;

  OnlineWebsocketDecoder decoder_;

  mutable std::mutex mutex_;

  std::set<connection_hdl, std::owner_less<connection_hdl>> connections_;
  bool restarting_ = false;
  std::map<connection_hdl, std::pair<std::string, bool>,
           std::owner_less<connection_hdl>>
      connection_log_states_;
};

#ifdef NEWTRANX_OFFLINE_MODEL
class StreamRecognizer {
public:
  StreamRecognizer() = default;
  StreamRecognizer(VoiceActivityDetector* vad, 
    OfflineRecognizer* recognizer,
    OfflinePunctuation* punctuation = NULL);
  ~StreamRecognizer();

  int WriteAudio(const float* samples, int32_t n);
  void PutText(std::string text);
  string ReadText();

  void process_voice();

private:
//  /*
  Connection connection;
  std::mutex frame_mutex;
  std::vector<float> frame;

  std::mutex text_mutex;
  std::vector<std::string> recognize_text;
  std::thread process_thread;
  int running;
 // */
};
#endif

}  // namespace sherpa_onnx

#endif  // SHERPA_ONNX_CSRC_ONLINE_WEBSOCKET_SERVER_IMPL_H_
