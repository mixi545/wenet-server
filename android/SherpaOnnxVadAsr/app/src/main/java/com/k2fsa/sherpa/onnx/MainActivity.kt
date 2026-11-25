package com.k2fsa.sherpa.onnx.vad.asr

import android.Manifest
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.util.Log
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import com.k2fsa.sherpa.onnx.OfflineRecognizer
import com.k2fsa.sherpa.onnx.OfflineRecognizerConfig
import com.k2fsa.sherpa.onnx.R
import com.k2fsa.sherpa.onnx.Vad
import com.k2fsa.sherpa.onnx.getFeatureConfig
import com.k2fsa.sherpa.onnx.getOfflineModelConfig
import com.k2fsa.sherpa.onnx.getVadModelConfig
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.concurrent.thread

import com.k2fsa.sherpa.onnx.OfflinePunctuation
import com.k2fsa.sherpa.onnx.OfflinePunctuationConfig
import com.k2fsa.sherpa.onnx.OfflinePunctuationModelConfig
import com.k2fsa.sherpa.onnx.OnlineVoicePipeline

private const val TAG = "sherpa-onnx"
private const val REQUEST_RECORD_AUDIO_PERMISSION = 200

class MainActivity : AppCompatActivity() {

    private lateinit var onlineVoicePipeline: OnlineVoicePipeline  // added by yangxiaoming
    private lateinit var offlinePunctuation: OfflinePunctuation  // added by yangxiaoming

    private lateinit var recordButton: Button
    private lateinit var textView: TextView

    private lateinit var vad: Vad

    private var audioRecord: AudioRecord? = null
    private var recordingThread: Thread? = null
    private val audioSource = MediaRecorder.AudioSource.MIC
    private val sampleRateInHz = 16000
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO

    // Note: We don't use AudioFormat.ENCODING_PCM_FLOAT
    // since the AudioRecord.read(float[]) needs API level >= 23
    // but we are targeting API level >= 21
    private val audioFormat = AudioFormat.ENCODING_PCM_16BIT

    private val permissions: Array<String> = arrayOf(Manifest.permission.RECORD_AUDIO)

    // Non-streaming ASR
    private lateinit var offlineRecognizer: OfflineRecognizer

    private var idx: Int = 0
    private var lastText: String = ""

    @Volatile
    private var isRecording: Boolean = false

    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<String>, grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        val permissionToRecordAccepted = if (requestCode == REQUEST_RECORD_AUDIO_PERMISSION) {
            grantResults[0] == PackageManager.PERMISSION_GRANTED
        } else {
            false
        }

        if (!permissionToRecordAccepted) {
            Log.e(TAG, "Audio record is disallowed")
            finish()
        }

        Log.i(TAG, "Audio record is permitted")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        ActivityCompat.requestPermissions(this, permissions, REQUEST_RECORD_AUDIO_PERMISSION)

        Log.i(TAG, "Start to initialize model")
        initVadModel()
        Log.i(TAG, "Finished initializing model")

        Log.i(TAG, "Start to initialize non-streaimng recognizer")
        initOfflineRecognizer()
        Log.i(TAG, "Finished initializing non-streaming recognizer")

        onlineVoicePipeline = OnlineVoicePipeline(vad, offlineRecognizer, offlinePunctuation)  // added by yangxiaoming

        recordButton = findViewById(R.id.record_button)
        recordButton.setOnClickListener { onclick() }

