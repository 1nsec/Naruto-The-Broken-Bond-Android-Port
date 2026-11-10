package org.brokenbond.port;

import android.animation.ObjectAnimator;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.Random;

/**
 * The launcher's effects, drawn over everything and never taking touches:
 * puffs of smoke, like a ninja vanishing, when a choice or a shortcut is
 * tapped; and for Play, kunai and shuriken thrown at the button from around
 * the screen, which stick in it and shake it. Drawn only while one is alive.
 */
public class EffectsView extends View {
    private static final int CLOUDS = 18;
    private static final long FLIGHT_MS = 380, STAY_MS = 300, FADE_MS = 200, SPARK_MS = 160;

    private static final class Cloud {
        float x, y, dx, dy, r0, r1, angle, spin;
        long born, life;
    }

    private static final class Thrown {
        boolean kunai;
        float sx, sy, tx, ty, heading, spin;
        long start;
    }

    private final ArrayList<Cloud> clouds = new ArrayList<>();
    private final ArrayList<Thrown> thrown = new ArrayList<>();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint spark = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF bounds = new RectF();
    private final Random random = new Random();
    private final float density;
    private Bitmap cloud, kunai, shuriken;

    public EffectsView(Context context) {
        this(context, null);
    }

    public EffectsView(Context context, AttributeSet attrs) {
        super(context, attrs);
        density = getResources().getDisplayMetrics().density;
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        spark.setStrokeCap(Paint.Cap.ROUND);
        spark.setStrokeWidth(2.2f * density);
    }

    // --- Smoke --------------------------------------------------------------------------------

    /** A puff over a view of the launcher, sized after it. */
    void puff(View around) {
        float[] center = centerOf(around);
        float size = Math.max(around.getWidth(), around.getHeight()) * 0.55f;
        puff(center[0], center[1], Math.max(30 * density, Math.min(72 * density, size)));
    }

    void puff(float x, float y, float size) {
        if (cloud == null) cloud = makeCloud(Math.round(96 * density));
        long now = SystemClock.uptimeMillis();
        // The burst at the center, under the rest: it swells and clears first.
        addCloud(x, y, 0, 0, size * 0.6f, size * 1.5f, now, 420);
        for (int i = 0; i < CLOUDS; ++i) {
            double angle = (i + random.nextFloat() * 0.7) * 2 * Math.PI / CLOUDS;
            float distance = size * (0.85f + random.nextFloat() * 0.8f);
            // Wider than tall, like the choices, and drifting up.
            addCloud(x, y, (float) Math.cos(angle) * distance * 1.25f,
                    (float) Math.sin(angle) * distance * 0.7f - size * 0.2f,
                    size * (0.28f + random.nextFloat() * 0.18f), size * (0.62f + random.nextFloat() * 0.35f),
                    now + random.nextInt(45), 560 + random.nextInt(300));
        }
        postInvalidateOnAnimation();
    }

    private void addCloud(float x, float y, float dx, float dy, float r0, float r1, long born, long life) {
        Cloud c = new Cloud();
        c.x = x;
        c.y = y;
        c.dx = dx;
        c.dy = dy;
        c.r0 = r0;
        c.r1 = r1;
        c.angle = random.nextFloat() * 360f;
        c.spin = (random.nextFloat() - 0.5f) * 120f;
        c.born = born;
        c.life = life;
        clouds.add(c);
    }

    // --- Kunai and shuriken -------------------------------------------------------------------

    /**
     * Kunai and shuriken thrown at a button: they land one after another in
     * its edges over about 0.6 s, the button shakes with the last one, and
     * they are gone 0.5 s later.
     */
    void volley(View target) {
        if (kunai == null) {
            kunai = makeKunai(Math.round(60 * density), Math.round(18 * density));
            shuriken = makeShuriken(Math.round(34 * density));
        }
        float[] center = centerOf(target);
        float w = target.getWidth(), h = target.getHeight();
        float left = center[0] - w / 2, top = center[1] - h / 2;
        float reach = Math.max(getWidth(), getHeight()) * 0.75f;
        long now = SystemClock.uptimeMillis();
        // What, where it lands (a fraction of the button), where it comes from (degrees from there), when.
        Object[][] throwsAt = {
                {false, 0.80f, 0.0f, -32f, 0L},
                {true, 1.0f, 0.42f, -6f, 50L},
                {false, 0.20f, 0.0f, -145f, 110L},
                {true, 0.50f, 0.0f, -96f, 170L},
                {false, 0.62f, 1.0f, 28f, 240L},
        };
        for (Object[] t : throwsAt) {
            Thrown x = new Thrown();
            x.kunai = (Boolean) t[0];
            x.tx = left + w * (Float) t[1];
            x.ty = top + h * (Float) t[2];
            double from = Math.toRadians((Float) t[3] + (random.nextFloat() - 0.5f) * 10f);
            x.sx = x.tx + (float) Math.cos(from) * reach;
            x.sy = x.ty + (float) Math.sin(from) * reach;
            x.heading = (float) Math.toDegrees(Math.atan2(x.ty - x.sy, x.tx - x.sx));
            x.spin = 900f + random.nextFloat() * 400f;
            x.start = now + (Long) t[4];
            thrown.add(x);
        }
        long last = 240L + FLIGHT_MS;
        postDelayed(() -> ObjectAnimator.ofFloat(target, View.TRANSLATION_X, 0f, -6 * density, 5 * density,
                -3 * density, 2 * density, 0f).setDuration(260).start(), last);
        postInvalidateOnAnimation();
    }

