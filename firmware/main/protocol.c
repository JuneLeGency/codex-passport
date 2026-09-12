#include "protocol.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

bool passport_private_endpoint(const char *url)
{
    unsigned a,b,c,d,port;int consumed=0;char canonical[97];
    if(!url || sscanf(url,"http://%u.%u.%u.%u:%u%n",&a,&b,&c,&d,&port,&consumed)!=5 ||
       url[consumed] || a>255 || b>255 || c>255 || d>255 || !port || port>65535)return false;
    snprintf(canonical,sizeof(canonical),"http://%u.%u.%u.%u:%u",a,b,c,d,port);
    if(strcmp(url,canonical))return false;
    return a==10 || (a==172 && b>=16 && b<=31) || (a==192 && b==168);
}

static bool number(const cJSON *o, const char *key, double low, double high, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
        v->valuedouble < low || v->valuedouble > high || floor(v->valuedouble) != v->valuedouble) return false;
    *out = v->valuedouble;
    return true;
}
static bool text(const cJSON *o, const char *key, char *out, size_t capacity)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= capacity) return false;
    const unsigned char *p=(const unsigned char *)v->valuestring;
    while(*p) {
        uint32_t cp=*p++; unsigned rest=0; uint32_t min=0;
        if(cp<0x80) { if(cp<32 || cp==127) return false; continue; }
        if(cp>=0xc2 && cp<=0xdf) { cp&=0x1f; rest=1; min=0x80; }
        else if(cp>=0xe0 && cp<=0xef) { cp&=0xf; rest=2; min=0x800; }
        else if(cp>=0xf0 && cp<=0xf4) { cp&=7; rest=3; min=0x10000; }
        else return false;
        for(unsigned i=0;i<rest;++i) { if((*p&0xc0)!=0x80) return false; cp=(cp<<6)|(*p++&0x3f); }
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
    }
    strcpy(out, v->valuestring);
    return true;
}
static bool notice(const cJSON *o, passport_notice_t *out)
{
    double n;
    if (!cJSON_IsObject(o) || !number(o,"id",1,2147483647,&n)) return false;
    out->id = (uint32_t)n;
    if (!text(o,"kind",out->kind,sizeof(out->kind)) || !text(o,"project",out->project,sizeof(out->project)) ||
        !number(o,"time",0,9007199254740991.0,&n)) return false;
    if(cJSON_HasObjectItem(o,"title") && !text(o,"title",out->title,sizeof(out->title))) return false;
    if(cJSON_HasObjectItem(o,"body") && !text(o,"body",out->body,sizeof(out->body))) return false;
    out->time = (uint64_t)n;
    return true;
}
static bool command(const cJSON *o,passport_command_t *out)
{
    if(!o || cJSON_IsNull(o))return true;
    double n;char op[20];
    if(!cJSON_IsObject(o) || !number(o,"id",1,2147483647,&n) || !text(o,"op",op,sizeof(op)))return false;
    out->id=(uint32_t)n;
    if(!strcmp(op,"settings")) {
        out->kind=COMMAND_SETTINGS;
        if(!number(o,"brightness",10,80,&n))return false;
        out->brightness=n;
        if(!number(o,"idle",15,120,&n))return false;
        out->idle=n;
        if(!number(o,"volume",20,80,&n))return false;
        out->volume=n;
        const cJSON *mute=cJSON_GetObjectItemCaseSensitive(o,"muted");
        if(!cJSON_IsBool(mute))return false;
        out->muted=cJSON_IsTrue(mute);
    } else if(!strcmp(op,"identify"))out->kind=COMMAND_IDENTIFY;
    else if(!strcmp(op,"forget_wifi"))out->kind=COMMAND_FORGET_WIFI;
    else if(!strcmp(op,"wifi")) {
        out->kind=COMMAND_WIFI;
        const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(o,"enabled");
        if(!cJSON_IsBool(enabled))return false;
        out->enabled=cJSON_IsTrue(enabled);
        out->saved=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"saved"));
        if(out->enabled && !out->saved &&
           (!text(o,"ssid",out->ssid,sizeof(out->ssid)) || !out->ssid[0] ||
            !text(o,"password",out->password,sizeof(out->password)) || strlen(out->password)<8 ||
            !text(o,"endpoint",out->endpoint,sizeof(out->endpoint)) ||
            !text(o,"token",out->token,sizeof(out->token)) || strlen(out->token)<20))return false;
    } else return false;
    return true;
}
bool passport_parse(const char *json, size_t length, passport_snapshot_t *out)
{
    if (!json || !out || length == 0 || length > PASSPORT_LINE_MAX) return false;
    /* Bound nesting before recursive JSON parsing on a small embedded stack. */
    unsigned depth=0; bool quoted=false, escape=false;
    for (size_t i=0;i<length;++i) {
        char c=json[i];
        if (quoted) { if (escape) escape=false; else if(c=='\\') escape=true; else if(c=='"') quoted=false; }
        else if(c=='"') quoted=true;
        else if(c=='{' || c=='[') { if (++depth>8) return false; }
        else if(c=='}' || c==']') { if (!depth) return false; --depth; }
    }
    if(depth || quoted) return false;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(json,length,&end,false);
    if (!root) return false;
    while (end < json+length && (*end==' ' || *end=='\r' || *end=='\n' || *end=='\t')) ++end;
    passport_snapshot_t value={0}; double n; bool ok=false;
    if(end != json+length || !number(root,"v",1,1,&n)) goto done;
#define FIELD(name, lo, hi) do { if(!number(root,#name,lo,hi,&n)) goto done; value.name=n; } while(0)
    FIELD(seq,1,2147483647); FIELD(now,0,9007199254740991.0);
    FIELD(running,0,1000000); FIELD(waiting,0,1000000); FIELD(unread,0,2147483647); FIELD(latest,0,2147483647);
    FIELD(tokens,-1,9007199254740991.0); FIELD(today,-1,9007199254740991.0);
#undef FIELD
    const cJSON *windows=cJSON_GetObjectItemCaseSensitive(root,"windows");
    if (!cJSON_IsArray(windows) || cJSON_GetArraySize(windows)!=2) goto done;
    for (int i=0;i<2;++i) {
        const cJSON *w=cJSON_GetArrayItem(windows,i);
        if(!cJSON_IsArray(w) || cJSON_GetArraySize(w)!=3) goto done;
        double limits[3][2]={{-1,100},{0,5256000},{0,9007199254740991.0}};
        double fields[3];
        for(int j=0;j<3;++j) {
            const cJSON *v=cJSON_GetArrayItem(w,j);
            if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<limits[j][0] ||
                v->valuedouble>limits[j][1] || floor(v->valuedouble)!=v->valuedouble) goto done;
            fields[j]=v->valuedouble;
        }
        value.windows[i]=(passport_window_t){fields[0],fields[1],fields[2]};
    }
    const cJSON *e=cJSON_GetObjectItemCaseSensitive(root,"event");
    if(!cJSON_IsNull(e) && !notice(e,&value.event)) goto done;
    const cJSON *recent=cJSON_GetObjectItemCaseSensitive(root,"recent");
    if(!cJSON_IsArray(recent) || cJSON_GetArraySize(recent)>PASSPORT_RECENT) goto done;
    value.recent_count=cJSON_GetArraySize(recent);
    for(unsigned i=0;i<value.recent_count;++i) if(!notice(cJSON_GetArrayItem(recent,i),&value.recent[i])) goto done;
    if(!command(cJSON_GetObjectItemCaseSensitive(root,"cmd"),&value.command))goto done;
    if(cJSON_HasObjectItem(root,"generation")) {
        if(!number(root,"generation",0,2147483647,&n))goto done;
        value.generation=n;
    }
    *out=value; ok=true;
done:
    cJSON_Delete(root);
    return ok;
}

bool passport_pace(const passport_window_t *window,uint64_t now,passport_pace_t *out)
{
    if(!window || !out || window->remaining<0 || window->remaining>100 || !window->minutes || now>=window->reset) return false;
    uint64_t duration=(uint64_t)window->minutes*60;
    if(window->reset<duration || now<window->reset-duration) return false;
    double elapsed=100.0*(now-(window->reset-duration))/duration;
    int used=100-window->remaining;
    double difference=used-elapsed;
    *out=(passport_pace_t){used,(int)round(elapsed),elapsed<1?0:difference>5?1:difference< -5?-1:0};
    return true;
}
