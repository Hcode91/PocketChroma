package com.hcode91.pocketchroma

import android.content.Context
import android.net.Uri
import java.io.File
import java.security.MessageDigest
import java.util.zip.ZipInputStream

object Storage {
    // SHA-1 of the known-good AX208 internal ROM (as catalogued by MAME)
    const val BIOS_SHA1 = "0dc7ab9bdaf73231d4d6627fe6308fe8103e1bbc"

    fun biosFile(ctx: Context) = File(ctx.filesDir, "ax208.bin")
    fun saveFile(ctx: Context, game: String) = File(File(ctx.filesDir, "saves").apply { mkdirs() }, "$game.sav")
    fun stateFile(ctx: Context, game: String, slot: Int) = File(File(ctx.filesDir, "states").apply { mkdirs() }, "$game.st$slot")

    fun sha1(data: ByteArray): String =
        MessageDigest.getInstance("SHA-1").digest(data).joinToString("") { "%02x".format(it) }

    /** Reads a cart from a .bin or from the first .bin inside a .zip. */
    fun readCart(ctx: Context, uri: Uri): ByteArray? {
        val raw = ctx.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return null
        if (raw.size >= 4 && raw[0] == 'P'.code.toByte() && raw[1] == 'K'.code.toByte()) {
            ZipInputStream(raw.inputStream()).use { zip ->
                while (true) {
                    val e = zip.nextEntry ?: break
                    if (!e.isDirectory && e.name.lowercase().endsWith(".bin")) return zip.readBytes()
                }
            }
            return null
        }
        return raw
    }

    fun gameId(name: String): String = name.substringBeforeLast('.').replace(Regex("[^A-Za-z0-9._-]"), "_")
}
