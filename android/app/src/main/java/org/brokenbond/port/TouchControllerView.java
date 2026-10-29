package org.brokenbond.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.util.SparseArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * The on-screen controller: an Xbox 360 pad drawn over the game.
 *
 * <p>It is an SDL virtual gamepad (touch_controller.cpp in the game library),
 * so the game and the PC Options menu see it as a controller, like a Bluetooth
 * pad. Sticks are floating: they center where the thumb lands in their area.
 * The gear button presses Back + Start, which opens PC Options.
 *
 * <p>The buttons are parchment discs with the game's action on them (attack,
 * kick, jump, block, kunai, sprint, jutsu, inventory) and the pad button in a
 * small badge, so the layout reads at a glance in a fight.
 */
public class TouchControllerView extends View {
    // SDL_GamepadButton.
    static final int A = 0, B = 1, X = 2, Y = 3, BACK = 4, START = 6, L3 = 7, R3 = 8, LB = 9, RB = 10;
    static final int DPAD_UP = 11, DPAD_DOWN = 12, DPAD_LEFT = 13, DPAD_RIGHT = 14;
    // SDL_GamepadAxis.
    static final int AXIS_LX = 0, AXIS_LY = 1, AXIS_RX = 2, AXIS_RY = 3, AXIS_LT = 4, AXIS_RT = 5;
    private static final int GEAR = -1, LT = -2, RT = -3;

    static native boolean nativeAttach();
    static native void nativeDetach();
    static native void nativeButton(int button, boolean pressed);
    static native void nativeAxis(int axis, int value);

    // Where a button's center is measured from.
    private static final int TOP_LEFT = 0, TOP_CENTER = 1, BOTTOM_LEFT = 2, BOTTOM_RIGHT = 3;
    // How a button is drawn.
    private static final int ROUND = 0, HEXAGON = 1, SMALL = 2, OPTIONS = 3, DPAD = 4;

    // Colors of the parchment theme.
    private static final int PARCHMENT_LIGHT = 0xFFF4E7C9, PARCHMENT_DARK = 0xFFD8C096;
    private static final int PRESSED_LIGHT = 0xFFE2CC9F, PRESSED_DARK = 0xFFB8955F;
    private static final int RING = 0xFF7A5A34, INK = 0xFF4A3420, INK_PRESSED = 0xFF2B1D10;
    private static final int BADGE = 0xFF2B2118, ACCENT = 0xFFF28C28;

    /** A button; id is an SDL button, or GEAR / LT / RT. */
    private static final class Button {
        final int id, anchor, shape, icon;
        final String badge;
        final float dx, dy, size;  // Center from the anchor, and radius (tile size for the D-pad), in units.
        final float rotation;      // Of the D-pad arrow.
        float cx, cy, r;           // In pixels, set by onSizeChanged().
        Drawable drawable;
        Shader shader, pressedShader;
        final Path outline = new Path();
        boolean down;

        Button(int id, int anchor, float dx, float dy, float size, int shape, int icon, String badge, float rotation) {
            this.id = id;
            this.anchor = anchor;
            this.dx = dx;
            this.dy = dy;
            this.size = size;
            this.shape = shape;
            this.icon = icon;
            this.badge = badge;
            this.rotation = rotation;
        }

        boolean hit(float x, float y) {
            float ddx = x - cx, ddy = y - cy;
            if (shape == DPAD) return Math.abs(ddx) <= r * 1.15f && Math.abs(ddy) <= r * 1.15f;
            float reach = r * 1.2f;
            return ddx * ddx + ddy * ddy <= reach * reach;
        }
    }

    /** A floating stick: its area is a rectangle; the base appears where the thumb lands. */
    private static final class Stick {
        final int axisX, axisY;
        final RectF area = new RectF();
        float baseX, baseY, knobX, knobY, radius, restX, restY;
        int pointer = -1;

        Stick(int axisX, int axisY) {
            this.axisX = axisX;
            this.axisY = axisY;
        }
    }

