#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/main/protocol.h"
int main(void) {
    const char *valid="{\"v\":1,\"seq\":1,\"now\":2000000000,\"running\":2,\"waiting\":1,\"unread\":3,\"latest\":4,\"tokens\":-1,\"today\":-1,\"windows\":[[75,300,2000000900],[-1,0,0]],\"recent\":[{\"id\":4,\"kind\":\"input\",\"project\":\"test\",\"time\":2000000000}],\"event\":null}";
    passport_snapshot_t s={0};
    assert(passport_parse(valid,strlen(valid),&s));assert(s.running==2);assert(s.windows[0].remaining==75);assert(s.windows[1].remaining==-1);
    assert(!passport_parse(valid,strlen(valid)-1,&s));
    assert(!passport_parse("{}",2,&s));assert(!passport_parse("[]",2,&s));
    char line[PASSPORT_LINE_MAX+32];snprintf(line,sizeof(line),"%s{}",valid);
    assert(!passport_parse(line,strlen(line),&s));
    memset(line,'[',20);assert(!passport_parse(line,20,&s));
    snprintf(line,sizeof(line),"%s",valid);char *p=strstr(line,"75,300");p[0]='-';p[1]='9';
    assert(!passport_parse(line,strlen(line),&s));
    const char *suffix=strstr(valid,"\"time\"");
    size_t prefix=(size_t)(suffix-valid);
    memcpy(line,valid,prefix);
    snprintf(line+prefix,sizeof(line)-prefix,"\"title\":\"同步通知\",\"body\":\"等待回复\",%s",suffix);
    assert(passport_parse(line,strlen(line),&s));
    assert(!strcmp(s.recent[0].title,"同步通知"));
    p=strstr(line,"同步");p[0]=(char)0xc0;p[1]=(char)0x80;
    assert(!passport_parse(line,strlen(line),&s));
    passport_window_t window={25,100,12000};passport_pace_t pace;
    assert(passport_pace(&window,9000,&pace));assert(pace.used==75 && pace.elapsed==50 && pace.speed==1);
    window.remaining=80;assert(passport_pace(&window,9000,&pace) && pace.speed==-1);
    assert(!passport_pace(&window,12000,&pace));assert(!passport_pace(&window,5999,&pace));
    puts("Protocol validation: PASS");
}
