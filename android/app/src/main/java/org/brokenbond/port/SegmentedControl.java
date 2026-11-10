package org.brokenbond.port;

import android.content.Context;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * A row of choices in a dark pill, the chosen one on an orange pill: the
 * launcher's quick settings. Each choice can have a small line under its name.
 */
final class SegmentedControl extends LinearLayout {
    interface Listener {
        void onChosen(int index);
    }

    private final LinearLayout[] options;
    private final TextView[] names, notes;
    private int chosen = -1;

    SegmentedControl(Context context, String[] labels, String[] subLabels, Listener listener) {
        super(context);
        setOrientation(HORIZONTAL);
        setBackgroundResource(R.drawable.bg_segment_track);
        int pad = dp(4);
        setPadding(pad, pad, pad, pad);
        options = new LinearLayout[labels.length];
        names = new TextView[labels.length];
        notes = new TextView[labels.length];
        for (int i = 0; i < labels.length; ++i) {
            final int index = i;
            LinearLayout option = new LinearLayout(context);
            option.setOrientation(VERTICAL);
            option.setGravity(Gravity.CENTER);
            option.setPadding(dp(6), dp(subLabels != null ? 5 : 8), dp(6), dp(subLabels != null ? 5 : 8));
            TextView name = new TextView(context);
            name.setText(labels[i]);
            name.setGravity(Gravity.CENTER);
            name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
            name.setTypeface(Typeface.DEFAULT_BOLD);
            name.setSingleLine(true);
            option.addView(name);
            names[i] = name;
            if (subLabels != null && subLabels[i] != null) {
                TextView note = new TextView(context);
                note.setText(subLabels[i]);
                note.setGravity(Gravity.CENTER);
                note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
                note.setSingleLine(true);
                option.addView(note);
                notes[i] = note;
            }
            option.setOnClickListener(v -> {
                if (chosen == index) return;
                setChosen(index);
                listener.onChosen(index);
            });
            options[i] = option;
            addView(option, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f));
        }
        setChosen(-1);
    }

    void setChosen(int index) {
        chosen = index;
        for (int i = 0; i < options.length; ++i) {
            boolean on = i == index;
            options[i].setBackgroundResource(on ? R.drawable.bg_segment_on : 0);
            names[i].setTextColor(on ? 0xFF1A1208 : 0xFFF3ECE4);
            if (notes[i] != null) notes[i].setTextColor(on ? 0xCC1A1208 : 0xFFA89A8C);
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
