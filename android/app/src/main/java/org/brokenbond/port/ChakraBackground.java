package org.brokenbond.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

import java.util.Random;

/**
 * The launcher's background, after the game's two sides: a night of ink with
 * an orange chakra glowing on the left and a blue one on the right, each made
 * of soft clouds that drift and breathe, and specks of chakra rising between
 * them. Drawn at about 30 frames per second while the launcher is on screen.
 */
public class ChakraBackground extends View {
    private static final int CLOUDS = 10, SPECKS = 26;
    private static final long FRAME_MS = 33;
    private static final int ORANGE = 0xFFF28C28, BLUE = 0xFF3D7BFF;

    private final Random random = new Random(11);
    // The clouds: center (as a fraction of the size), drift radius, phase, size, side (0 orange, 1 blue).
    private final float[] cx = new float[CLOUDS], cy = new float[CLOUDS], drift = new float[CLOUDS],
            phase = new float[CLOUDS], size = new float[CLOUDS];
    private final int[] side = new int[CLOUDS];
    private final float[] sx = new float[SPECKS], sy = new float[SPECKS], svy = new float[SPECKS],
            sr = new float[SPECKS], sphase = new float[SPECKS];
    private final int[] sside = new int[SPECKS];
    private final Paint sky = new Paint(), cloud = new Paint(Paint.ANTI_ALIAS_FLAG), speck = new Paint(Paint.ANTI_ALIAS_FLAG);
    private long last;
    private boolean running;

    public ChakraBackground(Context context) {
        this(context, null);
    }

    public ChakraBackground(Context context, AttributeSet attrs) {
        super(context, attrs);
        for (int i = 0; i < CLOUDS; ++i) {
            side[i] = i % 2;
            cx[i] = side[i] == 0 ? 0.08f + random.nextFloat() * 0.34f : 0.58f + random.nextFloat() * 0.38f;
            cy[i] = 0.15f + random.nextFloat() * 0.75f;
            drift[i] = 0.02f + random.nextFloat() * 0.05f;
            phase[i] = random.nextFloat() * 6.28f;
            size[i] = 0.22f + random.nextFloat() * 0.22f;
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        sky.setShader(new LinearGradient(0, 0, 0, h, 0xFF0E1320, 0xFF07070B, Shader.TileMode.CLAMP));
        for (int i = 0; i < SPECKS; ++i) spawnSpeck(i, w, h, true);
    }

    private void spawnSpeck(int i, int w, int h, boolean anywhere) {
        float density = getResources().getDisplayMetrics().density;
        sside[i] = random.nextInt(2);
        sx[i] = (sside[i] == 0 ? random.nextFloat() * 0.5f : 0.5f + random.nextFloat() * 0.5f) * w;
        sy[i] = anywhere ? random.nextFloat() * h : h + random.nextFloat() * 30 * density;
        svy[i] = (10 + random.nextFloat() * 22) * density;
        sr[i] = (1f + random.nextFloat() * 1.8f) * density;
        sphase[i] = random.nextFloat() * 6.28f;
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        start();
    }

    @Override
    protected void onDetachedFromWindow() {
        running = false;
        super.onDetachedFromWindow();
    }

    @Override
    protected void onWindowVisibilityChanged(int visibility) {
        super.onWindowVisibilityChanged(visibility);
        if (visibility == VISIBLE) start();
        else running = false;
    }

    private void start() {
        if (running) return;
        running = true;
        last = SystemClock.uptimeMillis();
        postInvalidateOnAnimation();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;
        canvas.drawRect(0, 0, w, h, sky);
        long now = SystemClock.uptimeMillis();
        float dt = Math.min(0.1f, (now - last) / 1000f), t = now / 1000f;
        last = now;
        for (int i = 0; i < CLOUDS; ++i) {
            float x = (cx[i] + drift[i] * (float) Math.sin(t * 0.21f + phase[i])) * w;
            float y = (cy[i] + drift[i] * (float) Math.cos(t * 0.17f + phase[i] * 1.3f)) * h;
            float breath = 0.75f + 0.25f * (float) Math.sin(t * 0.6f + phase[i]);
            float radius = size[i] * h * (0.9f + 0.2f * breath);
            int color = side[i] == 0 ? ORANGE : BLUE;
            int alpha = Math.round(70 * breath);
            cloud.setShader(new RadialGradient(x, y, Math.max(1f, radius),
                    new int[] {(alpha << 24) | (color & 0xFFFFFF), ((alpha / 3) << 24) | (color & 0xFFFFFF), 0},
                    new float[] {0f, 0.5f, 1f}, Shader.TileMode.CLAMP));
            canvas.drawCircle(x, y, radius, cloud);
        }
        float density = getResources().getDisplayMetrics().density;
        for (int i = 0; i < SPECKS; ++i) {
            sy[i] -= svy[i] * dt;
            sx[i] += (float) Math.sin(t * 1.1f + sphase[i]) * 8 * density * dt;
            if (sy[i] < -8) spawnSpeck(i, w, h, false);
            float glow = 0.5f + 0.5f * (float) Math.sin(t * 2.4f + sphase[i]);
            int base = sside[i] == 0 ? 0xFFFFB25C : 0xFF8FB6FF;
            speck.setColor(base);
            speck.setAlpha(Math.round(40 * glow));
            canvas.drawCircle(sx[i], sy[i], sr[i] * 3.5f, speck);
            speck.setAlpha(Math.round(110 + 145 * glow));
            canvas.drawCircle(sx[i], sy[i], sr[i], speck);
        }
        if (running) postInvalidateDelayed(FRAME_MS);
    }
}
