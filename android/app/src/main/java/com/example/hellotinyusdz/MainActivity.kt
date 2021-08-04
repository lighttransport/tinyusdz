/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package com.example.hellotinyusdz

import android.content.res.AssetManager
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.view.MotionEvent
import android.view.View
import android.widget.ImageView
import android.widget.Toast
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.BitmapFactory
import kotlinx.android.synthetic.main.activity_main.sample_text

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<View>(R.id.container)

        var conf = Bitmap.Config.ARGB_8888
        var b = Bitmap.createBitmap(512, 512, conf)

        var pixels = IntArray(512 * 512)

        //b.getPixels(pixels, 0,512, 0, 0, 512, 512)

        //for (y in 0 until 512) {
        //    for (x in 0 until 512) {
        //    pixels[y * 512 + x] = Color.argb(125, x % 256, y % 256, 64)
        //    }
        //}

        var width = 512
        var height = 512

        updateImage(pixels, width, height)

        b.setPixels(pixels, 0, 512, 0, 0, 512, 512)

        var img = findViewById<ImageView>(R.id.imageView)

        img.setImageBitmap(b)

        // Set up a touch listener which calls the native sound engine
        view.setOnTouchListener {_, event ->
            if (event.action == MotionEvent.ACTION_DOWN) {
                //playSound(true)
            } else if (event.action == MotionEvent.ACTION_UP) {
                //playSound(false)
            } else {
                return@setOnTouchListener false
            }
            true
        }
    }

    override fun onResume() {
        super.onResume()

        var n = createStream(getAssets());

        if (n <= 0) {
            val errorString : String = "Failed to load USD file"
            Toast.makeText(applicationContext, errorString,Toast.LENGTH_LONG).show()
            sample_text.text = errorString
        } else {
            val s : String = "Loaded USD. # of geom_meshes " + n
            Toast.makeText(applicationContext, s,Toast.LENGTH_LONG).show()
            sample_text.text = s

        }
    }

    override fun onPause() {
        super.onPause()
    }


    // Creates and starts Oboe stream to play audio
    private external fun createStream(mgr: AssetManager) : Int
    private external fun updateImage(img: IntArray, width: Int, height: Int) : Int

    companion object {
        // Used to load native code calling oboe on app startup.
        init {
            System.loadLibrary("hello-tinyusdz")
        }
    }
}
