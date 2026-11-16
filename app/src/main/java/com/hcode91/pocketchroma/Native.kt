package com.hcode91.pocketchroma

import android.graphics.Bitmap

object Native {
    init { System.loadLibrary("pocketchroma") }

    const val UP = 1 shl 0
    const val DOWN = 1 shl 1
    const val LEFT = 1 shl 2
    const val RIGHT = 1 shl 3
    const val A = 1 shl 4
    const val B = 1 shl 5
    const val L = 1 shl 6
    const val R = 1 shl 7
    const val MENU = 1 shl 8
    const val PAUSE = 1 shl 9

    @JvmStatic external fun create()
    @JvmStatic external fun loadBios(data: ByteArray): Boolean
    @JvmStatic external fun loadCart(data: ByteArray): Boolean
    @JvmStatic external fun loadSave(data: ByteArray): Boolean
    @JvmStatic external fun reset()
    @JvmStatic external fun setButtons(buttons: Int)
    @JvmStatic external fun runFrame(bitmap: Bitmap, audio: ShortArray): Int
    @JvmStatic external fun takeSaveIfDirty(): ByteArray?
    @JvmStatic external fun saveState(): ByteArray?
    @JvmStatic external fun loadState(data: ByteArray): Boolean
}
