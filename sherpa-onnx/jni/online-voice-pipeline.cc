#include "sherpa-onnx/csrc/offline-punctuation.h"
#include "sherpa-onnx/csrc/voice-activity-detector.h"
#include "sherpa-onnx/csrc/offline-recognizer.h"

#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/jni/common.h"

#include "sherpa-onnx/csrc/online-websocket-server-impl.h"

SHERPA_ONNX_EXTERN_C
JNIEXPORT jlong JNICALL
Java_com_k2fsa_sherpa_onnx_OnlineVoicePipeline_start(JNIEnv *env,
                                            jobject /*obj*/,
                                            jlong vad_ptr, 
                                            jlong recongnizer_ptr,
                                            jlong punctuation_ptr) {


return SafeJNI(
      env, "OnlineVoicePipeline_start",
      [&]() -> jlong {
        if (!ValidatePointer(env, vad_ptr, "OnlineVoicePipeline_start",
                             "VoiceActivityDetector pointer is null.")) {
          return (jlong)(nullptr);
        }
        
        if (!ValidatePointer(env, recongnizer_ptr, "OnlineVoicePipeline_start",
                             "OfflineRecognizer pointer is null.")) {
          return (jlong)(nullptr);
        }

        auto vad = reinterpret_cast<sherpa_onnx::VoiceActivityDetector *>(vad_ptr);
        SHERPA_ONNX_LOGE("%s", vad->GetConfig().ToString().c_str());

        auto recongnizer = reinterpret_cast<sherpa_onnx::OfflineRecognizer *>(recongnizer_ptr);
        SHERPA_ONNX_LOGE("%s", recongnizer->GetConfig().ToString().c_str());

        auto punctuation = reinterpret_cast<sherpa_onnx::OfflinePunctuation *>(punctuation_ptr);

        auto streamer = new sherpa_onnx::StreamRecognizer(vad, recongnizer, punctuation);
        
        return (jlong)streamer;
      }, (jlong)(nullptr));
}

SHERPA_ONNX_EXTERN_C
JNIEXPORT void JNICALL
Java_com_k2fsa_sherpa_onnx_OnlineVoicePipeline_stop(JNIEnv *env,
                                            jobject /*obj*/,
                                            jlong ptr) {
  
  SHERPA_ONNX_LOGE("Java_com_k2fsa_sherpa_onnx_OnlineVoicePipeline_stopStream");
  return SafeJNI(
    env, "OnlineVoicePipeline_stopStream",
    [&]() {
      
      if (!ValidatePointer(env, ptr, "OnlineVoicePipeline_stop",
                            "StreamRecognizer pointer is null.")) {
        return;
      }
      auto recognizer = reinterpret_cast<sherpa_onnx::StreamRecognizer *>(ptr);
      if (recognizer) delete recognizer;
      return ;
    }); 
}

SHERPA_ONNX_EXTERN_C
JNIEXPORT jint JNICALL
Java_com_k2fsa_sherpa_onnx_OnlineVoicePipeline_writeAudio(JNIEnv *env,
                                        jobject obj,
                                        jlong ptr, 
                                        jfloatArray samples) {
  return SafeJNI(
      env, "OnlineVoicePipeline_writeAudio",
      [&]() -> jint {
        
        if (!ValidatePointer(env, ptr, "OnlineVoicePipeline_writeAudio",
                             "StreamRecognizer pointer is null.")) {
          return -1;
        }
        
        sherpa_onnx::StreamRecognizer* recognizer 
          = reinterpret_cast<sherpa_onnx::StreamRecognizer*>(ptr);

        // 把 Java float[] 映射到 native 内存
        jfloat *p = env->GetFloatArrayElements(samples, nullptr);
        jsize n   = env->GetArrayLength(samples);

        //LOGE("**CTOR writeAudio %p", ptr);

        recognizer->WriteAudio((const float*)p, (int32_t)n);
        env->ReleaseFloatArrayElements(samples, p, JNI_ABORT);

        return 0;
      }, -1); 
}

SHERPA_ONNX_EXTERN_C
JNIEXPORT jstring JNICALL
Java_com_k2fsa_sherpa_onnx_OnlineVoicePipeline_readText(JNIEnv *env,
                                      jobject obj,
                                      jlong ptr) {
  
  return SafeJNI(
    env, "OnlineVoicePipeline_readText",
    [&]() -> jstring {
      
      if (!ValidatePointer(env, ptr, "OnlineVoicePipeline_readText",
                            "StreamRecognizer pointer is null.")) {
        return env->NewStringUTF("");
      }
      auto recognizer = reinterpret_cast<sherpa_onnx::StreamRecognizer *>(ptr);
      
      std::string result = recognizer->ReadText();

      //LOGE("**CTOR readText %p", ptr);

      jstring jstr; 
      if (result.empty()) {
        jstr = env->NewStringUTF("");
      } else {
        jstr = env->NewStringUTF(result.data());
      }
      return jstr;

    }, env->NewStringUTF("")); 
}

