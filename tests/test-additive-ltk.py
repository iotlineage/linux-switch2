#!/usr/bin/env python3
"""Compile the real patched add-only function with a deterministic list/lock fixture.
This exercises policy and concurrent writers, not the kernel's RCU implementation.
"""
import subprocess
import sys
import tempfile
from pathlib import Path
source=Path(sys.argv[1])/'net/bluetooth/hci_core.c'
text=source.read_text()
start=text.index('int hci_switch2_add_ltk(struct hci_dev *hdev,')
end=text.index('\nvoid hci_switch2_ltk_identity(',start)
function=text[start:end]
harness=r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8;
typedef struct {u8 b[6];} bdaddr_t;
#define ADDR_LE_DEV_PUBLIC 0
#define SMP_LTK 1
struct smp_ltk {struct smp_ltk *next;bdaddr_t bdaddr;u8 bdaddr_type,type,authenticated,enc_size,val[16];uint16_t ediv;uint64_t rand;};
struct hci_dev {struct smp_ltk *long_term_keys;bool connected,fail_alloc;bdaddr_t connected_peer;};
static pthread_mutex_t switch2_ltk_lock=PTHREAD_MUTEX_INITIALIZER;
static void release_lock(pthread_mutex_t **p){assert(!pthread_mutex_unlock(*p));}
static pthread_mutex_t *acquire_lock(pthread_mutex_t *p){assert(!pthread_mutex_lock(p));return p;}
#define guard(type) pthread_mutex_t *held __attribute__((cleanup(release_lock)))=acquire_lock
#define list_for_each_entry(p,head,member) for(p=*(head);p;p=p->next)
static int bacmp(const bdaddr_t *a,const bdaddr_t *b){return memcmp(a,b,6);}
static bool hci_conn_hash_lookup_le(struct hci_dev *d,bdaddr_t *peer,int type){(void)type;return d->connected&&!bacmp(peer,&d->connected_peer);}
static struct smp_ltk *__hci_add_ltk(struct hci_dev *d,bdaddr_t *peer,u8 addr,u8 type,u8 authenticated,u8 key[16],u8 enc,uint16_t ediv,uint64_t rand){
 if(d->fail_alloc)return NULL;
 struct smp_ltk *k=calloc(1,sizeof(*k));assert(k);k->bdaddr=*peer;k->bdaddr_type=addr;k->type=type;k->authenticated=authenticated;k->enc_size=enc;k->ediv=ediv;k->rand=rand;memcpy(k->val,key,16);k->next=d->long_term_keys;d->long_term_keys=k;return k;
}
'''
tests=r'''
static struct hci_dev adapter;
static void *pair_other_devices(void *unused){
 (void)unused;
 for(unsigned int i=1;i<2001;i++){
  bdaddr_t peer={{(u8)i,(u8)(i>>8),1,2,3,4}};u8 key[16];memset(key,i&255,16);
  guard(spinlock_bh)(&switch2_ltk_lock);
  assert(__hci_add_ltk(&adapter,&peer,0,SMP_LTK,0,key,16,0,0));
 }
 return NULL;
}
int main(void){
 bdaddr_t target={{1,2,3,4,5,6}}, second={{7,8,9,10,11,12}};u8 key[16]={42},other[16]={43};
 adapter.connected=true;adapter.connected_peer=target;
 assert(hci_switch2_add_ltk(&adapter,&target,key)==-EBUSY&&!adapter.long_term_keys);
 adapter.connected=false;adapter.fail_alloc=true;
 assert(hci_switch2_add_ltk(&adapter,&target,key)==-ENOMEM&&!adapter.long_term_keys);
 adapter.fail_alloc=false;assert(!hci_switch2_add_ltk(&adapter,&target,key));
 struct smp_ltk *original=adapter.long_term_keys;
 adapter.connected=true;
 assert(!hci_switch2_add_ltk(&adapter,&target,key));
 assert(hci_switch2_add_ltk(&adapter,&target,other)==-EEXIST);
 assert(original==adapter.long_term_keys&&!memcmp(original->val,key,16));
 pthread_t writer;assert(!pthread_create(&writer,NULL,pair_other_devices,NULL));
 for(unsigned int i=0;i<2000;i++)assert(!hci_switch2_add_ltk(&adapter,&target,key));
 assert(!pthread_join(writer,NULL));assert(!hci_switch2_add_ltk(&adapter,&second,other));
 unsigned int count=0;
 for(struct smp_ltk *k=adapter.long_term_keys;k;k=k->next){
  count++;
  if(k->bdaddr.b[5]==4){for(int j=0;j<16;j++)assert(k->val[j]==k->bdaddr.b[0]);}
 }
 assert(count==2002&&adapter.connected&&!bacmp(&adapter.connected_peer,&target));
 while(adapter.long_term_keys){struct smp_ltk *k=adapter.long_term_keys;adapter.long_term_keys=k->next;free(k);}
 puts("PASS: actual additive helper: busy target, allocation failure, idempotency, conflict refusal, 2000 concurrent unrelated keys preserved");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'test.c';path.write_text(harness+function+tests)
    binary=Path(tmp)/'test'
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-O2','-pthread',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
