package com.hcode91.pocketchroma

import android.app.Activity
import android.app.AlertDialog
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Bundle
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import android.widget.Toast

class EmulatorActivity : Activity() {

    private lateinit var view: GameView
    private lateinit var gameId: String
    private var thread: Thread? = null
    @Volatile private var running = false
    @Volatile private var paused = false
    @Volatile private var fastForward = false
    @Volatile private var padButtons = 0
    private var audio: AudioTrack? = null
    private val lock = Any()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        view = GameView(this)
        view.onButtonsChanged = { pushButtons() }
        setContentView(view)
        hideSystemUi()

        val uri = intent.data ?: return finish()
        gameId = Storage.gameId(intent.getStringExtra("name") ?: "game")

        Native.create()
        val bios = Storage.biosFile(this)
        if (!bios.exists() || !Native.loadBios(bios.readBytes())) return fail("Boot ROM missing or invalid")
        val cart = Storage.readCart(this, uri) ?: return fail("Couldn't read the cart file")
        if (!Native.loadCart(cart)) return fail("Not a valid cart dump (expected a power-of-two size, e.g. 16 MB)")
        val save = Storage.saveFile(this, gameId)
        if (save.exists()) Native.loadSave(save.readBytes())
        Native.reset()
    }

    private fun fail(msg: String) {
        Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
        finish()
    }

    override fun onResume() {
        super.onResume()
        hideSystemUi()
        start()
    }

    override fun onPause() {
        stop()
        flushSave()
        super.onPause()
    }

    private fun start() {
        if (running || isFinishing) return
        val rate = 32000
        val minBuf = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
        audio = try {
            AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(rate)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(maxOf(minBuf, rate / 60 * 2 * 4))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build().also { it.play() }
        } catch (e: Exception) { null }

        running = true
        thread = Thread({ loop() }, "emu").apply { priority = Thread.MAX_PRIORITY; start() }
    }

    private fun stop() {
        running = false
        thread?.join(1000)
        thread = null
        audio?.run { try { stop() } catch (_: Exception) {}; release() }
        audio = null
    }

    private fun loop() {
        val samples = ShortArray(4096)
        val frameNs = 1_000_000_000L / 60
        var next = SystemClock.elapsedRealtimeNanos()
        var saveCheck = 0
        while (running) {
            if (paused) { Thread.sleep(16); next = SystemClock.elapsedRealtimeNanos(); continue }
            val frames = if (fastForward) 4 else 1
            var n = 0
            synchronized(lock) {
                repeat(frames) { n = Native.runFrame(view.frame, samples) }
            }
            view.postInvalidate()

            val track = audio
            if (!fastForward && track != null && n > 0) {
                track.write(samples, 0, n) // blocking write paces emulation to the audio clock
            } else {
                next += frameNs
                val wait = next - SystemClock.elapsedRealtimeNanos()
                if (wait > 0) Thread.sleep(wait / 1_000_000, (wait % 1_000_000).toInt())
                else if (wait < -frameNs * 4) next = SystemClock.elapsedRealtimeNanos()
            }

            if (++saveCheck >= 120) { saveCheck = 0; flushSave() }
        }
    }

    private fun flushSave() {
        val data = synchronized(lock) { Native.takeSaveIfDirty() } ?: return
        try { Storage.saveFile(this, gameId).writeBytes(data) } catch (_: Exception) {}
    }

    private fun pushButtons() = Native.setButtons(view.touchButtons or padButtons)

    // --- physical controllers / keyboards ---
    private fun keyMask(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> Native.UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> Native.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> Native.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> Native.RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_K, KeyEvent.KEYCODE_ENTER -> Native.A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_J -> Native.B
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> Native.L
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> Native.R
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_TAB -> Native.MENU
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_SPACE -> Native.PAUSE
        else -> 0
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_BUTTON_MODE) {
            if (event.action == KeyEvent.ACTION_UP) showMenu()
            return true
        }
        val m = keyMask(event.keyCode)
        if (m == 0) return super.dispatchKeyEvent(event)
        padButtons = if (event.action == KeyEvent.ACTION_DOWN) padButtons or m else padButtons and m.inv()
        // hide touch controls once a controller is in use
        if (view.showControls && event.source and android.view.InputDevice.SOURCE_GAMEPAD != 0) {
            view.showControls = false; view.invalidate()
        }
        pushButtons()
        return true
    }

    private fun showMenu() {
        paused = true
        val items = arrayOf(
            "Resume", "Save state (slot 1)", "Load state (slot 1)",
            if (fastForward) "Fast-forward: ON" else "Fast-forward: OFF",
            if (view.showControls) "Hide touch controls" else "Show touch controls",
            "Reset", "Quit"
        )
        AlertDialog.Builder(this)
            .setTitle("PocketChroma")
            .setItems(items) { _, i ->
                when (i) {
                    1 -> {
                        val s = synchronized(lock) { Native.saveState() }
                        if (s != null) { Storage.stateFile(this, gameId, 1).writeBytes(s); toast("State saved") }
                    }
                    2 -> {
                        val f = Storage.stateFile(this, gameId, 1)
                        val ok = f.exists() && synchronized(lock) { Native.loadState(f.readBytes()) }
                        toast(if (ok) "State loaded" else "No state in slot 1")
                    }
                    3 -> fastForward = !fastForward
                    4 -> { view.showControls = !view.showControls; view.invalidate() }
                    5 -> synchronized(lock) { Native.reset() }
                    6 -> { flushSave(); finish() }
                }
            }
            .setOnDismissListener { paused = false; hideSystemUi() }
            .show()
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_SHORT).show()

    @Suppress("DEPRECATION")
    private fun hideSystemUi() {
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE)
    }
}
