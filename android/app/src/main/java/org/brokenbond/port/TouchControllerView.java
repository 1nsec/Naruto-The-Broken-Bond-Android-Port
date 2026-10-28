package org.brokenbond.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
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
 */
public class TouchControllerView extends View {
    // SDL_GamepadButton.
    static final int A = 0, B = 1, X = 2, Y = 3, BACK = 4, START = 6, L3 = 7, R3 = 8, LB = 9, RB = 10;
    static final int DPAD_UP = 11, DPAD_DOWN = 12, DPAD_LEFT = 13, DPAD_RIGHT = 14;
    // SDL_GamepadAxis.
    static final int AXIS_LX = 0, AXIS_LY = 1, AXIS_RX = 2, AXIS_RY = 3, AXIS_LT = 4, AXIS_RT = 5;
    private static final int GEAR = -1, LT = -2, RT = -3;

    static native boolean nativeAttach();
    static native void nativeButton(int button, boolean pressed);
    static native void nativeAxis(int axis, int value);

    /** A round button; id is an SDL button, or GEAR / LT / RT. */
    private static final class Button {
        final int id;
        final String label;
        float cx, cy, r;  // In pixels, set by layout().
        final float fx, fy, fr;  // Fractions of the height, from the given anchor.
        final boolean right, top;
        boolean down;

        Button(int id, String label, float fx, float fy, float fr, boolean right, boolean top) {
            this.id = id;
            this.label = label;
            this.fx = fx;
            this.fy = fy;
            this.fr = fr;
            this.right = right;
            this.top = top;
        }

        boolean hit(float x, float y) {
            float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy <= (r * 1.25f) * (r * 1.25f);
        }
    }

    /** A floating stick: its area is a rectangle; the base appears where the thumb lands. */
    private static final class Stick {
        final int axisX, axisY;
        final RectF area = new RectF();
        float baseX, baseY, knobX, knobY, radius;
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
    private final float scale;
    private int alpha;
    private boolean attached;

    public TouchControllerView(Context context, float opacity, float scale) {
        super(context);
        this.scale = scale;
        this.alpha = Math.round(255 * Math.max(0.15f, Math.min(1f, opacity)));
        // Face buttons (right), shoulders and triggers (top corners), menu buttons (top middle).
        buttons.add(new Button(A, "A", 0.20f, 0.20f, 0.075f, true, false));
        buttons.add(new Button(B, "B", 0.08f, 0.33f, 0.075f, true, false));
        buttons.add(new Button(X, "X", 0.32f, 0.33f, 0.075f, true, false));
        buttons.add(new Button(Y, "Y", 0.20f, 0.46f, 0.075f, true, false));
        buttons.add(new Button(RT, "RT", 0.03f, 0.03f, 0.07f, true, true));
        buttons.add(new Button(RB, "RB", 0.20f, 0.03f, 0.07f, true, true));
        buttons.add(new Button(LT, "LT", 0.03f, 0.03f, 0.07f, false, true));
        buttons.add(new Button(LB, "LB", 0.20f, 0.03f, 0.07f, false, true));
        buttons.add(new Button(R3, "R3", 0.52f, 0.06f, 0.05f, true, false));
        buttons.add(new Button(L3, "L3", 0.40f, 0.06f, 0.05f, false, false));
        buttons.add(new Button(BACK, "Back", -0.16f, 0.03f, 0.05f, false, true));
        buttons.add(new Button(START, "Start", 0.16f, 0.03f, 0.05f, false, true));
        buttons.add(new Button(GEAR, "\u2699", 0.0f, 0.03f, 0.05f, false, true));
        buttons.add(new Button(DPAD_UP, "\u25B2", 0.12f, 0.22f, 0.05f, false, true));
        buttons.add(new Button(DPAD_DOWN, "\u25BC", 0.12f, 0.43f, 0.05f, false, true));
        buttons.add(new Button(DPAD_LEFT, "\u25C0", 0.01f, 0.325f, 0.05f, false, true));
        buttons.add(new Button(DPAD_RIGHT, "\u25B6", 0.23f, 0.325f, 0.05f, false, true));
        stroke.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        setHapticFeedbackEnabled(true);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        float unit = h * scale;
        for (Button b : buttons) {
            b.r = b.fr * unit;
            if (b.id == GEAR || b.id == BACK || b.id == START) {
                b.cx = w / 2f + b.fx * unit;  // Centered menu row.
            } else {
                b.cx = b.right ? w - b.fx * unit - b.r : b.fx * unit + b.r;
            }
            b.cy = b.top ? b.fy * unit + b.r : h - b.fy * unit - b.r;
        }
        // Left stick: the lower left, under the D-pad; right stick (camera): the lower middle-right.
        left.area.set(0, h * 0.5f, w * 0.42f, h);
        right.area.set(w * 0.42f, h * 0.45f, w * 0.68f, h);
        left.radius = right.radius = 0.16f * unit;
        stroke.setStrokeWidth(Math.max(2f, unit * 0.006f));
        text.setTextSize(unit * 0.045f);
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

    private void drawStick(Canvas canvas, Stick s) {
        if (s.pointer < 0) {
            // A hint of where the stick lives.
            float cx = s.area.centerX(), cy = s.area.bottom - s.radius * 1.4f;
            stroke.setColor(0xFFFFFFFF);
            stroke.setAlpha(alpha / 3);
            canvas.drawCircle(cx, cy, s.radius, stroke);
            return;
        }
        fill.setColor(0xFF000000);
        fill.setAlpha(alpha / 3);
        canvas.drawCircle(s.baseX, s.baseY, s.radius, fill);
        stroke.setColor(0xFFFFFFFF);
        stroke.setAlpha(alpha);
        canvas.drawCircle(s.baseX, s.baseY, s.radius, stroke);
        fill.setColor(0xFFFFFFFF);
        fill.setAlpha(alpha);
        canvas.drawCircle(s.knobX, s.knobY, s.radius * 0.45f, fill);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        drawStick(canvas, left);
        drawStick(canvas, right);
        for (Button b : buttons) {
            int color;
            switch (b.id) {
                case A: color = 0xFF3CB043; break;
                case B: color = 0xFFD9342B; break;
                case X: color = 0xFF2F6FDB; break;
                case Y: color = 0xFFF2B705; break;
                default: color = 0xFF202020; break;
            }
            fill.setColor(color);
            fill.setAlpha(b.down ? Math.min(255, alpha + 80) : alpha / 2);
            canvas.drawCircle(b.cx, b.cy, b.r, fill);
            stroke.setColor(0xFFFFFFFF);
            stroke.setAlpha(alpha);
            canvas.drawCircle(b.cx, b.cy, b.r, stroke);
            text.setColor(0xFFFFFFFF);
            text.setAlpha(Math.min(255, alpha + 40));
            canvas.drawText(b.label, b.cx, b.cy - (text.descent() + text.ascent()) / 2, text);
        }
    }
}