    // --- Drawing ------------------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        long now = SystemClock.uptimeMillis();
        for (Iterator<Cloud> it = clouds.iterator(); it.hasNext(); ) {
            Cloud c = it.next();
            float age = (now - c.born) / (float) c.life;
            if (age >= 1f) {
                it.remove();
                continue;
            }
            if (age < 0f) continue;
            float out = 1f - (1f - age) * (1f - age) * (1f - age);  // Fast, then slowing down.
            float x = c.x + c.dx * out, y = c.y + c.dy * out - c.r1 * 0.2f * age;
            float r = c.r0 + (c.r1 - c.r0) * out;
            float alpha = age < 0.06f ? age / 0.06f : (float) Math.pow(1f - (age - 0.06f) / 0.94f, 1.3);
            paint.setAlpha(Math.round(255 * alpha));
            canvas.save();
            canvas.rotate(c.angle + c.spin * age, x, y);
            bounds.set(x - r, y - r, x + r, y + r);
            canvas.drawBitmap(cloud, null, bounds, paint);
            canvas.restore();
        }
        for (Iterator<Thrown> it = thrown.iterator(); it.hasNext(); ) {
            Thrown t = it.next();
            long age = now - t.start;
            if (age >= FLIGHT_MS + STAY_MS + FADE_MS) {
                it.remove();
                continue;
            }
            if (age < 0) continue;
            if (age < FLIGHT_MS) {
                float p = age / (float) FLIGHT_MS;
                // Two fainter copies behind it: the blur of its speed.
                drawThrown(canvas, t, p - 0.1f, 50);
                drawThrown(canvas, t, p - 0.05f, 100);
                drawThrown(canvas, t, p, 255);
            } else {
                long stuck = age - FLIGHT_MS;
                int alpha = stuck < STAY_MS ? 255 : Math.round(255 * (1f - (stuck - STAY_MS) / (float) FADE_MS));
                drawThrown(canvas, t, 1f, alpha);
                if (stuck < SPARK_MS) drawSpark(canvas, t, stuck / (float) SPARK_MS);
            }
        }
        if (!clouds.isEmpty() || !thrown.isEmpty()) postInvalidateOnAnimation();
    }

    private void drawThrown(Canvas canvas, Thrown t, float p, int alpha) {
        if (p < 0f) return;
        float e = 1f - (float) Math.pow(1f - p, 1.4);  // A throw slows a little before it lands.
        float x = t.sx + (t.tx - t.sx) * e, y = t.sy + (t.ty - t.sy) * e;
        paint.setAlpha(alpha);
        canvas.save();
        if (t.kunai) {
            // Point first; its tip goes a little into the button.
            canvas.rotate(t.heading, x, y);
            float length = kunai.getWidth(), half = kunai.getHeight() / 2f, into = 5 * density;
            bounds.set(x - length + into, y - half, x + into, y + half);
        } else {
            canvas.rotate(t.spin * e, x, y);
            float half = shuriken.getWidth() / 2f;
            bounds.set(x - half, y - half, x + half, y + half);
        }
        canvas.drawBitmap(t.kunai ? kunai : shuriken, null, bounds, paint);
        canvas.restore();
    }

    /** A few short streaks of light where it landed. */
    private void drawSpark(Canvas canvas, Thrown t, float p) {
        spark.setColor(0xFFFFE2A0);
        spark.setAlpha(Math.round(255 * (1f - p)));
        float inner = (4 + 10 * p) * density, outer = inner + (8 * (1f - p) + 2) * density;
        for (int i = 0; i < 6; ++i) {
            double a = Math.toRadians(t.heading + 180 + (i - 2.5) * 34);
            float c = (float) Math.cos(a), s = (float) Math.sin(a);
            canvas.drawLine(t.tx + c * inner, t.ty + s * inner, t.tx + c * outer, t.ty + s * outer, spark);
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        clouds.clear();
        thrown.clear();
        super.onDetachedFromWindow();
    }

    private float[] centerOf(View view) {
        int[] here = new int[2], there = new int[2];
        getLocationInWindow(here);
        view.getLocationInWindow(there);
        return new float[] {there[0] - here[0] + view.getWidth() / 2f, there[1] - here[1] + view.getHeight() / 2f};
    }

    // --- The pictures -------------------------------------------------------------------------

    /** One billowing cloud: round lobes with a grey rim each, shaded towards one side. */
    private static Bitmap makeCloud(int size) {
        Bitmap bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint lobe = new Paint(Paint.ANTI_ALIAS_FLAG);
        float s = size;
        float[][] lobes = {  // x, y, radius (fractions of the size); the center one last.
                {0.30f, 0.40f, 0.20f}, {0.62f, 0.30f, 0.21f}, {0.74f, 0.58f, 0.20f},
                {0.48f, 0.72f, 0.21f}, {0.24f, 0.64f, 0.18f}, {0.50f, 0.48f, 0.27f},
        };
        for (float[] l : lobes) {
            float x = l[0] * s, y = l[1] * s, r = l[2] * s;
            lobe.setShader(new RadialGradient(x, y, r,
                    new int[] {0xFFFFFFFF, 0xFFF1EEE8, 0xFFBDB5A8, 0x00BDB5A8},
                    new float[] {0f, 0.5f, 0.86f, 1f}, Shader.TileMode.CLAMP));
            canvas.drawCircle(x, y, r, lobe);
        }
        Paint shade = new Paint(Paint.ANTI_ALIAS_FLAG);
        shade.setShader(new LinearGradient(0, 0, s, s, 0x00000000, 0x6E453C30, Shader.TileMode.CLAMP));
        shade.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC_ATOP));
        canvas.drawRect(0, 0, s, s, shade);
        return bitmap;
    }

    /** A kunai pointing right: a ring, a wrapped grip and a leaf-shaped steel blade. */
    private static Bitmap makeKunai(int length, int height) {
        Bitmap bitmap = Bitmap.createBitmap(length, height, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        float l = length, h = height, mid = h / 2;
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        // The ring.
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(h * 0.17f);
        p.setColor(0xFF3A3D44);
        canvas.drawCircle(h * 0.42f, mid, h * 0.3f, p);
        // The grip, wrapped in cloth.
        p.setStyle(Paint.Style.FILL);
        float gripStart = h * 0.68f, gripEnd = l * 0.52f, grip = h * 0.2f;
        p.setColor(0xFF2B1E16);
        canvas.drawRect(gripStart, mid - grip, gripEnd, mid + grip, p);
        p.setColor(0xFF7A5536);
        p.setStrokeWidth(h * 0.09f);
        for (float x = gripStart + h * 0.15f; x < gripEnd - h * 0.1f; x += h * 0.34f) {
            canvas.drawLine(x, mid + grip, x + h * 0.2f, mid - grip, p);
        }
        // The blade.
        Path blade = new Path();
        blade.moveTo(gripEnd - h * 0.04f, mid - h * 0.16f);
        blade.lineTo(l * 0.66f, h * 0.06f);
        blade.lineTo(l - 1, mid);
        blade.lineTo(l * 0.66f, h * 0.94f);
        blade.lineTo(gripEnd - h * 0.04f, mid + h * 0.16f);
        blade.close();
        p.setShader(new LinearGradient(0, 0, 0, h, 0xFFF1F4F8, 0xFF7E8691, Shader.TileMode.CLAMP));
        canvas.drawPath(blade, p);
        p.setShader(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1f, h * 0.07f));
        p.setColor(0xFF5A616B);
        canvas.drawLine(gripEnd, mid, l - h * 0.3f, mid, p);  // The ridge.
        p.setColor(0xFF2A2E34);
        canvas.drawPath(blade, p);
        return bitmap;
    }

    /** A four-pointed shuriken with a hole in the middle. */
    private static Bitmap makeShuriken(int size) {
        Bitmap bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        float c = size / 2f, outer = size * 0.48f, inner = size * 0.15f;
        Path star = new Path();
        for (int i = 0; i < 8; ++i) {
            double a = Math.toRadians(-90 + i * 45);
            float r = i % 2 == 0 ? outer : inner;
            float x = c + (float) Math.cos(a) * r, y = c + (float) Math.sin(a) * r;
            if (i == 0) star.moveTo(x, y);
            else star.lineTo(x, y);
        }
        star.close();
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setShader(new RadialGradient(c - size * 0.1f, c - size * 0.1f, outer,
                0xFFE4E8EE, 0xFF6E7681, Shader.TileMode.CLAMP));
        canvas.drawPath(star, p);
        p.setShader(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1f, size * 0.05f));
        p.setColor(0xFF2A2E34);
        canvas.drawPath(star, p);
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF1C1E22);
        canvas.drawCircle(c, c, size * 0.075f, p);
        return bitmap;
    }
}
