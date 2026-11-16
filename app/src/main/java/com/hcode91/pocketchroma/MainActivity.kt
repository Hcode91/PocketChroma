package com.hcode91.pocketchroma

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.*
import androidx.documentfile.provider.DocumentFile

class MainActivity : Activity() {

    private lateinit var status: TextView
    private lateinit var list: LinearLayout
    private val prefs by lazy { getSharedPreferences("pocketchroma", MODE_PRIVATE) }

    companion object {
        private const val REQ_BIOS = 1
        private const val REQ_FOLDER = 2
        private const val REQ_FILE = 3
        private const val ACCENT = 0xFF7C4DFF.toInt()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0xFF121018.toInt())
            setPadding(pad, pad, pad, pad)
        }
        root.addView(TextView(this).apply {
            text = "PocketChroma"
            textSize = 28f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Color.WHITE)
        })
        status = TextView(this).apply {
            setTextColor(0xFFB0A8C8.toInt()); textSize = 14f; setPadding(0, dp(4), 0, dp(12))
        }
        root.addView(status)

        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        buttons.addView(button("Boot ROM") { pick(REQ_BIOS, Intent.ACTION_OPEN_DOCUMENT) }, weighted())
        buttons.addView(button("Games folder") {
            startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_FOLDER)
        }, weighted())
        buttons.addView(button("Open file") { pick(REQ_FILE, Intent.ACTION_OPEN_DOCUMENT) }, weighted())
        root.addView(buttons)

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, dp(12), 0, 0) }
        root.addView(ScrollView(this).apply { addView(list) },
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val bios = Storage.biosFile(this)
        status.text = if (bios.exists()) {
            "Boot ROM: ready" + if (Storage.sha1(bios.readBytes()) != Storage.BIOS_SHA1) " (unrecognised checksum)" else ""
        } else {
            "Boot ROM missing. Tap \"Boot ROM\" and choose your 8 KB ax208.bin dump."
        }

        list.removeAllViews()
        val tree = prefs.getString("folder", null)?.let { Uri.parse(it) } ?: run {
            list.addView(hint("Choose a games folder containing .bin or .zip cart dumps."))
            return
        }
        val dir = DocumentFile.fromTreeUri(this, tree)
        val games = dir?.listFiles()
            ?.filter { f -> f.isFile && f.name?.lowercase()?.let { it.endsWith(".bin") || it.endsWith(".zip") } == true }
            ?.sortedBy { it.name?.lowercase() }
            .orEmpty()
        if (games.isEmpty()) list.addView(hint("No .bin or .zip files found in that folder."))
        games.forEach { f ->
            list.addView(TextView(this).apply {
                text = f.name
                textSize = 17f
                setTextColor(Color.WHITE)
                setPadding(dp(12), dp(14), dp(12), dp(14))
                setBackgroundColor(0xFF1E1A2A.toInt())
                setOnClickListener { launch(f.uri, f.name ?: "game") }
            }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                bottomMargin = dp(6)
            })
        }
    }

    private fun launch(uri: Uri, name: String) {
        if (!Storage.biosFile(this).exists()) {
            Toast.makeText(this, "Set the boot ROM (ax208.bin) first", Toast.LENGTH_LONG).show()
            return
        }
        startActivity(Intent(this, EmulatorActivity::class.java).apply {
            data = uri
            putExtra("name", name)
        })
    }

    private fun pick(req: Int, action: String) {
        startActivityForResult(Intent(action).apply {
            addCategory(Intent.CATEGORY_OPENABLE); type = "*/*"
        }, req)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        when (requestCode) {
            REQ_BIOS -> {
                val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() }
                if (bytes == null || bytes.size != 0x2000) {
                    Toast.makeText(this, "Boot ROM must be exactly 8192 bytes", Toast.LENGTH_LONG).show()
                } else {
                    Storage.biosFile(this).writeBytes(bytes)
                    val ok = Storage.sha1(bytes) == Storage.BIOS_SHA1
                    Toast.makeText(this, if (ok) "Boot ROM verified" else "Boot ROM saved (checksum not recognised)", Toast.LENGTH_LONG).show()
                }
            }
            REQ_FOLDER -> {
                contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                prefs.edit().putString("folder", uri.toString()).apply()
            }
            REQ_FILE -> {
                val name = DocumentFile.fromSingleUri(this, uri)?.name ?: "game"
                launch(uri, name)
            }
        }
        refresh()
    }

    private fun button(label: String, onClick: () -> Unit) = Button(this).apply {
        text = label
        isAllCaps = false
        setTextColor(Color.WHITE)
        setBackgroundColor(ACCENT)
        setOnClickListener { onClick() }
    }

    private fun weighted() = LinearLayout.LayoutParams(0, dp(48), 1f).apply { marginEnd = dp(6) }

    private fun hint(t: String) = TextView(this).apply {
        text = t; setTextColor(0xFF8A82A0.toInt()); gravity = Gravity.CENTER; setPadding(0, dp(24), 0, 0)
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}