    private final List<Button> buttons = new ArrayList<>();
    private final Stick left = new Stick(AXIS_LX, AXIS_LY);
    private final Stick right = new Stick(AXIS_RX, AXIS_RY);
    private final SparseArray<Button> pointerButtons = new SparseArray<>();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path arrow = new Path();
    private final float scale;
    private final float opacity;
    private boolean attached;

    public TouchControllerView(Context context, float opacity, float scale) {
        super(context);
        this.scale = scale;
        this.opacity = Math.max(0.15f, Math.min(1f, opacity));
        // Right hand: the attacks around the big basic attack (X), the sprint trigger above.
        add(X, BOTTOM_RIGHT, 0.155f, 0.165f, 0.115f, ROUND, R.drawable.ic_touch_attack, "X");
        add(A, BOTTOM_RIGHT, 0.37f, 0.10f, 0.085f, ROUND, R.drawable.ic_touch_jump, "A");
        add(Y, BOTTOM_RIGHT, 0.33f, 0.33f, 0.085f, ROUND, R.drawable.ic_touch_heavy_attack, "Y");
        add(B, BOTTOM_RIGHT, 0.13f, 0.42f, 0.08f, ROUND, R.drawable.ic_touch_block, "B");
        add(RB, BOTTOM_RIGHT, 0.58f, 0.12f, 0.075f, ROUND, R.drawable.ic_touch_kunai, "RB");
        add(RT, BOTTOM_RIGHT, 0.12f, 0.66f, 0.085f, HEXAGON, R.drawable.ic_touch_sprint, "RT");
        add(R3, BOTTOM_RIGHT, 0.30f, 0.62f, 0.045f, SMALL, 0, "R3");
        // Left hand: jutsus and the inventory under the health bar, the D-pad below them.
        add(LT, TOP_LEFT, 0.12f, 0.30f, 0.085f, HEXAGON, R.drawable.ic_touch_jutsu, "LT");
        add(LB, TOP_LEFT, 0.32f, 0.27f, 0.075f, ROUND, R.drawable.ic_touch_inventory, "LB");
        add(L3, BOTTOM_LEFT, 0.065f, 0.065f, 0.045f, SMALL, 0, "L3");
        float padX = 0.20f, padY = 0.56f, arm = 0.10f, tile = 0.0475f;
        buttons.add(new Button(DPAD_UP, TOP_LEFT, padX, padY - arm, tile, DPAD, 0, null, 0f));
        buttons.add(new Button(DPAD_DOWN, TOP_LEFT, padX, padY + arm, tile, DPAD, 0, null, 180f));
        buttons.add(new Button(DPAD_LEFT, TOP_LEFT, padX - arm, padY, tile, DPAD, 0, null, 270f));
        buttons.add(new Button(DPAD_RIGHT, TOP_LEFT, padX + arm, padY, tile, DPAD, 0, null, 90f));
        // Menu row, top middle.
        add(BACK, TOP_CENTER, -0.13f, 0.08f, 0.05f, SMALL, 0, "BACK");
        add(GEAR, TOP_CENTER, 0f, 0.08f, 0.05f, OPTIONS, R.drawable.ic_touch_options, null);
        add(START, TOP_CENTER, 0.13f, 0.08f, 0.05f, SMALL, 0, "START");

        for (Button b : buttons) {
            if (b.icon != 0) {
                Drawable d = context.getDrawable(b.icon);
                if (d != null) b.drawable = d.mutate();
            }
        }
        stroke.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        setHapticFeedbackEnabled(true);
    }

    private void add(int id, int anchor, float dx, float dy, float radius, int shape, int icon, String badge) {
        buttons.add(new Button(id, anchor, dx, dy, radius, shape, icon, badge, 0f));
    }

