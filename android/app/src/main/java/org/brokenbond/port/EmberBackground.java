package org.brokenbond.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

import java.util.Random;

/**
 * The launcher's background: a dark, warm gradient with a glow behind the
 * logo, embers rising and a few leaves drifting down. Drawn at about 30 frames
 * per second while the launcher is on screen, nothing while it is not.
 */
public class EmberBackground extends View {
    private static final int EMBERS = 34, LEAVES = 7;
    private static final long FRAME_MS = 33;

    private final Random random = new Random(7);
    private final float[] ex = new float[EMBERS], ey = new float[EMBERS], evy = new float[EMBERS],
            esize = new float[EMBERS], ephase = new float[EMBERS];
    private final float[] lx = new float[LEAVES], ly = new float[LEAVES], lvx = new float[LEAVES],
            lvy = new float[LEAVES], lrot = new float[LEAVES], lspin = new float[LEAVES], lsize = new float[LEAVES];
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), glow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ember = new Paint(Paint.ANTI_ALIAS_FLAG), leaf = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path leafPath = new Path();
    private long last;
    private boolean running;

    public EmberBackground(Context context) {
        this(context, null);
    }

    public EmberBackground(Context context, AttributeSet attrs) {
        super(context, attrs);
        leaf.setStyle(Paint.Style.FILL);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        fill.setShader(new LinearGradient(0, 0, w * 0.35f, h, 0xFF1C0D08, 0xFF0C0806, Shader.TileMode.CLAMP));
        glow.setShader(new RadialGradient(w * 0.26f, h * 0.42f, Math.max(1f, h * 0.75f),
                new int[] {0x66E0501E, 0x22B8350F, 0x00000000}, new float[] {0f, 0.45f, 1f}, Shader.TileMode.CLAMP));
        for (int i = 0; i < EMBERS; ++i) spawnEmber(i, w, h, true);
        for (int i = 0; i < LEAVES; ++i) spawnLeaf(i, w, h, true);
        // An almond shape, its length 1 along x, for the leaves.
        leafPath.reset();
        leafPath.moveTo(-0.5f, 0f);
        leafPath.quadTo(0f, -0.32f, 0.5f, 0f);
        leafPath.quadTo(0f, 0.32f, -0.5f, 0f);
        leafPath.close();
    }

    private void spawnEmber(int i, int w, int h, boolean anywhere) {
        float density = getResources().getDisplayMetrics().density;
        ex[i] = random.nextFloat() * w;
        ey[i] = anywhere ? random.nextFloat() * h : h + random.nextFloat() * 40 * density;
        evy[i] = (14 + random.nextFloat() * 30) * density;  // Upward, per second.
        esize[i] = (1.2f + random.nextFloat() * 2.2f) * density;
        ephase[i] = random.nextFloat() * 6.28f;
    }

    private void spawnLeaf(int i, int w, int h, boolean anywhere) {
        float density = getResources().getDisplayMetrics().density;
        lx[i] = random.nextFloat() * w;
        ly[i] = anywhere ? random.nextFloat() * h : -30 * density;
        lvx[i] = (-12 + random.nextFloat() * 24) * density;
        lvy[i] = (10 + random.nextFloat() * 16) * density;
        lrot[i] = random.nextFloat() * 360;
        lspin[i] = -40 + random.nextFloat() * 80;
        lsize[i] = (9 + random.nextFloat() * 9) * density;
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        running = true;
        last = SystemClock.uptimeMillis();
        postInvalidateOnAnimation();
    }

    @Override
    protected void onDetachedFromWindow() {
        running = false;
        super.onDetachedFromWindow();
    }

    @Override
    protected void onWindowVisibilityChanged(int visibility) {
        super.onWindowVisibilityChanged(visibility);
        boolean visible = visibility == VISIBLE;
        if (visible && !running) {
            running = true;
            last = SystemClock.uptimeMillis();
            postInvalidateOnAnimation();
        } else if (!visible) {
            running = false;
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        int w = getWidth(), h = getHeight();
        canvas.drawRect(0, 0, w, h, fill);
        canvas.drawRect(0, 0, w, h, glow);
        long now = SystemClock.uptimeMillis();
        float dt = Math.min(0.1f, (now - last) / 1000f);
        last = now;
        float t = now / 1000f;
        for (int i = 0; i < EMBERS; ++i) {
            ey[i] -= evy[i] * dt;
            ex[i] += (float) Math.sin(t * 0.9f + ephase[i]) * 6 * dt * getResources().getDisplayMetrics().density;
            if (ey[i] < -10) spawnEmber(i, w, h, false);
            float flicker = 0.55f + 0.45f * (float) Math.sin(t * 3.1f + ephase[i]);
            float fade = Math.min(1f, ey[i] / (h * 0.35f));  // Embers fade out near the top.
            int alpha = Math.round(255 * Math.max(0f, flicker * fade));
            ember.setColor(0xFFFF9A3C);
            ember.setAlpha(alpha / 5);
            canvas.drawCircle(ex[i], ey[i], esize[i] * 3.2f, ember);
            ember.setColor(0xFFFFC266);
            ember.setAlpha(alpha);
            canvas.drawCircle(ex[i], ey[i], esize[i], ember);
        }
        for (int i = 0; i < LEAVES; ++i) {
            lx[i] += (lvx[i] + (float) Math.sin(t * 0.7f + i) * 10) * dt;
            ly[i] += lvy[i] * dt;
            lrot[i] += lspin[i] * dt;
            if (ly[i] > h + 30 || lx[i] < -40 || lx[i] > w + 40) spawnLeaf(i, w, h, false);
            canvas.save();
            canvas.translate(lx[i], ly[i]);
            canvas.rotate(lrot[i]);
            canvas.scale(lsize[i], lsize[i]);
            leaf.setColor(i % 3 == 0 ? 0x664E7A2E : 0x553C5F22);
            canvas.drawPath(leafPath, leaf);
            canvas.restore();
        }
        if (running) postInvalidateDelayed(FRAME_MS);
    }
}
