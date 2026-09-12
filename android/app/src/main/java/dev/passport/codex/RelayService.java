package dev.passport.codex;

import android.app.*;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.*;
import android.os.*;
import java.util.*;
import java.util.concurrent.*;
import java.net.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import org.json.*;

public class RelayService extends Service {
    static final UUID SERVICE=UUID.fromString("fac06f64-6578-2d70-6173-73706f727401");
    static final UUID RX=UUID.fromString("fac06f64-6578-2d70-6173-73706f727402");
    static final UUID TX=UUID.fromString("fac06f64-6578-2d70-6173-73706f727403");
    static final UUID CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private final Handler handler=new Handler(Looper.getMainLooper());
    private ScheduledExecutorService worker;
    private BluetoothAdapter adapter;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rx;
    private final ArrayDeque<byte[]> chunks=new ArrayDeque<>();
    private StringBuilder incoming=new StringBuilder();
    private boolean scanning, ready, stopped;
    private int mtu=23, pendingSeq, pendingEvent;
    private long sentAt, scanAt, lastNetwork;
    private boolean desktopOwns;
    private String signature="";
    private BluetoothDevice selected;
    private android.content.SharedPreferences prefs;
    private PowerManager.WakeLock wake;
    private String base, auth;

    private void status(String message) {
        prefs.edit().putString("status",message).putLong("statusAt",System.currentTimeMillis()).apply();
    }
    @Override public void onCreate() {
        super.onCreate();
        prefs=getSharedPreferences("relay",0);
        adapter=((BluetoothManager)getSystemService(BLUETOOTH_SERVICE)).getAdapter();
        wake=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"CodexPassport:transfer");
        IntentFilter filter=new IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        if(Build.VERSION.SDK_INT>=33) registerReceiver(bondReceiver,filter,Context.RECEIVER_EXPORTED);
        else registerReceiver(bondReceiver,filter);
    }
    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        base=prefs.getString("endpoint",""); auth=prefs.getString("token","");
        if(Build.VERSION.SDK_INT>=26) {
            NotificationChannel ch=new NotificationChannel("relay","Passport 后台同步",NotificationManager.IMPORTANCE_LOW);
            ((NotificationManager)getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(ch);
        }
        Notification.Builder n=Build.VERSION.SDK_INT>=26 ? new Notification.Builder(this,"relay") : new Notification.Builder(this);
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        startForeground(1,n.setContentTitle("Codex Passport").setContentText("通过私有网络与 BLE 同步").setSmallIcon(android.R.drawable.stat_notify_sync).setContentIntent(open).build());
        if(worker==null) {
            worker=Executors.newSingleThreadScheduledExecutor();
            worker.scheduleWithFixedDelay(this::poll,0,3,TimeUnit.SECONDS);
        }
        handler.post(this::ensureBle);
        return START_STICKY;
    }
    private JSONObject request(String path,JSONObject body) throws Exception {
        URL url=new URL(base+path);
        // Plain HTTP is accepted only for private/local VPN destinations, never public hosts.
        if(!url.getProtocol().equals("https")) {
            InetAddress address=InetAddress.getByName(url.getHost());
            byte[] b=address.getAddress();
            boolean tail=b.length==4 && (b[0]&255)==100 && (b[1]&255)>=64 && (b[1]&255)<=127;
            boolean ula=b.length==16 && ((b[0]&0xfe)==0xfc);
            if(!url.getProtocol().equals("http") || !(address.isSiteLocalAddress()||address.isLoopbackAddress()||tail||ula)) throw new IOException("Private VPN address required");
        }
        if(auth.length()<20) throw new IOException("Pairing token missing");
        HttpURLConnection c=(HttpURLConnection)url.openConnection();
        c.setConnectTimeout(5000); c.setReadTimeout(5000); c.setInstanceFollowRedirects(false);
        c.setRequestProperty("Authorization","Bearer "+auth);
        try {
            if(body!=null) {
                c.setRequestMethod("POST"); c.setDoOutput(true); c.setRequestProperty("Content-Type","application/json");
                try(OutputStream out=c.getOutputStream()) {out.write(body.toString().getBytes(StandardCharsets.UTF_8));}
            }
            if(c.getResponseCode()!=200) throw new IOException("Relay HTTP "+c.getResponseCode());
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();
            try(InputStream in=c.getInputStream()) {
                byte[] buf=new byte[1024]; int count;
                while((count=in.read(buf))!=-1) {bytes.write(buf,0,count);if(bytes.size()>8192) throw new IOException("Oversized relay response");}
            }
            return new JSONObject(bytes.toString("UTF-8"));
        } finally {c.disconnect();}
    }
    private static String kindLabel(String kind) {
        switch(kind) {
            case "completed": return "已完成";
            case "approval": return "需要批准";
            case "input": return "等待你的回复";
            case "interrupted": return "已中断";
            case "failed": return "执行失败";
            default: return "会话更新";
        }
    }
    private void poll() {
        if(stopped) return;
        try {
            long event=prefs.getLong("ackEvent",0), read=prefs.getLong("ackRead",0);
            if(event>0||read>0) {
                request("/v1/ack",new JSONObject().put("event",event).put("read",read));
                // Don't clear newer receipts written by a BLE callback during this request.
                synchronized(this) {
                    android.content.SharedPreferences.Editor e=prefs.edit();
                    if(prefs.getLong("ackEvent",0)==event)e.remove("ackEvent");
                    if(prefs.getLong("ackRead",0)==read)e.remove("ackRead");
                    e.apply();
                }
            }
            String requested=prefs.getString("routeRequest","");
            if(!requested.isEmpty()) {
                request("/v1/route",new JSONObject().put("owner",requested));
                if(prefs.getString("routeRequest","").equals(requested))prefs.edit().remove("routeRequest").apply();
            }
            JSONObject snapshot=request("/v1/snapshot",null);
            lastNetwork=SystemClock.elapsedRealtime();
            JSONObject link=snapshot.optJSONObject("_link");
            boolean direct=link!=null&&link.optString("owner").equals("desktop");
            String directState=link==null?"":link.optString("state");
            prefs.edit().putBoolean("desktopAvailable",link!=null&&link.optBoolean("desktopAvailable")).putBoolean("desktopOwns",direct).apply();
            snapshot.remove("_link");
            JSONArray recent=snapshot.optJSONArray("recent");
            StringBuilder lines=new StringBuilder();
            if(recent!=null) for(int i=0;i<recent.length();i++) {
                JSONObject e=recent.getJSONObject(i);
                lines.append(e.optString("title",e.optString("project"))).append(" · ").append(kindLabel(e.optString("kind"))).append("\n");
                String body=e.optString("body");
                if(!body.isEmpty())lines.append(body).append("\n");
                lines.append("\n");
            }
            JSONObject display=new JSONObject(snapshot.toString());display.remove("seq");display.remove("now");
            prefs.edit().putString("inbox",lines.toString()).putString("snapshot",display.toString()).putLong("snapshotAt",System.currentTimeMillis()).apply();
            handler.post(()->{
                desktopOwns=direct;
                if(direct) {
                    stopScan();disconnect();
                    status(directState.equals("connected")?"同步正常 · 电脑蓝牙直连 Passport":"正在切换到电脑蓝牙 · 失败后自动回到手机");
                } else {sendSnapshot(snapshot);ensureBle();}
            });
        } catch(Exception error) {
            status("电脑 未连接 · 请检查 WireGuard / 地址 / 密钥");
            handler.post(()->{
                desktopOwns=false;
                if(SystemClock.elapsedRealtime()-lastNetwork>15000){stopScan();disconnect();}
                else ensureBle();
            });
        }
    }
    private void ensureBle() {
        if(stopped||desktopOwns) return;
        if(adapter==null||!adapter.isEnabled()){status("请开启手机蓝牙");return;}
        long now=SystemClock.elapsedRealtime();
        if(pendingSeq!=0&&now-sentAt>15000){disconnect();status("BLE 回执超时，重新连接");}
        if(gatt!=null||scanning)return;
        if(now-scanAt<10000)return;
        scanAt=now; scanning=true; status("正在搜索 Passport…");
        try {
            List<ScanFilter> filters=Collections.singletonList(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build());
            adapter.getBluetoothLeScanner().startScan(filters,new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_POWER).build(),scanner);
            handler.postDelayed(()->{if(scanning){adapter.getBluetoothLeScanner().stopScan(scanner);scanning=false;}},8000);
        } catch(Exception error){scanning=false;status("无法扫描 · 请检查蓝牙/定位权限");}
    }
    private final ScanCallback scanner=new ScanCallback() {
        @Override public void onScanResult(int type,ScanResult result) {
            handler.post(()->{
                if(!scanning||desktopOwns)return;
                adapter.getBluetoothLeScanner().stopScan(this);scanning=false;selected=result.getDevice();
                connect(selected);
            });
        }
        @Override public void onScanFailed(int code){scanning=false;status("蓝牙扫描失败："+code);}
    };
    private final BroadcastReceiver bondReceiver=new BroadcastReceiver() {
        @Override public void onReceive(Context c,Intent i) {
            BluetoothDevice d=i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            if(selected!=null&&d!=null&&d.getAddress().equals(selected.getAddress())&&d.getBondState()==BluetoothDevice.BOND_BONDED)
                handler.post(()->{if(gatt==null)connect(d);else if(!gatt.requestMtu(185))gatt.discoverServices();});
        }
    };
    private void stopScan() {
        if(scanning&&adapter!=null)try{adapter.getBluetoothLeScanner().stopScan(scanner);}catch(Exception ignored){}
        scanning=false;
    }
    private void connect(BluetoothDevice d) {
        status("正在建立加密 BLE 连接…");
        gatt=d.connectGatt(this,false,callback,BluetoothDevice.TRANSPORT_LE);
        BluetoothGatt attempt=gatt;
        handler.postDelayed(()->{if(!ready&&gatt==attempt&&gatt!=null){disconnect();status("BLE 连接超时，稍后重试");}},150000);
    }
    private final BluetoothGattCallback callback=new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt g,int status,int state) {
            handler.post(()->{
                if(g!=gatt)return;
                if(status==BluetoothGatt.GATT_SUCCESS&&state==BluetoothProfile.STATE_CONNECTED) {
                    if(g.getDevice().getBondState()!=BluetoothDevice.BOND_BONDED) {
                        RelayService.this.status("请在手机输入 Passport 显示的六位配对码");
                        g.getDevice().createBond();
                    } else if(!g.requestMtu(185))g.discoverServices();
                } else disconnect();
            });
        }
        @Override public void onMtuChanged(BluetoothGatt g,int size,int status) {handler.post(()->{if(g==gatt){mtu=status==0?size:23;g.discoverServices();}});}
        @Override public void onServicesDiscovered(BluetoothGatt g,int status) {
            handler.post(()->{
                if(g!=gatt)return;
                BluetoothGattService s=g.getService(SERVICE);
                if(status!=0||s==null){disconnect();return;}
                rx=s.getCharacteristic(RX); BluetoothGattCharacteristic tx=s.getCharacteristic(TX);
                if(rx==null||tx==null){disconnect();return;}
                g.setCharacteristicNotification(tx,true);
                BluetoothGattDescriptor d=tx.getDescriptor(CCCD);
                if(d==null){disconnect();return;}
                d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                g.writeDescriptor(d);
            });
        }
        @Override public void onDescriptorWrite(BluetoothGatt g,BluetoothGattDescriptor d,int status) {
            handler.post(()->{if(g==gatt&&status==0){ready=true;g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_LOW_POWER);RelayService.this.status("BLE 已连接 · 等待电脑数据");}else if(g==gatt)disconnect();});
        }
        @Override public void onCharacteristicWrite(BluetoothGatt g,BluetoothGattCharacteristic c,int status) {
            handler.post(()->{if(g!=gatt)return;if(status!=0){disconnect();return;}writeNext();});
        }
        @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c) {
            byte[] bytes=c.getValue().clone();
            handler.post(()->{if(g==gatt)receive(bytes);});
        }
    };
    private void sendSnapshot(JSONObject data) {
        if(!ready||pendingSeq!=0)return;
        try {
            data=new JSONObject(data.toString());
            JSONArray threads=data.optJSONArray("_threads");
            if(threads==null)threads=data.optJSONArray("recent");
            JSONArray recent=new JSONArray();
            if(threads!=null)for(int i=0;i<Math.min(3,threads.length());i++)recent.put(threads.getJSONObject(i));
            data.remove("_threads");data.put("recent",recent);
            JSONObject event=data.optJSONObject("event");
            if(event!=null)data.put("event",new JSONObject().put("id",event.getInt("id"))
                .put("kind",event.getString("kind")).put("project","").put("time",event.getLong("time")));
            JSONObject comparable=new JSONObject(data.toString());comparable.remove("seq");comparable.remove("now");
            String sig=comparable.toString();
            if(sig.equals(signature)&&SystemClock.elapsedRealtime()-sentAt<10000)return;
            byte[] frame=(data.toString()+"\n").getBytes(StandardCharsets.UTF_8);
            if(frame.length>2048)return;
            pendingSeq=data.getInt("seq");pendingEvent=data.optJSONObject("event")==null?0:data.getJSONObject("event").optInt("id");
            signature=sig;sentAt=SystemClock.elapsedRealtime();wake.acquire(15000);
            for(int offset=0;offset<frame.length;offset+=mtu-3)chunks.add(Arrays.copyOfRange(frame,offset,Math.min(frame.length,offset+mtu-3)));
            writeNext();
        } catch(Exception e){disconnect();}
    }
    private void writeNext() {
        if(!ready||chunks.isEmpty())return;
        rx.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);rx.setValue(chunks.remove());
        if(!gatt.writeCharacteristic(rx))disconnect();
    }
    private void receive(byte[] bytes) {
        incoming.append(new String(bytes,StandardCharsets.UTF_8));
        int newline;
        while((newline=incoming.indexOf("\n"))>=0) {
            String line=incoming.substring(0,newline);incoming.delete(0,newline+1);
            try {
                JSONObject reply=new JSONObject(line);
                if(reply.optInt("v")!=1)continue;
                if(reply.optInt("ack")==pendingSeq&&pendingSeq!=0&&reply.optInt("event")==pendingEvent) {
                    synchronized(this){if(pendingEvent>0)prefs.edit().putLong("ackEvent",pendingEvent).apply();}
                    pendingSeq=0;pendingEvent=0;if(wake.isHeld())wake.release();
                    status("同步正常 · 电脑 → 手机 → BLE");
                }
                if(reply.has("read"))synchronized(this){prefs.edit().putLong("ackRead",Math.max(prefs.getLong("ackRead",0),reply.getLong("read"))).apply();}
            } catch(Exception ignored){}
        }
        if(incoming.length()>4096)incoming.setLength(0);
    }
    private void disconnect() {
        ready=false;pendingSeq=0;pendingEvent=0;signature="";chunks.clear();incoming.setLength(0);
        BluetoothGatt old=gatt;gatt=null;if(old!=null){old.disconnect();old.close();}
        if(wake.isHeld())wake.release();
    }
    @Override public void onDestroy() {
        stopped=true;handler.removeCallbacksAndMessages(null);
        if(worker!=null)worker.shutdownNow();
        if(scanning&&adapter!=null)try{adapter.getBluetoothLeScanner().stopScan(scanner);}catch(Exception ignored){}
        unregisterReceiver(bondReceiver);disconnect();super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent){return null;}
}