        textView = findViewById(R.id.my_text)
        textView.movementMethod = ScrollingMovementMethod()
    }

    private fun onclick() {
        if (!isRecording) {
            val ret = initMicrophone()
            if (!ret) {
                Log.e(TAG, "Failed to initialize microphone")
                return
            }
            Log.i(TAG, "state: ${audioRecord?.state}")
            audioRecord!!.startRecording()
            recordButton.setText(R.string.stop)
            isRecording = true

            textView.text = ""
            lastText = ""
            idx = 0

            vad.reset()
            recordingThread = thread(true) {
                processSamples()
            }
            Log.i(TAG, "Started recording")
        } else {
            isRecording = false

            audioRecord!!.stop()
            audioRecord!!.release()
            audioRecord = null

            recordButton.setText(R.string.start)
            Log.i(TAG, "Stopped recording")
        }
    }

    private  fun initVadModel() {
        val type = 0
        Log.i(TAG, "Select VAD model type ${type}")
        val config = getVadModelConfig(type)
        vad = Vad(
            assetManager = application.assets,
            config = config!!,
        )
    }

    private fun initMicrophone(): Boolean {
        if (ActivityCompat.checkSelfPermission(
                this, Manifest.permission.RECORD_AUDIO
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            ActivityCompat.requestPermissions(this, permissions, REQUEST_RECORD_AUDIO_PERMISSION)
            return false
        }

        val numBytes = AudioRecord.getMinBufferSize(sampleRateInHz, channelConfig, audioFormat)
        Log.i(
            TAG, "buffer size in milliseconds: ${numBytes * 1000.0f / sampleRateInHz}"
        )

        audioRecord = AudioRecord(
            audioSource,
            sampleRateInHz,
            channelConfig,
            audioFormat,
            numBytes * 2 // a sample has two bytes as we are using 16-bit PCM
        )
        return true
    }

    private fun processSamples() {
        Log.i(TAG, "processing samples")

        val bufferSize = 512 // in samples
        val buffer = ShortArray(bufferSize)
        val coroutineScope = CoroutineScope(Dispatchers.IO)


        while (isRecording) {
            val ret = audioRecord?.read(buffer, 0, buffer.size)
            if (ret != null && ret > 0) {
                val samples = FloatArray(ret) { buffer[it] / 32768.0f }

/*
                vad.acceptWaveform(samples)
                while(!vad.empty()) {
                    var segment = vad.front()
                    coroutineScope.launch {
                        val text = runSecondPass(segment.samples)
                        if (text.isNotBlank()) {
                            withContext(Dispatchers.Main) {
                                lastText = "${lastText}\n${idx}: ${text}"
                                idx += 1
                                textView.text = lastText.lowercase()
                            }
                        }
                    }

                    vad.pop();
                }


                val isSpeechDetected = vad.isSpeechDetected()
// */

// /*

                coroutineScope.launch {
                    onlineVoicePipeline.writeAudio(samples)
                    val (text, is_final) = onlineVoicePipeline.readText()
                    if (text.isNotBlank()) {
                        withContext(Dispatchers.Main) {
                            if (is_final){
                                lastText = "${lastText}\n${idx}: ${text}"
                                idx += 1
                                textView.text = lastText.lowercase()
                            } else {
                                textView.text = lastText + "\n${idx}: ${text}"
                            }
                        }
                    }

                }  // added by yangxiaoming
 // */
            }
        }

        // Clean up the coroutine scope when done
        coroutineScope.cancel()
    }

    private fun initOfflineRecognizer() {
        // Please change getOfflineModelConfig() to add new models
        // See https://k2-fsa.github.io/sherpa/onnx/pretrained_models/index.html
        // for a list of available models
        val asrModelType = 15
        val asrRuleFsts: String?
        asrRuleFsts = null
        Log.i(TAG, "Select model type ${asrModelType} for ASR")

        val config = OfflineRecognizerConfig(
            featConfig = getFeatureConfig(sampleRate = sampleRateInHz, featureDim = 80),
            modelConfig = getOfflineModelConfig(type = asrModelType)!!,
        )
        if (asrRuleFsts != null) {
            config.ruleFsts = asrRuleFsts;
        }

        offlineRecognizer = OfflineRecognizer(
            assetManager = application.assets,
            config = config,
        )

        // /*
        var punc_model_config = OfflinePunctuationModelConfig("punc_model.onnx")
        var punc_config = OfflinePunctuationConfig(punc_model_config)
        offlinePunctuation = OfflinePunctuation(
            assetManager = application.assets,
            config = punc_config)  // added by yangxiaoming
        // */

    }

    private fun runSecondPass(samples: FloatArray): String {
        val stream = offlineRecognizer.createStream()
        stream.acceptWaveform(samples, sampleRateInHz)
        offlineRecognizer.decode(stream)
        val result = offlineRecognizer.getResult(stream)
        stream.release()
        return result.text
    }

    private fun appendText(text: String) {
        // 1. 追加内容
        textView.append("$text")
        // 2. 自动滚到底
        textView.post {
            val scrollAmount = textView.layout.getLineTop(textView.lineCount) - textView.height
            textView.scrollTo(0, maxOf(scrollAmount, 0))   // 瞬间底部
            // textView.smoothScrollTo(0, maxOf(scrollAmount, 0))  // 或平滑滚动
        }
    }

    private fun appendLine(text: String) {
        // 1. 追加内容
        textView.append("\n$text")
        // 2. 自动滚到底
        textView.post {
            val scrollAmount = textView.layout.getLineTop(textView.lineCount) - textView.height
            textView.scrollTo(0, maxOf(scrollAmount, 0))   // 瞬间底部
            // textView.smoothScrollTo(0, maxOf(scrollAmount, 0))  // 或平滑滚动
        }
    }

    private fun textDisplay(text: String){
        if (text.isNotBlank()) {
            lastText = "${idx}: ${text}"
            //textView.text = lastText.lowercase()
            appendLine(lastText)
            idx += 1
        }
    }
}
