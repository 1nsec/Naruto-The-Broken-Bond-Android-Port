package org.brokenbond.port;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.Shader;
import android.graphics.drawable.Drawable;

/**
 * A slanted band of light that crosses a rounded button (the launcher's Play,
 * a red seal), drawn as its foreground and clipped to its shape. The position goes
 * from 0 (the band at the left edge) to 1 (the right edge); outside that range
 * nothing shows, which gives the pause between two passes.
 */
final class ShineDrawable extends Drawable {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path pill = new Path();
    private float position = -1f;

    void setPosition(float value) {
        position = value;
        invalidateSelf();
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        pill.reset();
        float radius = Math.min(bounds.height() / 2f, 14 * android.content.res.Resources.getSystem().getDisplayMetrics().density);
        pill.addRoundRect(bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius, Path.Direction.CW);
    }

    @Override
    public void draw(Canvas canvas) {
        Rect b = getBounds();
        if (position < -0.2f || position > 1.2f || b.isEmpty()) return;
        float band = b.height() * 0.9f, center = b.left + position * b.width();
        paint.setShader(new LinearGradient(center - band, b.top, center + band, b.bottom,
                new int[] {0x00FFFFFF, 0x66FFFFFF, 0x00FFFFFF}, new float[] {0f, 0.5f, 1f}, Shader.TileMode.CLAMP));
        canvas.save();
        canvas.clipPath(pill);
        canvas.drawRect(b, paint);
        canvas.restore();
    }

    @Override
    public void setAlpha(int alpha) {
        paint.setAlpha(alpha);
    }

    @Override
    public void setColorFilter(ColorFilter filter) {
        paint.setColorFilter(filter);
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }
}
