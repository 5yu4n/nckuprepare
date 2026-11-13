// gcc -O2 -pthread -fsanitize=thread -I../pico-app test_spsc.c && ./a.out
#include <pthread.h>
#include <stdio.h>
#include "spsc_ring.h"
#define TOTAL 2000000u
static ring_t q; static unsigned long long got_sum;
static void *cons(void *p) { (void)p; uint32_t tmp[32]; unsigned long long s = 0; uint32_t got = 0;
  while (got < TOTAL) { unsigned n = ring_pop_many(&q, tmp, 32); for (unsigned i = 0; i < n; i++) s += tmp[i]; got += n; } got_sum = s; return NULL; }
int main(void) { pthread_t t; pthread_create(&t, NULL, cons, NULL); unsigned long long ref = 0;
  for (uint32_t i = 0; i < TOTAL; i++) { uint32_t v = i * 2654435761u; ref += v; while (!ring_push(&q, v)) ; }
  pthread_join(t, NULL); printf("spsc %s\n", ref == got_sum ? "ok" : "MISMATCH"); return ref != got_sum; }
