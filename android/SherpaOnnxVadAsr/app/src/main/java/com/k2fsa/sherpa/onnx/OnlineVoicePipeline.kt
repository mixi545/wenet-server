package com.k2fsa.sherpa.onnx

import android.content.res.AssetManager

import com.k2fsa.sherpa.onnx.OfflineRecognizer
import com.k2fsa.sherpa.onnx.OfflineRecognizerConfig
import com.k2fsa.sherpa.onnx.getOfflineModelConfig
import com.k2fsa.sherpa.onnx.getVadModelConfig

import kotlinx.coroutines.*
import kotlinx.coroutines.sync.Mutex

import android.util.Log
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.google.gson.JsonParser


private const val TAG = "Pipeline"

class OnlineVoicePipeline(vad: Vad,
                          recognizer : OfflineRecognizer,
                          punctuation: OfflinePunctuation){

    private var ptr : Long

    init {
        ptr = start(vad.getPtr(),
            recognizer.getPtr(),
            punctuation.getPtr());
    }

    /*
    fun startStream(vad: Vad, recognizer : OfflineRecognizer) {

        ptrStream = startStream(vad.getPtr(), recognizer.getPtr());
    }
*/
    private external fun start(
        vad_ptr: Long, recognizer_ptr : Long, punctuation_ptr : Long
    ) : Long

    fun stop() = stop(ptr)
    private external fun stop(ptr: Long?)
/*
    fun writeAudio(data: FloatArray) {
        writeAudio(ptrStream, data)
    }


 // */

// /*
    suspend fun writeAudio(data: FloatArray) {
        //Log.i(TAG, "writeAudio")
        writeAudio(ptr, data)
    }
// */
    /*
    suspend fun writeAudio(data: FloatArray) = coroutineScope {
        val writer = async(Dispatchers.IO) { writeAudio(ptrStream, data) }
        writer.await()
    }
//    */
    private external fun writeAudio(ptr: Long, data: FloatArray) : Int
/*
    fun readText(): String {
        return readText(ptrStream)
    }

 // */
// /*
    suspend fun readText(): Pair<String, Boolean> {
        var result = readText(ptr)

        if (result.isNotBlank()) {
            val element: JsonElement = JsonParser.parseString(result)
            val obj: JsonObject = element.asJsonObject

            return Pair(obj.get("text").asString, obj.get("is_final").asBoolean)
        }
        return Pair("", false)
        //return (readText(ptr), false)
    }
 // */
 /*
    suspend fun readText(data: FloatArray) : String = coroutineScope {
        val reader = async(Dispatchers.IO) { readText(ptrStream) }
        reader.await()
    }
 //   */
    private external fun readText(ptr: Long) : String

    companion object {
        init {
            System.loadLibrary("sherpa-onnx-jni")
        }
    }
}

