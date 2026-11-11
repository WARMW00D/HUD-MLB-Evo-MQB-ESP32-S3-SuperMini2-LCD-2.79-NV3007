/* gcc -fsanitize=address,undefined tests/test_flog.c supermini_hud2/hud_flog_core.c -o tf && ./tf out.tar && python3 -m tarfile -l out.tar  — ядро флеш-лога на имитации NOR-флеши */
#include "../supermini_hud2/hud_flog_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SZ (FLOG_TOTAL_BYTES+8192)
static uint8_t mem[SZ]; static int fail_after=-1;
static bool rd(uint32_t a,uint8_t*d,size_t n,void*u){memcpy(d,mem+a,n);return 1;}
static bool er(uint32_t a,void*u){if(a%4096)abort();memset(mem+a,0xFF,4096);return 1;}
static bool wr(uint32_t a,const uint8_t*s,size_t n,void*u){
  if((a&~255u)!=((a+n-1)&~255u))abort();
  if(fail_after==0)return 0; if(fail_after>0)fail_after--;
  for(size_t i=0;i<n;i++){ if((mem[a+i]&s[i])!=s[i]){ /* program only clears bits */ fprintf(stderr,"overwrite @%u\n",a+i);abort();} mem[a+i]&=s[i];} return 1;}
static flog_io_t io={rd,er,wr,0};
static flog_t F;
int main(int c,char**v){
  memset(mem,0xFF,SZ);
  if(!flog_mount(&F,&io,4096)) return 1;
  /* big write: 5 slots worth to wrap */
  char line[100]; uint32_t total=0; int N=0;
  for(int i=0;i<20000;i++){ int n=snprintf(line,sizeof line,"line %d %s\n",i,i%7==0?"\xFF":"ok-padding-padding-padding-padding-padding"); if(flog_append(&F,(uint8_t*)line,n,1700000000+i)!=(size_t)n)return 2; total+=n;N++; if(i==5000){ if(!flog_set_epoch_if_unknown(&F,1700000123))return 9;} }
  flog_file_t fl[4]; int n=flog_list(&F,fl);
  printf("total=%u files=%d\n",total,n);
  for(int i=0;i<n;i++)printf(" seq=%u slot=%u size=%u ep=%u\n",fl[i].seq,fl[i].slot,fl[i].size,fl[i].start_epoch);
  /* remount */
  flog_t G; if(!flog_mount(&G,&io,4096))return 3;
  if(G.seq!=F.seq||G.sector!=F.sector||G.off!=F.off){printf("remount mismatch\n");return 4;}
  /* tar dump */
  flog_tar_t T; flog_tar_begin(&G,&T); uint32_t tot=flog_tar_total(&T);
  FILE*o=fopen(c>1?v[1]:"/tmp/o.tar","wb"); uint8_t b[700]; size_t k,sum=0;
  while((k=flog_tar_read(&G,&T,b,sizeof b))){fwrite(b,1,k,o);sum+=k;}
  fclose(o); printf("tar bytes=%zu expect=%u err=%d\n",sum,tot,T.err);
  if(sum!=tot)return 5;
  /* write failure then remount continues */
  fail_after=0; size_t w=flog_append(&F,(uint8_t*)"x\n",2,0); if(w!=0)return 6; fail_after=-1;
  if(!flog_mount(&F,&io,4096))return 7;
  return 0;
}
