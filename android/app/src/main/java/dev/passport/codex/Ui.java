package dev.passport.codex;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.view.*;
import android.widget.*;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.shape.ShapeAppearanceModel;
import com.google.android.material.textfield.*;

final class Ui {
    final Activity activity;
    Ui(Activity activity){this.activity=activity;}
    int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    int color(int id){return activity.getColor(id);}
    LinearLayout column(){LinearLayout view=new LinearLayout(activity);view.setOrientation(LinearLayout.VERTICAL);return view;}
    LinearLayout row(){LinearLayout view=new LinearLayout(activity);view.setGravity(Gravity.CENTER_VERTICAL);return view;}
    LinearLayout.LayoutParams box(int w,int h){return new LinearLayout.LayoutParams(w<0?w:dp(w),h<0?h:dp(h));}
    void gap(LinearLayout parent,int height){parent.addView(new View(activity),box(1,height));}
    TextView text(String value,int size,boolean bold){
        TextView view=new TextView(activity);view.setText(value);view.setTextSize(size);view.setTextColor(color(bold?R.color.on_surface:R.color.on_variant));view.setIncludeFontPadding(false);
        if(bold)view.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));return view;
    }
    LinearLayout card(LinearLayout parent,boolean hero){
        MaterialCardView card=new MaterialCardView(activity);card.setCardElevation(0);card.setStrokeWidth(0);
        card.setCardBackgroundColor(color(hero?R.color.primary_container:R.color.container));
        card.setShapeAppearanceModel(ShapeAppearanceModel.builder().setAllCornerSizes(dp(hero?32:24)).build());
        LinearLayout.LayoutParams lp=box(-1,-2);lp.bottomMargin=dp(14);parent.addView(card,lp);
        LinearLayout inside=column();inside.setPadding(dp(22),dp(22),dp(22),dp(22));card.addView(inside);return inside;
    }
    MaterialButton button(LinearLayout parent,String title,boolean primary,View.OnClickListener action){
        MaterialButton b=new MaterialButton(activity);b.setText(title);b.setTextSize(16);b.setAllCaps(false);b.setCornerRadius(dp(28));b.setInsetTop(0);b.setInsetBottom(0);
        b.setTextColor(color(primary?R.color.on_primary:R.color.primary));b.setBackgroundTintList(ColorStateList.valueOf(color(primary?R.color.primary:R.color.secondary_container)));
        LinearLayout.LayoutParams lp=box(-1,56);lp.bottomMargin=dp(10);parent.addView(b,lp);b.setOnClickListener(action);return b;
    }
    EditText input(LinearLayout parent,String hint,String value,boolean secret){
        TextInputLayout outer=new TextInputLayout(activity);outer.setHint(hint);outer.setBoxBackgroundMode(TextInputLayout.BOX_BACKGROUND_OUTLINE);outer.setBoxCornerRadii(dp(18),dp(18),dp(18),dp(18));
        TextInputEditText edit=new TextInputEditText(outer.getContext());edit.setSingleLine();edit.setTextSize(16);edit.setInputType(secret?129:1);edit.setText(value);outer.addView(edit);
        if(secret){outer.setEndIconMode(TextInputLayout.END_ICON_PASSWORD_TOGGLE);if(android.os.Build.VERSION.SDK_INT>=26)edit.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);}
        LinearLayout.LayoutParams lp=box(-1,-2);lp.bottomMargin=dp(14);parent.addView(outer,lp);return edit;
    }
}
