package dev.passport.codex;

import android.content.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.*;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.materialswitch.MaterialSwitch;
import com.google.android.material.slider.Slider;
import org.json.*;
import java.util.concurrent.*;

/** Device preferences stay visible while asynchronous receipts update status. */
public class DeviceActivity extends AppCompatActivity {
    private Ui ui;private SharedPreferences prefs;private LinearLayout content;
    private Slider brightness,volume,idle;private MaterialSwitch sound;
    private TextView receipt,route,battery;private MaterialButton apply,find,savedWifi;
    private boolean busy,waitingWifi;private int requestId;private long requestedAt;
    private String requestOp="";
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Runnable refresh=new Runnable(){public void run(){update();handler.postDelayed(this,1500);}};
    private JSONObject state(){try{return new JSONObject(prefs.getString("deviceState","{}"));}catch(Exception e){return new JSONObject();}}
    private JSONObject settings(){JSONObject s=state().optJSONObject("settings");return s==null?new JSONObject():s;}
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);ui=new Ui(this);prefs=getSharedPreferences("relay",0);
        if(saved!=null){requestId=saved.getInt("requestId");requestedAt=saved.getLong("requestedAt");requestOp=saved.getString("requestOp","");waitingWifi=saved.getBoolean("waitingWifi");}
        WindowCompat.setDecorFitsSystemWindows(getWindow(),false);
        LinearLayout root=ui.column();root.setBackgroundColor(ui.color(R.color.surface));setContentView(root);
        ViewCompat.setOnApplyWindowInsetsListener(root,(v,insets)->{Insets i=insets.getInsets(WindowInsetsCompat.Type.systemBars()|WindowInsetsCompat.Type.ime());v.setPadding(i.left,i.top,i.right,i.bottom);return insets;});
        ScrollView scroll=new ScrollView(this);content=ui.column();content.setPadding(ui.dp(24),ui.dp(20),ui.dp(24),ui.dp(24));scroll.addView(content);root.addView(scroll,ui.box(-1,-1));
        ui.button(content,"返回工作台",false,v->finish());ui.gap(content,16);
        content.addView(ui.text("你的 Passport",32,true));ui.gap(content,10);content.addView(ui.text("让提醒、屏幕和连接适合你的日常。",16,false));ui.gap(content,24);
        LinearLayout hero=ui.card(content,true);battery=ui.text("正在读取设备…",26,true);hero.addView(battery);ui.gap(hero,10);route=ui.text("",14,false);hero.addView(route);
        JSONObject current=settings();
        LinearLayout audio=ui.card(content,false);audio.addView(ui.text("安静地提醒",22,true));ui.gap(audio,12);
        sound=new MaterialSwitch(this);sound.setText("需要我处理时短响");sound.setTextSize(16);sound.setChecked(!current.optBoolean("muted",false));audio.addView(sound,ui.box(-1,56));
        audio.addView(ui.text("仅等待回复或需要批准时响，至少间隔两分钟。普通完成只更新列表，不反复亮屏。静音后，待处理颜色和未读标记仍会保留。",14,false));ui.gap(audio,16);
        volume=slider(audio,"提示音音量",current.optInt("volume",65),20,80,5,"%");
        LinearLayout display=ui.card(content,false);display.addView(ui.text("舒适的屏幕",22,true));ui.gap(display,16);
        brightness=slider(display,"屏幕亮度",current.optInt("brightness",55),10,80,5,"%");
        idle=slider(display,"自动熄屏",current.optInt("idle",60),15,120,15," 秒");
        display.addView(ui.text("电量不超过 10% 时，背光最多 20%，亮屏最多 30 秒。首次按键只唤醒，之后再操作。",14,false));
        LinearLayout presets=ui.row();
        MaterialButton quiet=new MaterialButton(this);quiet.setText("安静阅读");quiet.setOnClickListener(v->{sound.setChecked(false);volume.setValue(35);brightness.setValue(25);idle.setValue(30);});presets.addView(quiet,new LinearLayout.LayoutParams(0,ui.dp(52),1));
        MaterialButton daily=new MaterialButton(this);daily.setText("日常使用");daily.setOnClickListener(v->{sound.setChecked(true);volume.setValue(50);brightness.setValue(55);idle.setValue(60);});presets.addView(daily,new LinearLayout.LayoutParams(0,ui.dp(52),1));content.addView(presets);ui.gap(content,14);
        receipt=ui.text("调整后点击应用，收到设备回执才会确认保存。",14,false);receipt.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);content.addView(receipt);ui.gap(content,14);
        apply=ui.button(content,"应用到 Passport",true,v->{try{send(new JSONObject().put("op","settings").put("brightness",Math.round(brightness.getValue())).put("idle",Math.round(idle.getValue())).put("volume",Math.round(volume.getValue())).put("muted",!sound.isChecked()));}catch(Exception ignored){}});
        find=ui.button(content,"找一下 Passport",false,v->{try{send(new JSONObject().put("op","identify"));}catch(Exception ignored){}});
        content.addView(ui.text("查找会亮屏并短响；设备已静音时仅亮屏。",13,false));ui.gap(content,28);
        LinearLayout wifi=ui.card(content,true);wifi.addView(ui.text("Wi-Fi 直连",24,true));ui.gap(wifi,10);
        wifi.addView(ui.text("可选的桌面连接方式。Passport 与电脑在同一局域网时，可直接同步，无需手机一直转发。只支持 2.4GHz 网络。",14,false));ui.gap(wifi,18);
        ui.button(wifi,"设置 Wi-Fi",true,v->wifiDialog());
        savedWifi=ui.button(wifi,"使用已保存的 Wi-Fi",false,v->{try{send(new JSONObject().put("op","wifi").put("enabled",true).put("saved",true));}catch(Exception ignored){}});
        ui.button(wifi,"返回手机转发",false,v->phoneRoute());
        ui.button(wifi,"忘记 Passport 的 Wi-Fi",false,v->new MaterialAlertDialogBuilder(this).setTitle("忘记设备上的网络？").setMessage("会清除 Passport 保存的 Wi-Fi 密码与直连信息，蓝牙配对保留。").setNegativeButton("取消",null).setPositiveButton("忘记",(d,w)->{try{send(new JSONObject().put("op","forget_wifi"));}catch(Exception ignored){}}).show());
        wifi.addView(ui.text("直连失败会回到手机转发。离开这个网络后，请开启手机 WireGuard 并保持后台同步。Wi-Fi 模式不代替手机上的 VPN。",14,false));
        ui.button(content,"重新查看入门指南",false,v->startActivity(new Intent(this,GuideActivity.class).putExtra("step",0)));
    }
    private Slider slider(LinearLayout parent,String label,int value,int min,int max,int step,String unit){
        TextView title=ui.text(label+"  "+value+unit,16,true);parent.addView(title);ui.gap(parent,6);
        Slider slider=new Slider(this);slider.setValueFrom(min);slider.setValueTo(max);slider.setStepSize(step);slider.setValue(Math.max(min,Math.min(max,min+Math.round((value-min)/(float)step)*step)));
        slider.setContentDescription(label);slider.addOnChangeListener((s,v,user)->title.setText(label+"  "+Math.round(v)+unit));parent.addView(slider,ui.box(-1,56));ui.gap(parent,16);return slider;
    }
    private boolean ready(){return !settings().optString("fw").isEmpty()&&System.currentTimeMillis()/1000-state().optLong("seenAt",0)<30;}
    private void update(){
        JSONObject current=settings(),status=state().optJSONObject("command");
        int level=current.optInt("battery",-1);battery.setText(level<0?"Passport 尚未连接":"电量 "+level+"%");
        String owner=prefs.getString("routeOwner","phone"),linkState=prefs.getString("routeState","");
        boolean wifiConnected=owner.equals("wifi")&&linkState.equals("connected")&&ready();
        route.setText((owner.equals("wifi")?(wifiConnected?"Wi-Fi 直连 · 同步正常":"Wi-Fi 正在连接…"):owner.equals("desktop")?"电脑蓝牙直连":"手机蓝牙转发")+" · "+current.optString("fw","等待新版固件回执"));
        if(requestId!=0 && status!=null && status.optInt("id")==requestId){
            switch(status.optString("state")){
                case "applied":waitingWifi=requestOp.equals("wifi");receipt.setText(waitingWifi?"网络配置已保存，正在等待联网回执…":"已应用 · Passport 已确认保存或执行");requestId=0;break;
                case "failed":receipt.setText("设备未能应用，请检查连接或重新设置网络");requestId=0;break;
                case "expired":receipt.setText("暂未收到设备回执，设置没有被确认，请连接后重试");requestId=0;break;
                default:receipt.setText("已送出，正在等待 Passport 确认…");
            }
        }
        if(waitingWifi){
            if(wifiConnected){receipt.setText("Wi-Fi 已连接 · Passport 已通过直连确认同步");waitingWifi=false;}
            else if(System.currentTimeMillis()-requestedAt>60000&&!owner.equals("wifi")){receipt.setText("Wi-Fi 未能建立同步，已返回手机转发；网络信息已保留");waitingWifi=false;}
        }
        if(requestId!=0 && System.currentTimeMillis()-requestedAt>100000){requestId=0;receipt.setText("等待已超时，请确认 Passport 已连接后重试");}
        boolean allowed=ready()&&!busy&&requestId==0;
        apply.setEnabled(allowed);find.setEnabled(allowed);savedWifi.setEnabled(allowed&&current.optBoolean("configured"));
        if(!ready()&&requestId==0&&!busy&&!waitingWifi)receipt.setText("先连接 Passport。设备设置需要新版固件与电脑中继。");
    }
    private void send(JSONObject command){
        if(busy||requestId!=0){receipt.setText("请等待上一项设置的设备回执");return;}
        if(!ready()){receipt.setText("请先连接 Passport，并确认已升级固件和电脑中继");return;}
        busy=true;waitingWifi=false;requestOp=command.optString("op");apply.setEnabled(false);receipt.setText("正在发送设置…");
        final String address=prefs.getString("endpoint",""),token=prefs.getString("token","");
        worker.execute(()->{try{
            JSONObject reply=RelayClient.request(address,token,"/v1/device/command",command);
            handler.post(()->{if(isFinishing()||isDestroyed())return;busy=false;requestId=reply.optInt("id");requestedAt=System.currentTimeMillis();receipt.setText("已送出，正在等待 Passport 确认…");});
        }catch(Exception e){handler.post(()->{if(isFinishing()||isDestroyed())return;busy=false;receipt.setText(RelayClient.friendly(e));apply.setEnabled(ready());});}});
    }
    private void wifiDialog(){
        if(!ready()){receipt.setText("请先让 Passport 同步成功，再配置 Wi-Fi");return;}
        LinearLayout form=ui.column();form.setPadding(ui.dp(24),ui.dp(12),ui.dp(24),0);
        form.addView(ui.text("使用 WPA2 / WPA3 兼容的 2.4GHz 网络。电脑中继地址需要是同一局域网的 HTTP IPv4 地址。密码只发到设备，不保存在手机设置中。",14,false));ui.gap(form,16);
        EditText ssid=ui.input(form,"Wi-Fi 名称","",false),password=ui.input(form,"Wi-Fi 密码","",true);
        androidx.appcompat.app.AlertDialog dialog=new MaterialAlertDialogBuilder(this).setTitle("连接 Wi-Fi").setView(form).setNegativeButton("取消",null).setPositiveButton("发送到 Passport",null).create();
        dialog.setOnShowListener(d->dialog.getButton(-1).setOnClickListener(v->{
            String name=ssid.getText().toString(),secret=password.getText().toString();
            if(name.isEmpty()||name.getBytes(java.nio.charset.StandardCharsets.UTF_8).length>32){ssid.setError("网络名称需要 1–32 字节");return;}
            int length=secret.getBytes(java.nio.charset.StandardCharsets.UTF_8).length;
            if(length<8||length>63){password.setError("密码需要 8–63 字节");return;}
            try{send(new JSONObject().put("op","wifi").put("enabled",true).put("ssid",name).put("password",secret).put("endpoint",prefs.getString("endpoint","")));password.setText("");dialog.dismiss();}catch(Exception ignored){}
        }));dialog.show();
    }
    private void phoneRoute(){
        prefs.edit().putString("routeRequest","phone").apply();
        Intent intent=new Intent(this,RelayService.class);try{if(Build.VERSION.SDK_INT>=26)startForegroundService(intent);else startService(intent);receipt.setText("正在返回手机转发…");}catch(Exception e){receipt.setText("请回到概览连接 Passport，并允许附近设备权限");}
    }
    @Override protected void onResume(){super.onResume();handler.post(refresh);}
    @Override protected void onPause(){handler.removeCallbacks(refresh);super.onPause();}
    @Override protected void onDestroy(){worker.shutdownNow();super.onDestroy();}
    @Override protected void onSaveInstanceState(Bundle saved){saved.putInt("requestId",requestId);saved.putLong("requestedAt",requestedAt);saved.putString("requestOp",requestOp);saved.putBoolean("waitingWifi",waitingWifi);super.onSaveInstanceState(saved);}
}
