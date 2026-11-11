/* gcc tests/test_time.c supermini_hud2/hud_time.c -o tt && ./tt — время машины из CAN 0x6B2 */
#include "../supermini_hud2/hud_time.h"
#include <stdio.h>
#include <string.h>
static void put(uint8_t*d,int s,int l,uint32_t v){uint64_t r=0;for(int i=7;i>=0;i--)r=(r<<8)|d[i];uint64_t m=((1ULL<<l)-1)<<s;r=(r&~m)|(((uint64_t)v<<s)&m);for(int i=0;i<8;i++)d[i]=r>>(8*i);}
static void fr(uint8_t*d,int y,int mo,int da,int h,int mi,int se){memset(d,0,8);put(d,28,7,y-2000);put(d,35,4,mo);put(d,39,5,da);put(d,44,5,h);put(d,49,6,mi);put(d,55,6,se);}
int main(){uint8_t d[8];
 if(hud_time_epoch(0))return 1;
 fr(d,2026,11,11,10,15,3); hud_time_on_6b2(d,1000); if(hud_time_epoch(1000))return 2;
 fr(d,2026,11,11,10,15,4); hud_time_on_6b2(d,2000); if(hud_time_epoch(2000))return 3;
 fr(d,2026,11,11,10,15,5); hud_time_on_6b2(d,3000);
 uint32_t e=hud_time_epoch(3000); hud_dt_t t; hud_time_split(e,&t);
 printf("%04u-%02u-%02u %02u:%02u:%02u\n",t.year,t.mon,t.day,t.hour,t.min,t.sec);
 if(t.year!=2026||t.mon!=11||t.day!=11||t.hour!=10||t.min!=15||t.sec!=5)return 4;
 hud_time_split(hud_time_epoch(63000),&t); printf("+60s: %02u:%02u:%02u\n",t.hour,t.min,t.sec); if(t.min!=16||t.sec!=5)return 5;
 /* garbage frame ignored */ fr(d,2031,2,30,25,0,0); hud_time_on_6b2(d,64000); 
 if(hud_time_epoch(64000)==0) return 6;
 /* leap & roundtrip */ hud_dt_t a={2028,2,29,23,59,59},b; hud_time_split(hud_time_join(&a),&b); if(memcmp(&a,&b,sizeof a))return 7;
 puts("ok");return 0;}
