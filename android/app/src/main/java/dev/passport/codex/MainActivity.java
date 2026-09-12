package dev.passport.codex;

import android.Manifest;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.os.*;
import android.view.*;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.*;
import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.android.material.shape.ShapeAppearanceModel;
import com.google.android.material.textfield.*;
import org.json.*;
import java.io.*;
import java.text.DateFormat;
import java.util.Date;

/** Original companion UI, using the official Material 3 Expressive theme/components. */
public class MainActivity extends AppCompatActivity {
    private final Handler handler=new Handler(Looper.getMainLooper());
    private SharedPreferences prefs;
    private LinearLayout content;
    private ScrollView scroll;
    private BottomNavigationView navigation;
    private EditText endpoint,token;
    private int page=1;
    private String rendered="";
    private JSONObject data=new JSONObject();
    private boolean connected;
    private String statusMessage;
    private final Runnable refresh=new Runnable(){public void run(){update();handler.postDelayed(this,2000);}};

    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private int color(int resource){return getColor(resource);}
    private LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    private LinearLayout row(){LinearLayout l=new LinearLayout(this);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    private LinearLayout.LayoutParams box(int width,int height){return new LinearLayout.LayoutParams(width<0?width:dp(width),height<0?height:dp(height));}
    private TextView text(String value,int size,int color,boolean bold){
        TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);
        t.setFontFeatureSettings("kern");t.setIncludeFontPadding(false);
        if(bold)t.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        return t;
    }
    private void gap(LinearLayout parent,int height){parent.addView(new View(this),box(1,height));}
    private void heading(String title,String sub){
        gap(content,16);content.addView(text(title,32,color(R.color.on_surface),true));
        gap(content,10);content.addView(text(sub,14,color(R.color.on_variant),false));gap(content,24);
    }
    private MaterialCardView card(LinearLayout parent,int background,int radius){
        MaterialCardView c=new MaterialCardView(this);c.setCardBackgroundColor(background);c.setCardElevation(0);c.setStrokeWidth(0);
        c.setShapeAppearanceModel(ShapeAppearanceModel.builder().setAllCornerSizes(dp(radius)).build());
        LinearLayout.LayoutParams lp=box(-1,-2);lp.bottomMargin=dp(12);parent.addView(c,lp);return c;
    }
    private LinearLayout inside(MaterialCardView c,int padding){LinearLayout l=column();l.setPadding(dp(padding),dp(padding),dp(padding),dp(padding));c.addView(l);return l;}
    private MaterialButton button(String title,int icon,boolean filled){
        MaterialButton b=new MaterialButton(this);b.setText(title);b.setTextSize(16);b.setAllCaps(false);
        b.setCornerRadius(dp(28));b.setInsetTop(0);b.setInsetBottom(0);
        b.setIconResource(icon);b.setIconGravity(MaterialButton.ICON_GRAVITY_TEXT_START);b.setIconPadding(dp(10));
        int ink=color(filled?R.color.on_primary:R.color.primary);
        b.setTextColor(ink);b.setIconTint(ColorStateList.valueOf(ink));
        b.setBackgroundTintList(ColorStateList.valueOf(color(filled?R.color.primary:R.color.secondary_container)));
        b.setLayoutParams(box(-1,56));return b;
    }
    private TextView tag(String label,int background,int foreground){
        TextView t=text(label,12,foreground,true);t.setPadding(dp(12),dp(7),dp(12),dp(7));
        android.graphics.drawable.GradientDrawable d=new android.graphics.drawable.GradientDrawable();d.setColor(background);d.setCornerRadius(dp(20));t.setBackground(d);return t;
    }
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);prefs=getSharedPreferences("relay",0);provision();
        WindowCompat.setDecorFitsSystemWindows(getWindow(),false);
        boolean night=(getResources().getConfiguration().uiMode&0x30)==0x20;
        WindowInsetsControllerCompat bars=WindowCompat.getInsetsController(getWindow(),getWindow().getDecorView());bars.setAppearanceLightStatusBars(!night);bars.setAppearanceLightNavigationBars(!night);
        LinearLayout root=column();root.setBackgroundColor(color(R.color.surface));setContentView(root);
        ViewCompat.setOnApplyWindowInsetsListener(root,(v,insets)->{Insets i=insets.getInsets(WindowInsetsCompat.Type.systemBars()|WindowInsetsCompat.Type.ime());v.setPadding(i.left,i.top,i.right,i.bottom);return insets;});
        scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setClipToPadding(false);scroll.setVerticalScrollBarEnabled(false);
        content=column();content.setFocusableInTouchMode(true);content.setDescendantFocusability(ViewGroup.FOCUS_BEFORE_DESCENDANTS);content.setPadding(dp(22),dp(16),dp(22),dp(16));scroll.addView(content);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        navigation=new BottomNavigationView(this);navigation.setBackgroundColor(color(R.color.surface));navigation.setElevation(0);
        navigation.getMenu().add(0,1,0,"概览").setIcon(R.drawable.ic_home);
        navigation.getMenu().add(0,2,1,"通知").setIcon(R.drawable.ic_inbox);
        navigation.getMenu().add(0,3,2,"连接").setIcon(R.drawable.ic_link);
        navigation.setOnItemReselectedListener(item->scroll.smoothScrollTo(0,0));
        navigation.setOnItemSelectedListener(item->{page=item.getItemId();rendered="";render();scroll.smoothScrollTo(0,0);return true;});root.addView(navigation,box(-1,-2));
        if(saved!=null)page=saved.getInt("page",1);navigation.setSelectedItemId(page);update();
    }
    private void provision(){
        File f=new File(getFilesDir(),"relay.json");if(!f.exists()||f.length()>8192)return;
        try(FileInputStream in=new FileInputStream(f)){
            ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] bytes=new byte[1024];int n;while((n=in.read(bytes))!=-1)out.write(bytes,0,n);
            JSONObject o=new JSONObject(out.toString("UTF-8"));prefs.edit().putString("endpoint",o.getString("endpoint")).putString("token",o.getString("token")).apply();f.delete();
        }catch(Exception ignored){}
    }
    private void update(){
        String raw=prefs.getString("snapshot","{}");
        try{data=new JSONObject(raw);}catch(JSONException e){data=new JSONObject();}
        long age=System.currentTimeMillis()-prefs.getLong("statusAt",0);
        statusMessage=age>30000?"连接尚未就绪":prefs.getString("status","尚未连接");
        connected=age<30000&&statusMessage.startsWith("同步正常");
        String sig=page+raw+statusMessage+(System.currentTimeMillis()/60000);
        if(!sig.equals(rendered)&&page!=3){rendered=sig;int y=scroll.getScrollY();render();scroll.post(()->scroll.scrollTo(0,y));}
        int unread=data.optInt("unread");
        if(unread>0){navigation.getOrCreateBadge(2).setNumber(unread);}else navigation.removeBadge(2);
    }
    private void render(){
        content.removeAllViews();
        LinearLayout top=row();TextView brand=text("Passport",23,color(R.color.on_surface),true);top.addView(brand,new LinearLayout.LayoutParams(0,-2,1));
        top.addView(tag("CODEX",color(R.color.secondary_container),color(R.color.primary)));content.addView(top);
        if(page==3){connectionPage();return;}
        if(page==2){heading("你的通知","只保留需要你关注的进展");notices(4);return;}
        heading("随身工作台","重要进展，随身可见。");
        LinearLayout hero=inside(card(content,color(R.color.primary_container),32),22);
        LinearLayout label=row();label.addView(tag(connected?"●  已连接":"○  未连接",color(R.color.surface),color(R.color.primary)),box(-2,-2));
        TextView name=text("AI Passport",14,color(R.color.on_primary_container),true);name.setGravity(Gravity.END);label.addView(name,new LinearLayout.LayoutParams(0,-2,1));hero.addView(label);gap(hero,18);
        hero.addView(text(connected?"进展已在身边":"让进展跟上你",28,color(R.color.on_primary_container),true));gap(hero,8);
        hero.addView(text(connected?(prefs.getBoolean("desktopOwns",false)?"电脑正在通过蓝牙同步到 Passport":"手机正在将通知同步到 Passport"):statusMessage==null?"连接后自动同步通知与用量":statusMessage,14,color(R.color.on_primary_container),false));gap(hero,20);
        MaterialButton connect=button(connected?"暂停手机同步":"连接 Passport",connected?R.drawable.ic_pause:R.drawable.ic_arrow,true);
        connect.setOnClickListener(v->{if(connected)pause();else requestConnect();});hero.addView(connect);
        LinearLayout metrics=row();metric(metrics,data.optInt("running"),"正在进行",false);metric(metrics,data.optInt("waiting"),"等待回复",true);content.addView(metrics);gap(content,20);
        usage();gap(content,8);
        LinearLayout section=row();section.addView(text("最近进展",22,color(R.color.on_surface),true),new LinearLayout.LayoutParams(0,-2,1));
        MaterialButton all=new MaterialButton(this,null,com.google.android.material.R.attr.materialButtonOutlinedStyle);all.setText("查看全部");all.setOnClickListener(v->navigation.setSelectedItemId(2));section.addView(all);content.addView(section);gap(content,12);notices(2);
    }
    private void metric(LinearLayout parent,int value,String title,boolean attention){
        LinearLayout l=column();l.setPadding(dp(20),dp(16),dp(20),dp(16));
        android.graphics.drawable.GradientDrawable bg=new android.graphics.drawable.GradientDrawable();bg.setCornerRadius(dp(24));bg.setColor(color(attention&&value>0?R.color.tertiary_container:R.color.container));l.setBackground(bg);
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,-2,1);if(parent.getChildCount()==0)lp.rightMargin=dp(10);parent.addView(l,lp);
        l.addView(text(Integer.toString(value),34,color(R.color.on_surface),true));gap(l,4);l.addView(text(title,13,color(R.color.on_variant),false));
    }
    private LinearProgressIndicator progress(int value,int indicator,boolean wavy){
        LinearProgressIndicator bar=new LinearProgressIndicator(this);bar.setTrackThickness(dp(wavy?6:4));bar.setTrackCornerRadius(dp(3));
        if(wavy){bar.setWaveAmplitude(dp(2));bar.setWavelength(dp(24));bar.setWaveSpeed(0);}
        bar.setIndicatorColor(indicator);bar.setTrackColor(color(R.color.secondary_container));bar.setMax(100);bar.setProgress(value);return bar;
    }
    private String duration(int minutes){
        return minutes>0&&minutes%1440==0?(minutes/1440)+" 天周期":minutes>0&&minutes%60==0?(minutes/60)+" 小时周期":minutes>0?minutes+" 分钟周期":"额度周期";
    }
    private String countdown(long seconds){
        long mins=Math.max(1,(seconds+59)/60);
        if(mins>=1440)return (mins/1440)+" 天 "+(mins%1440/60)+" 小时";
        if(mins>=60)return (mins/60)+" 小时 "+(mins%60)+" 分钟";
        return mins+" 分钟";
    }
    private void usage(){
        JSONArray windows=data.optJSONArray("windows");boolean shown=false;
        if(windows!=null)for(int i=0;i<windows.length();i++){
            JSONArray window=windows.optJSONArray(i);if(window==null||window.optInt(1)<=0)continue;
            usageWindow(window);shown=true;
        }
        if(!shown)usageWindow(new JSONArray());
    }
    private void usageWindow(JSONArray window){
        LinearLayout panel=inside(card(content,color(R.color.container),28),22);
        int minutes=window.optInt(1),remaining=window.optInt(0,-1);long reset=window.optLong(2),now=System.currentTimeMillis()/1000;
        boolean fresh=System.currentTimeMillis()-prefs.getLong("snapshotAt",0)<30000;
        UsagePace pace=fresh?UsagePace.at(remaining,minutes,reset,now):null;
        LinearLayout head=row();head.addView(text(duration(minutes),20,color(R.color.on_surface),true),new LinearLayout.LayoutParams(0,-2,1));
        boolean fast=pace!=null&&pace.elapsed>=1&&pace.difference>5;
        head.addView(tag(pace==null?"等待更新":pace.label,color(fast?R.color.tertiary_container:R.color.secondary_container),color(fast?R.color.on_tertiary_container:R.color.primary)));panel.addView(head);gap(panel,12);
        LinearLayout amount=row();amount.setGravity(Gravity.BOTTOM);amount.addView(text(pace==null?"—":Integer.toString(remaining),56,color(R.color.primary),true));
        TextView unit=text(pace==null?"  额度不可用":"%  剩余",16,color(R.color.on_variant),false);unit.setPadding(0,0,0,dp(9));amount.addView(unit);panel.addView(amount);gap(panel,14);
        if(pace==null){panel.addView(text("等待电脑返回当前周期的有效额度",13,color(R.color.on_variant),false));return;}
        panel.addView(text("额度已用   "+Math.round(pace.used)+"%",13,color(R.color.on_surface),true));gap(panel,8);
        panel.addView(progress((int)Math.round(pace.used),color(fast?R.color.on_tertiary_container:R.color.primary),true),box(-1,14));gap(panel,14);
        panel.addView(text("周期已过   "+Math.round(pace.elapsed)+"%",13,color(R.color.on_variant),true));gap(panel,8);
        panel.addView(progress((int)Math.round(pace.elapsed),color(R.color.on_variant),false),box(-1,8));gap(panel,14);
        String comparison=pace.elapsed<1?"周期刚开始，稍后再比较消耗速度":Math.abs(pace.difference)<=5?"额度消耗与时间进度基本同步":"额度消耗比时间进度"+(pace.difference>0?"快 ":"慢 ")+Math.round(Math.abs(pace.difference))+" 个百分点";
        panel.addView(text(comparison,13,color(R.color.on_variant),false));gap(panel,16);
        panel.addView(text("还有 "+countdown(reset-now)+" 重置",16,color(R.color.on_surface),true));gap(panel,6);
        DateFormat format=DateFormat.getDateTimeInstance(DateFormat.SHORT,DateFormat.SHORT);
        panel.addView(text(format.format(new Date((reset-minutes*60L)*1000))+" → "+format.format(new Date(reset*1000)),11,color(R.color.on_variant),false));
    }
    private static String kind(String k){switch(k){case "completed":return "已完成";case "input":return "等待回复";case "approval":return "需要批准";case "failed":return "执行失败";case "interrupted":return "已中断";default:return "会话更新";}}
    private void notices(int limit){
        JSONArray recent=data.optJSONArray("recent");
        if(recent==null||recent.length()==0){LinearLayout empty=inside(card(content,color(R.color.container),28),24);empty.addView(text("暂时没有新进展",20,color(R.color.on_surface),true));gap(empty,10);empty.addView(text("任务完成或需要你回复时，通知会出现在这里。",14,color(R.color.on_variant),false));return;}
        for(int i=0;i<Math.min(limit,recent.length());i++){
            JSONObject e=recent.optJSONObject(i);if(e==null)continue;
            boolean attention=e.optString("kind").equals("input")||e.optString("kind").equals("approval")||e.optString("kind").equals("failed");
            LinearLayout notice=inside(card(content,color(R.color.container),i==0?26:18),20);
            LinearLayout meta=row();meta.addView(tag(kind(e.optString("kind")),color(attention?R.color.tertiary_container:R.color.secondary_container),color(attention?R.color.on_tertiary_container:R.color.primary)));
            TextView when=text(DateFormat.getTimeInstance(DateFormat.SHORT).format(new Date(e.optLong("time")*1000)),12,color(R.color.on_variant),false);when.setGravity(Gravity.END);meta.addView(when,new LinearLayout.LayoutParams(0,-2,1));notice.addView(meta);gap(notice,14);
            TextView title=text(e.optString("title",e.optString("project")),18,color(R.color.on_surface),true);title.setMaxLines(3);notice.addView(title);
            String body=e.optString("body");if(!body.isEmpty()){gap(notice,8);notice.addView(text(body,14,color(R.color.on_variant),false));}
            gap(notice,12);notice.addView(text(e.optString("project"),12,color(R.color.on_variant),false));
        }
    }
    private void connectionPage(){
        heading("保持连接","手机与 Passport 在一起，就能接收进展。");
        LinearLayout route=inside(card(content,color(R.color.primary_container),28),22);
        route.addView(text(prefs.getBoolean("desktopOwns",false)?"电脑  →  Passport":"电脑  →  手机  →  Passport",19,color(R.color.on_primary_container),true));gap(route,10);
        route.addView(text("离开家时，先开启你已配置的 WireGuard。手机通过蓝牙连接 Passport。",14,color(R.color.on_primary_container),false));
        gap(content,12);content.addView(text("电脑 中继",20,color(R.color.on_surface),true));gap(content,14);
        endpoint=input("中继地址",prefs.getString("endpoint",""),false);
        token=input("配对密钥",prefs.getString("token",""),true);gap(content,12);
        MaterialButton save=button("保存并连接",R.drawable.ic_link,true);save.setOnClickListener(v->{
            String url=endpoint.getText().toString().trim(),secret=token.getText().toString().trim();
            if(!url.startsWith("http://")&&!url.startsWith("https://")){endpoint.setError("请输入完整的 HTTP 或 HTTPS 地址");return;}
            if(secret.length()<20){token.setError("请填写 电脑 中继生成的配对密钥");return;}
            prefs.edit().putString("endpoint",url).putString("token",secret).apply();stopService(new Intent(this,RelayService.class));requestConnect();navigation.setSelectedItemId(1);
        });content.addView(save);gap(content,12);
        MaterialButton stop=button("停止手机同步",R.drawable.ic_pause,false);stop.setOnClickListener(v->pause());content.addView(stop);gap(content,24);
        content.addView(text("蓝牙连接方式",18,color(R.color.on_surface),true));gap(content,8);
        content.addView(text("电脑已配置直连服务时，可在这里切换。电脑连不上会自动回到手机；恢复后不会反复抢连。",14,color(R.color.on_variant),false));gap(content,12);
        MaterialButton phone=button("使用手机转发",R.drawable.ic_link,false);
        phone.setOnClickListener(v->{prefs.edit().putString("routeRequest","phone").apply();requestConnect();});content.addView(phone);gap(content,8);
        MaterialButton desktop=button("使用电脑蓝牙直连",R.drawable.ic_link,false);
        desktop.setEnabled(prefs.getBoolean("desktopAvailable",false));
        desktop.setOnClickListener(v->{prefs.edit().putString("routeRequest","desktop").apply();requestConnect();});content.addView(desktop);gap(content,24);
        content.addView(text("首次连接",18,color(R.color.on_surface),true));gap(content,8);
        content.addView(text("将两台设备放在一起，在手机系统弹窗输入 Passport 屏幕上的六位配对码。",14,color(R.color.on_variant),false));gap(content,20);
        content.addView(text("Passport 图例",18,color(R.color.on_surface),true));gap(content,8);
        content.addView(text("外环：剩余额度 · 内环：剩余时间\n橙色：用得偏快 · 绿色：均衡 · 蓝色：偏慢\n▶ 进行中 · △ 待回复 · 铃铛：未读\n上/下键切换周期，OK 标为已读。\n熄屏后首次按键只唤醒；长按 OK 重连蓝牙。",14,color(R.color.on_variant),false));gap(content,20);
        content.addView(text("后台接收",18,color(R.color.on_surface),true));gap(content,8);
        content.addView(text("持续同步时，系统会显示一条常驻通知。若手机限制后台运行，可在应用电池设置中允许后台活动。",14,color(R.color.on_variant),false));
    }
    private EditText input(String hint,String value,boolean secret){
        TextInputLayout field=new TextInputLayout(this);field.setHint(hint);field.setBoxBackgroundMode(TextInputLayout.BOX_BACKGROUND_OUTLINE);field.setBoxCornerRadii(dp(18),dp(18),dp(18),dp(18));
        TextInputEditText edit=new TextInputEditText(field.getContext());edit.setSingleLine();edit.setInputType(secret?129:17);edit.setText(value);edit.setTextSize(15);field.addView(edit);
        if(secret)field.setEndIconMode(TextInputLayout.END_ICON_PASSWORD_TOGGLE);
        LinearLayout.LayoutParams lp=box(-1,-2);lp.bottomMargin=dp(12);content.addView(field,lp);return edit;
    }
    private void requestConnect(){
        if(prefs.getString("token","").length()<20){navigation.setSelectedItemId(3);return;}
        String[] permissions=Build.VERSION.SDK_INT>=31?(Build.VERSION.SDK_INT>=33?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT,Manifest.permission.POST_NOTIFICATIONS}:new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}):new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        boolean missing=false;for(String p:permissions)if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED)missing=true;
        if(missing)requestPermissions(permissions,1);else startRelay();
    }
    private void startRelay(){Intent intent=new Intent(this,RelayService.class);if(Build.VERSION.SDK_INT>=26)startForegroundService(intent);else startService(intent);prefs.edit().putString("status","正在连接你的 Passport…").putLong("statusAt",System.currentTimeMillis()).apply();update();}
    private void pause(){stopService(new Intent(this,RelayService.class));prefs.edit().putString("status","同步已暂停").putLong("statusAt",System.currentTimeMillis()).apply();update();}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request==1){boolean allowed=true;for(int i=0;i<permissions.length;i++)if(!permissions[i].equals(Manifest.permission.POST_NOTIFICATIONS)&&grants[i]!=PackageManager.PERMISSION_GRANTED)allowed=false;if(allowed)startRelay();else new MaterialAlertDialogBuilder(this).setTitle("需要蓝牙权限").setMessage("允许附近设备权限后，才能连接你的 Passport。").setPositiveButton("知道了",null).show();}}
    @Override protected void onResume(){super.onResume();handler.post(refresh);}
    @Override protected void onPause(){handler.removeCallbacks(refresh);super.onPause();}
    @Override protected void onSaveInstanceState(Bundle b){b.putInt("page",page);super.onSaveInstanceState(b);}
}
