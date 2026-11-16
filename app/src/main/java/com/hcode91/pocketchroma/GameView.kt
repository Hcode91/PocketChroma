package com.hcode91.pocketchroma

import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.min

/** Draws the emulated screen and an on-screen controller. */
class GameView(context: Context) : View(context) {

    val frame: Bitmap = Bitmap.createBitmap(320, 240, Bitmap.Config.ARGB_8888)
    var touchButtons = 0
        private set
    var showControls = true
    var onButtonsChanged: (() -> Unit)? = null

    private class Btn(val mask: Int, val label: String, val round: Boolean) { val rect = RectF() }

    private val dpadRect = RectF()
    private val buttons = listOf(
        Btn(Native.A, "A", true), Btn(Native.B, "B", true),
        Btn(Native.L, "L", false), Btn(Native.R, "R", false),
        Btn(Native.MENU, "MENU", false), Btn(Native.PAUSE, "PAUSE", false),
    )
    private val screenRect = RectF()
    private val bmpPaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x40FFFFFF }
    private val pressed = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x907C4DFF.toInt() }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xC0FFFFFF.toInt(); textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD
    }

    init { setBackgroundColor(Color.BLACK) }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        layoutControls(w.toFloat(), h.toFloat())
    }

    private fun layoutControls(w: Float, h: Float) {
        val landscape = w > h
        val unit = min(w, h) / 10f
        text.textSize = unit * 0.38f
        if (landscape) {
            val sh = h
            val sw = min(w, sh * 4f / 3f)
            screenRect.set((w - sw) / 2, (h - sh) / 2, (w + sw) / 2, (h + sh) / 2)
            val d = unit * 3.4f
            dpadRect.set(unit * 0.5f, h - d - unit * 1.2f, unit * 0.5f + d, h - unit * 1.2f)
            val r = unit * 0.85f
            placeRound(buttons[0], w - unit * 1.4f, h - unit * 3.4f, r)
            placeRound(buttons[1], w - unit * 3.1f, h - unit * 2.3f, r)
            buttons[2].rect.set(unit * 0.3f, unit * 0.3f, unit * 2.6f, unit * 1.3f)
            buttons[3].rect.set(w - unit * 2.6f, unit * 0.3f, w - unit * 0.3f, unit * 1.3f)
            buttons[4].rect.set(w - unit * 2.6f, h - unit * 1.0f, w - unit * 1.4f, h - unit * 0.3f)
            buttons[5].rect.set(w - unit * 1.3f, h - unit * 1.0f, w - unit * 0.1f, h - unit * 0.3f)
        } else {
            val sw = w
            val sh = sw * 3f / 4f
            screenRect.set(0f, unit * 0.5f, sw, unit * 0.5f + sh)
            val top = screenRect.bottom
            val area = h - top
            val d = min(unit * 4.2f, area * 0.6f)
            val cy = top + area * 0.55f
            dpadRect.set(unit * 0.5f, cy - d / 2, unit * 0.5f + d, cy + d / 2)
            val r = unit * 1.0f
            placeRound(buttons[0], w - unit * 1.5f, cy - unit * 0.7f, r)
            placeRound(buttons[1], w - unit * 3.7f, cy + unit * 0.7f, r)
            buttons[2].rect.set(unit * 0.3f, top + unit * 0.4f, unit * 2.8f, top + unit * 1.4f)
            buttons[3].rect.set(w - unit * 2.8f, top + unit * 0.4f, w - unit * 0.3f, top + unit * 1.4f)
            buttons[4].rect.set(w / 2 - unit * 2.4f, h - unit * 1.4f, w / 2 - unit * 0.2f, h - unit * 0.6f)
            buttons[5].rect.set(w / 2 + unit * 0.2f, h - unit * 1.4f, w / 2 + unit * 2.4f, h - unit * 0.6f)
        }
    }

    private fun placeRound(b: Btn, cx: Float, cy: Float, r: Float) = b.rect.set(cx - r, cy - r, cx + r, cy + r)

    override fun onDraw(canvas: Canvas) {
        canvas.drawBitmap(frame, null, screenRect, bmpPaint)
        if (!showControls) return

        // D-pad
        val cx = dpadRect.centerX(); val cy = dpadRect.centerY(); val third = dpadRect.width() / 3
        val dirs = listOf(
            Native.UP to RectF(cx - third / 2, dpadRect.top, cx + third / 2, cy - third / 2),
            Native.DOWN to RectF(cx - third / 2, cy + third / 2, cx + third / 2, dpadRect.bottom),
            Native.LEFT to RectF(dpadRect.left, cy - third / 2, cx - third / 2, cy + third / 2),
            Native.RIGHT to RectF(cx + third / 2, cy - third / 2, dpadRect.right, cy + third / 2),
        )
        canvas.drawRect(cx - third / 2, cy - third / 2, cx + third / 2, cy + third / 2, fill)
        for ((mask, r) in dirs) canvas.drawRoundRect(r, 12f, 12f, if (touchButtons and mask != 0) pressed else fill)

        for (b in buttons) {
            val p = if (touchButtons and b.mask != 0) pressed else fill
            if (b.round) canvas.drawOval(b.rect, p) else canvas.drawRoundRect(b.rect, 18f, 18f, p)
            canvas.drawText(b.label, b.rect.centerX(), b.rect.centerY() + text.textSize / 3, text)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (!showControls) return false
        var mask = 0
        val up = e.actionMasked == MotionEvent.ACTION_UP || e.actionMasked == MotionEvent.ACTION_CANCEL
        for (i in 0 until e.pointerCount) {
            if (up) break
            if (e.actionMasked == MotionEvent.ACTION_POINTER_UP && i == e.actionIndex) continue
            mask = mask or hit(e.getX(i), e.getY(i))
        }
        if (mask != touchButtons) {
            if (mask and touchButtons.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            touchButtons = mask
            onButtonsChanged?.invoke()
        }
        return true
    }

    private fun hit(x: Float, y: Float): Int {
        var m = 0
        // generous d-pad zone with diagonals
        val pad = dpadRect.width() * 0.25f
        if (x > dpadRect.left - pad && x < dpadRect.right + pad && y > dpadRect.top - pad && y < dpadRect.bottom + pad) {
            val dx = x - dpadRect.centerX(); val dy = y - dpadRect.centerY()
            val dead = dpadRect.width() * 0.12f
            if (abs(dx) > dead || abs(dy) > dead) {
                if (dx < -dead && abs(dy) < abs(dx) * 2.4f) m = m or Native.LEFT
                if (dx > dead && abs(dy) < abs(dx) * 2.4f) m = m or Native.RIGHT
                if (dy < -dead && abs(dx) < abs(dy) * 2.4f) m = m or Native.UP
                if (dy > dead && abs(dx) < abs(dy) * 2.4f) m = m or Native.DOWN
            }
            return m
        }
        for (b in buttons) {
            val r = RectF(b.rect).apply { inset(-width() * 0.15f, -height() * 0.15f) }
            if (r.contains(x, y)) m = m or b.mask
        }
        return m
    }
}
