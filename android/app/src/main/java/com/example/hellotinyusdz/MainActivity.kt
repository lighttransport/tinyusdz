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

    val render_width = 512;
    val render_height = 512;

    fun updateRender() {
        renderImage(render_width, render_height);

        var conf = Bitmap.Config.ARGB_8888
        var b = Bitmap.createBitmap(render_width, render_height, conf)

        var pixels = IntArray(render_width * render_height)

        grabImage(pixels, render_width, render_height)

        b.setPixels(pixels, 0, render_width, 0, 0, render_width, render_height)

        var img = findViewById<ImageView>(R.id.imageView)

        img.setImageBitmap(b)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<View>(R.id.container)


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

        var n = initScene(getAssets(), "suzanne.usdc");

        if (n <= 0) {
            val errorString : String = "Failed to load USD file"
            Toast.makeText(applicationContext, errorString,Toast.LENGTH_LONG).show()
            sample_text.text = errorString
        } else {
            val s : String = "Loaded USD. # of geom_meshes " + n
            Toast.makeText(applicationContext, s,Toast.LENGTH_LONG).show()
            sample_text.text = s

            updateRender()
        }
    }

    override fun onPause() {
        super.onPause()
    }


    private external fun initScene(mgr: AssetManager, filename: String) : Int
    private external fun renderImage(width: Int, height: Int) : Int
    private external fun grabImage(img: IntArray, width: Int, height: Int) : Int

    companion object {
        // Used to load native code calling oboe on app startup.
        init {
            System.loadLibrary("hello-tinyusdz")
        }
    }
}
