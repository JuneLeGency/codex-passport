package dev.passport.codex;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import org.json.*;

/** Authenticated, bounded requests shared by setup and background synchronization. */
final class RelayClient {
    static String endpoint(String value) throws Exception {
        URI uri=new URI(value.trim());
        if(!("http".equals(uri.getScheme())||"https".equals(uri.getScheme()))||uri.getHost()==null||
           uri.getUserInfo()!=null||uri.getQuery()!=null||uri.getFragment()!=null||
           !(uri.getPath()==null||uri.getPath().isEmpty()||uri.getPath().equals("/"))||uri.getPort()>65535)
            throw new IllegalArgumentException("请填写完整中继地址，例如 http://电脑内网IP:18765");
        String normalized=uri.toString();return normalized.endsWith("/")?normalized.substring(0,normalized.length()-1):normalized;
    }
    static JSONObject request(String base,String token,String path,JSONObject body) throws Exception {
        URL url=new URL(endpoint(base)+path);
        if(!url.getProtocol().equals("https")) {
            for(InetAddress address:InetAddress.getAllByName(url.getHost())) {
                byte[] b=address.getAddress();
                boolean tail=b.length==4&&(b[0]&255)==100&&(b[1]&255)>=64&&(b[1]&255)<=127;
                boolean ula=b.length==16&&((b[0]&0xfe)==0xfc);
                if(!(address.isSiteLocalAddress()||address.isLoopbackAddress()||tail||ula))
                    throw new IOException("HTTP 仅支持内网或 VPN 地址");
            }
        }
        if(token.length()<20)throw new IllegalArgumentException("请复制电脑显示的完整配对密钥");
        HttpURLConnection connection=(HttpURLConnection)url.openConnection();
        connection.setConnectTimeout(5000);connection.setReadTimeout(5000);connection.setInstanceFollowRedirects(false);
        connection.setRequestProperty("Authorization","Bearer "+token);
        try {
            if(body!=null){
                connection.setRequestMethod("POST");connection.setDoOutput(true);connection.setRequestProperty("Content-Type","application/json");
                try(OutputStream out=connection.getOutputStream()){out.write(body.toString().getBytes(StandardCharsets.UTF_8));}
            }
            int code=connection.getResponseCode();
            if(code==401)throw new IOException("配对密钥不正确，请重新复制电脑上的密钥");
            if(code==404)throw new IOException("电脑中继需要升级，请重新执行安装命令");
            if(code!=200)throw new IOException(code==400?"请求未接受，请检查输入或等待上一项设置完成":"中继暂时不可用（"+code+"）");
            ByteArrayOutputStream out=new ByteArrayOutputStream();
            try(InputStream in=connection.getInputStream()){
                byte[] buffer=new byte[1024];int count;
                while((count=in.read(buffer))!=-1){out.write(buffer,0,count);if(out.size()>8192)throw new IOException("中继响应过大");}
            }
            return new JSONObject(out.toString("UTF-8"));
        } finally {connection.disconnect();}
    }
    static String friendly(Exception error){
        if(error instanceof SocketTimeoutException)return "连接超时：确认电脑中继正在运行，手机与电脑同网，或已开启 WireGuard";
        if(error instanceof ConnectException||error instanceof UnknownHostException)return "找不到电脑：检查中继地址、电脑网络和防火墙";
        return error instanceof IllegalArgumentException||error instanceof IOException?error.getMessage():"暂时无法连接，请稍后重试";
    }
}
