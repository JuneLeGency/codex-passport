package dev.passport.codex;

import android.Manifest;
import android.bluetooth.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.*;
import android.view.*;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.*;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import java.util.concurrent.*;

/** A resumable setup flow; steps complete from real connection results. */
public class GuideActivity extends AppCompatActivity {
    private Ui ui;private LinearLayout content;private ScrollView scroll;
    private SharedPreferences prefs;private int step;private boolean busy;
    private EditText endpoint,token;private TextView result;private MaterialButton primary;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Runnable refresh=new Runnable(){public void run(){refreshPairing();handler.postDelayed(this,1000);}};
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);ui=new Ui(this);prefs=getSharedPreferences("relay",0);
        step=saved==null?getIntent().getIntExtra("step",prefs.getInt("guideStep",0)):saved.getInt("step");
        step=Math.max(0,Math.min(4,step));
        WindowCompat.setDecorFitsSystemWindows(getWindow(),false);
        LinearLayout root=ui.column();root.setBackgroundColor(ui.color(R.color.surface));setContentView(root);
        ViewCompat.setOnApplyWindowInsetsListener(root,(v,insets)->{Insets i=insets.getInsets(WindowInsetsCompat.Type.systemBars()|WindowInsetsCompat.Type.ime());v.setPadding(i.left,i.top,i.right,i.bottom);return insets;});
        scroll=new ScrollView(this);scroll.setFillViewport(true);content=ui.column();content.setPadding(ui.dp(24),ui.dp(24),ui.dp(24),ui.dp(24));scroll.addView(content);root.addView(scroll,ui.box(-1,-1));render();
    }
    private void next(int value){step=value;prefs.edit().putInt("guideStep",step).apply();render();scroll.smoothScrollTo(0,0);}
    private void title(String title,String subtitle){ui.gap(content,28);content.addView(ui.text(title,34,true));ui.gap(content,12);content.addView(ui.text(subtitle,16,false));ui.gap(content,26);}
    private void fact(LinearLayout parent,String title,String body){parent.addView(ui.text(title,18,true));ui.gap(parent,8);parent.addView(ui.text(body,14,false));ui.gap(parent,20);}
    private void render(){
        content.removeAllViews();result=null;primary=null;
        LinearLayout top=ui.row();top.addView(ui.text("Passport",23,true),new LinearLayout.LayoutParams(0,-2,1));
        top.addView(ui.text(step==0?"入门指南":Math.min(step,4)+" / 4",14,false));content.addView(top);
        if(step>0){ui.gap(content,18);LinearProgressIndicator progress=new LinearProgressIndicator(this);progress.setMax(4);progress.setProgress(step);progress.setTrackThickness(ui.dp(6));progress.setTrackCornerRadius(ui.dp(3));content.addView(progress,ui.box(-1,8));}
        if(step==0){
            title("把进展\n带在身边","几步连接，让重要通知和额度出现在 Passport 上。");
            LinearLayout hero=ui.card(content,true);
            hero.addView(ui.text("电脑  →  手机  →  Passport",22,true));ui.gap(hero,24);
            fact(hero,"需要你时，轻响一下","待回复、待批准时偶尔短响；任务结果安静地更新在屏幕上。");
            fact(hero,"抬眼就知道进度","大仪表盘看额度，进展页看最近三个会话。");
            ui.button(content,"开始设置",true,v->next(1));
        } else if(step==1){
            title("先准备电脑","电脑负责收集 Codex 进展，使用时保持开机和中继运行。");
            LinearLayout card=ui.card(content,true);
            fact(card,"1  安装中继","在使用 Codex 的电脑上打开终端。已有 uv 时，复制并运行下面的命令。");
            command(card,"uv tool install --upgrade 'codex-passport-sync[ble]'");
            ui.gap(card,10);fact(card,"2  启动并查看连接信息","先安装通知 Hooks，再启动中继。终端会显示地址和配对密钥，下一步填到手机里。已有 Codex 会话需重新打开。");
            command(card,"codex-passport install-hooks\ncodex-passport setup");
            content.addView(ui.text("还没有 uv，或不知道怎么打开终端？图文指南按步骤说明安装方法。",14,false));ui.gap(content,14);
            ui.button(content,"打开电脑安装指南",false,v->startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("https://github.com/JuneLeGency/codex-passport/blob/main/docs/GETTING_STARTED.zh_CN.md"))));
            ui.button(content,"电脑已显示地址与密钥",true,v->next(2));
        } else if(step==2){
            title("连接你的电脑","手机与电脑连接同一网络；远程使用时先开启已配置的 WireGuard。");
            LinearLayout help=ui.card(content,false);fact(help,"找对这两个信息","中继地址通常以 http:// 开头，端口默认为 18765。配对密钥由电脑中继生成，不是蓝牙六位码。不要填写无线 ADB 或 VPN 入口端口。");
            endpoint=ui.input(content,"电脑中继地址",prefs.getString("endpoint",""),false);endpoint.setInputType(17);
            token=ui.input(content,"中继配对密钥",prefs.getString("token",""),true);
            result=ui.text("输入后会先验证电脑连接，不会跳过检查。",14,false);result.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);content.addView(result);ui.gap(content,18);
            primary=ui.button(content,"验证并继续",true,v->verify());primary.setEnabled(!busy);
        } else if(step==3){
            title("让它们认识一下","把手机与 Passport 放在一起，并唤醒 Passport 屏幕。");
            LinearLayout card=ui.card(content,true);
            fact(card,"允许附近设备权限","用于搜索并连接 Passport。较旧 Android 系统还需要定位权限与定位开关。");
            fact(card,"输入屏幕上的六位码","在手机系统配对弹窗中输入 Passport 此刻显示的数字。请保持两台设备靠近。");
            result=ui.text("准备好后，点击下方开始连接。",16,true);result.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);content.addView(result);ui.gap(content,20);
            primary=ui.button(content,"允许权限并连接",true,v->pair());
            ui.button(content,"打开手机蓝牙设置",false,v->startActivity(new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS)));
            content.addView(ui.text("如果旧电脑或手机占着连接，亮屏后长按 Passport 的 OK 键约两秒，再重试。原有配对不会被清除。",14,false));
        } else {
            title("进展已在身边","连接完成。记住这几个按键，就可以开始使用。");
            LinearLayout card=ui.card(content,true);
            fact(card,"短按上 / 下：切换两页","仪表盘看额度，进展页看最近三个会话；长按上 / 下切换额度周期。");
            fact(card,"短按 OK：标为已读","不会批准或执行任务。熄屏后的第一次按键只唤醒。");
            fact(card,"双击 OK：声音开关","新版固件支持静音；长按 OK 重连蓝牙，保留配对。");
            LinearLayout legend=ui.card(content,false);fact(legend,"外环是额度，内环是时间","橙色表示用得偏快，绿色表示与时间基本同步，蓝色表示用得偏慢。");
            content.addView(ui.text("在「设备」里调声音、亮度和熄屏时间。Wi-Fi 直连是可选项，可稍后设置。外出时保持 WireGuard 和手机后台同步运行。",14,false));ui.gap(content,20);
            ui.button(content,"开始使用",true,v->{prefs.edit().putBoolean("setupComplete",true).putInt("guideStep",0).apply();finish();});
        }
        if(step<4)ui.button(content,"稍后继续",false,v->finish());
        if(step>0)ui.button(content,"上一步",false,v->next(step-1));
    }
    private void command(LinearLayout parent,String value){
        TextView code=ui.text(value,14,true);code.setTypeface(android.graphics.Typeface.MONOSPACE);code.setTextIsSelectable(true);parent.addView(code);ui.gap(parent,12);
        ui.button(parent,"复制命令",false,v->{((ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("Passport setup",value));Toast.makeText(this,"命令已复制",Toast.LENGTH_SHORT).show();});
    }
    private void verify(){
        if(busy)return;final String address,secret=token.getText().toString().trim();
        try{address=RelayClient.endpoint(endpoint.getText().toString());}catch(Exception e){endpoint.setError(RelayClient.friendly(e));return;}
        if(secret.length()<20){token.setError("请复制完整配对密钥");return;}
        busy=true;primary.setEnabled(false);result.setText("正在验证电脑中继…");
        worker.execute(()->{try{
            RelayClient.request(address,secret,"/v1/ping",null);
            handler.post(()->{if(isFinishing()||isDestroyed())return;busy=false;
                stopService(new Intent(this,RelayService.class));
                prefs.edit().putString("endpoint",address).putString("token",secret).apply();next(3);});
        }catch(Exception e){handler.post(()->{if(isFinishing()||isDestroyed())return;busy=false;if(step==2){primary.setEnabled(true);result.setText(RelayClient.friendly(e));}});}});
    }
    private void pair(){
        if(step!=3)return;
        String[] permissions=Build.VERSION.SDK_INT>=31?(Build.VERSION.SDK_INT>=33?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT,Manifest.permission.POST_NOTIFICATIONS}:new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}):new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        boolean missing=false;for(String permission:permissions)if(checkSelfPermission(permission)!=PackageManager.PERMISSION_GRANTED)missing=true;
        if(missing){requestPermissions(permissions,20);return;}
        connect();
    }
    private void connect(){
        BluetoothAdapter adapter=((BluetoothManager)getSystemService(BLUETOOTH_SERVICE)).getAdapter();
        if(adapter==null){result.setText("这台手机没有可用的蓝牙适配器");return;}
        if(!adapter.isEnabled()){result.setText("请开启蓝牙后，再点击连接");startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));return;}
        prefs.edit().putString("routeRequest","phone").putString("status","正在连接你的 Passport…").putLong("statusAt",System.currentTimeMillis()).apply();
        Intent intent=new Intent(this,RelayService.class);if(Build.VERSION.SDK_INT>=26)startForegroundService(intent);else startService(intent);
        result.setText("正在搜索 Passport，请留意手机配对弹窗…");
    }
    private void refreshPairing(){
        if(step!=3||result==null)return;
        String status=prefs.getString("status","");
        if(System.currentTimeMillis()-prefs.getLong("statusAt",0)>30000)return;
        if(status.equals("同步正常 · 电脑 → 手机 → BLE")) {
            result.setText("已收到 Passport 的同步回执");primary.setText("连接成功，认识一下按键");primary.setOnClickListener(v->next(4));
        } else if(!status.isEmpty()){result.setText(status);primary.setText("重新尝试连接");primary.setOnClickListener(v->pair());}
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){
        super.onRequestPermissionsResult(request,permissions,grants);
        if(request!=20)return;boolean allowed=grants.length==permissions.length&&grants.length>0;
        for(int i=0;i<grants.length;i++)if(!permissions[i].equals(Manifest.permission.POST_NOTIFICATIONS)&&grants[i]!=PackageManager.PERMISSION_GRANTED)allowed=false;
        if(allowed)connect();else if(result!=null)result.setText("连接需要附近设备权限。可在系统的应用权限中允许后重试。");
    }
    @Override public void onBackPressed(){if(step>0)next(step-1);else super.onBackPressed();}
    @Override protected void onResume(){super.onResume();handler.post(refresh);}
    @Override protected void onPause(){handler.removeCallbacks(refresh);super.onPause();}
    @Override protected void onDestroy(){worker.shutdownNow();super.onDestroy();}
    @Override protected void onSaveInstanceState(Bundle saved){saved.putInt("step",step);super.onSaveInstanceState(saved);}
}