    private int alpha(float a) {
        return Math.round(255 * Math.max(0f, Math.min(1f, a * opacity)));
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        float unit = h * scale;
        for (Button b : buttons) {
            b.r = b.size * unit;
            switch (b.anchor) {
                case TOP_LEFT: b.cx = b.dx * unit; b.cy = b.dy * unit; break;
                case TOP_CENTER: b.cx = w / 2f + b.dx * unit; b.cy = b.dy * unit; break;
                case BOTTOM_LEFT: b.cx = b.dx * unit; b.cy = h - b.dy * unit; break;
                default: b.cx = w - b.dx * unit; b.cy = h - b.dy * unit; break;
            }
            float r = b.r;
            b.outline.reset();
            if (b.shape == HEXAGON) {
                for (int i = 0; i < 6; ++i) {
                    double a = Math.PI / 3 * i - Math.PI / 2;
                    float px = b.cx + (float) (r * 1.08 * Math.cos(a)), py = b.cy + (float) (r * 1.08 * Math.sin(a));
                    if (i == 0) b.outline.moveTo(px, py); else b.outline.lineTo(px, py);
                }
                b.outline.close();
            } else if (b.shape == DPAD) {
                b.outline.addRoundRect(b.cx - r, b.cy - r, b.cx + r, b.cy + r, r * 0.44f, r * 0.44f, Path.Direction.CW);
            } else {
                b.outline.addCircle(b.cx, b.cy, r, Path.Direction.CW);
            }
            if (b.shape == DPAD) {
                b.shader = new LinearGradient(b.cx, b.cy - r, b.cx, b.cy + r, PARCHMENT_LIGHT, PARCHMENT_DARK, Shader.TileMode.CLAMP);
                b.pressedShader = new LinearGradient(b.cx, b.cy - r, b.cx, b.cy + r, PRESSED_LIGHT, PRESSED_DARK, Shader.TileMode.CLAMP);
            } else {
                b.shader = new RadialGradient(b.cx - r * 0.3f, b.cy - r * 0.35f, r * 1.2f, PARCHMENT_LIGHT, PARCHMENT_DARK, Shader.TileMode.CLAMP);
                b.pressedShader = new RadialGradient(b.cx - r * 0.3f, b.cy - r * 0.35f, r * 1.2f, PRESSED_LIGHT, PRESSED_DARK, Shader.TileMode.CLAMP);
            }
        }
        // Left stick: the lower left, under the D-pad. Camera: between it and the attack buttons.
        float cameraRight = Math.max(w * 0.55f, w - 0.70f * unit);
        left.area.set(0, h * 0.60f, w * 0.45f, h);
        right.area.set(w * 0.45f, h * 0.40f, cameraRight, h);
        left.radius = right.radius = 0.16f * unit;
        left.restX = w * 0.22f;
        right.restX = right.area.centerX();
        left.restY = right.restY = h - 0.20f * unit;
        stroke.setStrokeWidth(Math.max(2f, unit * 0.007f));
    }

    private boolean ensureAttached() {
        if (!attached) {
            try {
                attached = nativeAttach();
            } catch (UnsatisfiedLinkError e) {
                attached = false;
            }
        }
        return attached;
    }

    private void press(Button b, boolean down) {
        if (b.down == down) return;
        b.down = down;
        if (down) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        if (!ensureAttached()) return;
        switch (b.id) {
            case GEAR:  // PC Options: Back + Start together.
                nativeButton(BACK, down);
                nativeButton(START, down);
                break;
            case LT:
                nativeAxis(AXIS_LT, down ? 32767 : -32768);
                break;
            case RT:
                nativeAxis(AXIS_RT, down ? 32767 : -32768);
                break;
            default:
                nativeButton(b.id, down);
        }
    }

    private void moveStick(Stick s, float x, float y) {
        float dx = x - s.baseX, dy = y - s.baseY;
        float len = (float) Math.hypot(dx, dy);
        if (len > s.radius) {
            dx = dx / len * s.radius;
            dy = dy / len * s.radius;
        }
        s.knobX = s.baseX + dx;
        s.knobY = s.baseY + dy;
        if (ensureAttached()) {
            nativeAxis(s.axisX, Math.round(dx / s.radius * 32767));
            nativeAxis(s.axisY, Math.round(dy / s.radius * 32767));  // SDL: down is positive.
        }
    }

    private void releaseStick(Stick s) {
        s.pointer = -1;
        if (ensureAttached()) {
            nativeAxis(s.axisX, 0);
            nativeAxis(s.axisY, 0);
        }
    }

    private Button buttonAt(float x, float y) {
        for (Button b : buttons) if (b.hit(x, y)) return b;
        return null;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int id = e.getPointerId(index);
                float x = e.getX(index), y = e.getY(index);
                Button b = buttonAt(x, y);
                if (b != null) {
                    pointerButtons.put(id, b);
                    press(b, true);
                } else if (left.pointer < 0 && left.area.contains(x, y)) {
                    left.pointer = id;
                    left.baseX = left.knobX = x;
                    left.baseY = left.knobY = y;
                } else if (right.pointer < 0 && right.area.contains(x, y)) {
                    right.pointer = id;
                    right.baseX = right.knobX = x;
                    right.baseY = right.knobY = y;
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); ++i) {
                    int id = e.getPointerId(i);
                    float x = e.getX(i), y = e.getY(i);
                    if (id == left.pointer) moveStick(left, x, y);
                    else if (id == right.pointer) moveStick(right, x, y);
                    else {
                        // Sliding between buttons (for example A to B) follows the thumb.
                        Button now = buttonAt(x, y), before = pointerButtons.get(id);
                        if (now != before) {
                            if (before != null) press(before, false);
                            if (now != null) {
                                press(now, true);
                                pointerButtons.put(id, now);
                            } else {
                                pointerButtons.remove(id);
                            }
                        }
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                if (action == MotionEvent.ACTION_CANCEL) {
                    for (int i = 0; i < pointerButtons.size(); ++i) press(pointerButtons.valueAt(i), false);
                    pointerButtons.clear();
                    if (left.pointer >= 0) releaseStick(left);
                    if (right.pointer >= 0) releaseStick(right);
                    break;
                }
                int id = e.getPointerId(index);
                Button b = pointerButtons.get(id);
                if (b != null) {
                    press(b, false);
                    pointerButtons.remove(id);
                }
                if (id == left.pointer) releaseStick(left);
                if (id == right.pointer) releaseStick(right);
                break;
            }
            default:
                break;
        }
        invalidate();
        return true;
    }

    /** Releases everything (the view hides, or the game pauses). */
    void releaseAll() {
        for (Button b : buttons) press(b, false);
        pointerButtons.clear();
        if (left.pointer >= 0) releaseStick(left);
        if (right.pointer >= 0) releaseStick(right);
        invalidate();
    }

    /**
     * Hidden: everything released and the virtual gamepad detached, so the game
     * stops counting it as a controller (a physical one is player 1). The next
     * touch after the view shows again attaches it.
     */
    void detach() {
        releaseAll();
        if (!attached) return;
        try {
            nativeDetach();
        } catch (UnsatisfiedLinkError ignored) {
            // No game library: nothing was attached.
        }
        attached = false;
    }

    /** A parchment disc: shaded fill with a soft shadow, and a dark rim. */
    private void drawDisc(Canvas canvas, float cx, float cy, float r, boolean pressed, float a) {
        fill.setShader(new RadialGradient(cx - r * 0.3f, cy - r * 0.35f, r * 1.2f,
                pressed ? PRESSED_LIGHT : PARCHMENT_LIGHT, pressed ? PRESSED_DARK : PARCHMENT_DARK, Shader.TileMode.CLAMP));
        fill.setAlpha(alpha(a));
        fill.setShadowLayer(r * 0.25f, 0, r * 0.06f, 0x73000000);
        canvas.drawCircle(cx, cy, r, fill);
        fill.clearShadowLayer();
        fill.setShader(null);
        stroke.setColor(RING);
        stroke.setAlpha(alpha(a));
        canvas.drawCircle(cx, cy, r, stroke);
    }

    private void drawStick(Canvas canvas, Stick s) {
        boolean active = s.pointer >= 0;
        float cx = active ? s.baseX : s.restX, cy = active ? s.baseY : s.restY;
        float kx = active ? s.knobX : cx, ky = active ? s.knobY : cy;
        float a = active ? 1f : 0.55f, r = s.radius;
        fill.setColor(0xFF1E140A);
        fill.setAlpha(alpha(a * 0.25f));
        canvas.drawCircle(cx, cy, r, fill);
        stroke.setColor(PARCHMENT_LIGHT);
        stroke.setAlpha(alpha(a));
        canvas.drawCircle(cx, cy, r, stroke);
        // Four direction marks on the ring.
        fill.setColor(PARCHMENT_LIGHT);
        fill.setAlpha(alpha(a));
        for (int i = 0; i < 4; ++i) {
            canvas.save();
            canvas.rotate(90f * i, cx, cy);
            arrow.reset();
            arrow.moveTo(cx, cy - r * 0.86f);
            arrow.lineTo(cx + r * 0.07f, cy - r * 0.74f);
            arrow.lineTo(cx - r * 0.07f, cy - r * 0.74f);
            arrow.close();
            canvas.drawPath(arrow, fill);
            canvas.restore();
        }
        drawDisc(canvas, kx, ky, r * 0.42f, false, a);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        drawStick(canvas, left);
        drawStick(canvas, right);
        for (Button b : buttons) {
            float r = b.r, cx = b.cx, cy = b.cy;
            canvas.save();
            if (b.down) canvas.scale(0.94f, 0.94f, cx, cy);
            // Body.
            fill.setShader(b.down ? b.pressedShader : b.shader);
            fill.setAlpha(alpha(1f));
            fill.setShadowLayer(r * 0.25f, 0, r * 0.06f, 0x73000000);
            canvas.drawPath(b.outline, fill);
            fill.clearShadowLayer();
            fill.setShader(null);
            stroke.setColor(b.shape == OPTIONS ? ACCENT : RING);
            stroke.setAlpha(alpha(1f));
            canvas.drawPath(b.outline, stroke);
            int ink = b.down ? INK_PRESSED : INK;
            if (b.shape == DPAD) {
                // An arrow pointing away from the pad's center.
                canvas.save();
                canvas.rotate(b.rotation, cx, cy);
                float s = r * 1.0f;
                arrow.reset();
                arrow.moveTo(cx, cy - s * 0.55f);
                arrow.lineTo(cx + s * 0.5f, cy + s * 0.35f);
                arrow.lineTo(cx - s * 0.5f, cy + s * 0.35f);
                arrow.close();
                fill.setColor(ink);
                fill.setAlpha(alpha(1f));
                canvas.drawPath(arrow, fill);
                canvas.restore();
            } else if (b.drawable != null) {
                float size = r * (b.shape == OPTIONS ? 1.15f : 1.05f);
                int half = Math.round(size / 2);
                b.drawable.setBounds(Math.round(cx) - half, Math.round(cy) - half, Math.round(cx) + half, Math.round(cy) + half);
                b.drawable.setTint(ink);
                b.drawable.setAlpha(alpha(1f));
                b.drawable.draw(canvas);
            } else if (b.badge != null) {
                // Small buttons: their name on the disc.
                text.setColor(ink);
                text.setAlpha(alpha(1f));
                text.setTextSize(r * (b.badge.length() > 2 ? 0.42f : 0.62f));
                canvas.drawText(b.badge, cx, cy - (text.descent() + text.ascent()) / 2, text);
            }
            // The pad button, in a badge at the lower right of the icon buttons.
            if (b.drawable != null && b.badge != null) {
                float br = r * 0.34f, bx = cx + r * 0.70f, by = cy + r * 0.70f;
                fill.setColor(BADGE);
                fill.setAlpha(alpha(1f));
                canvas.drawCircle(bx, by, br, fill);
                float width = stroke.getStrokeWidth();
                stroke.setStrokeWidth(Math.max(1.5f, width * 0.6f));
                stroke.setColor(PARCHMENT_LIGHT);
                stroke.setAlpha(alpha(1f));
                canvas.drawCircle(bx, by, br, stroke);
                stroke.setStrokeWidth(width);
                text.setColor(0xFFFFFFFF);
                text.setAlpha(alpha(1f));
                text.setTextSize(br * (b.badge.length() > 1 ? 0.95f : 1.25f));
                canvas.drawText(b.badge, bx, by - (text.descent() + text.ascent()) / 2, text);
            }
            canvas.restore();
        }
    }
}
