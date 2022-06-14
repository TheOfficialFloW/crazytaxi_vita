/* main.c -- Crazy Taxi Classic .so loader
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <kubridge.h>

#include <vitaGL.h>

#include <SLES/OpenSLES.h>

#include <fcntl.h>
#include <dirent.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include <math.h>

#include <errno.h>
#include <ctype.h>
#include <sys/time.h>

#include "main.h"
#include "config.h"
#include "dialog.h"
#include "so_util.h"
#include "jni_patch.h"
#include "sha1.h"

int pstv_mode = 0;

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;

so_module crazytaxi_mod;

void *__wrap_memcpy(void *dest, const void *src, size_t n) {
  return sceClibMemcpy(dest, src, n);
}

void *__wrap_memmove(void *dest, const void *src, size_t n) {
  return sceClibMemmove(dest, src, n);
}

void *__wrap_memset(void *s, int c, size_t n) {
  return sceClibMemset(s, c, n);
}

int debugPrintf(char *text, ...) {
#ifdef DEBUG
  va_list list;
  static char string[0x4000];

  va_start(list, text);
  vsprintf(string, text, list);
  va_end(list);

  SceUID fd = sceIoOpen("ux0:data/craxytaxi_log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, string, strlen(string));
    sceIoClose(fd);
  }
#endif
  return 0;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
#ifdef DEBUG
  va_list list;
  static char string[0x4000];

  va_start(list, fmt);
  vsprintf(string, fmt, list);
  va_end(list);

  printf("[LOG] %s: %s\n", tag, string);
#endif
  return 0;
}

int __android_log_write(int prio, const char *tag, const char *text) {
#ifdef DEBUG
  printf("[LOG] %s: %s\n", tag, text);
#endif
  return 0;
}

void __assert2(const char *file, int line, const char *func, const char *expr) {
  printf("assertion failed:\n%s:%d (%s): %s\n", file, line, func, expr);
}

int ret0(void) {
  return 0;
}

int ret1(void) {
  return 1;
}

#define CLOCK_MONOTONIC 0
int clock_gettime(int clk_id, struct timespec *tp) {
  if (clk_id == CLOCK_MONOTONIC) {
    SceKernelSysClock ticks;
    sceKernelGetProcessTime(&ticks);

    tp->tv_sec = ticks / (1000 * 1000);
    tp->tv_nsec = (ticks * 1000) % (1000 * 1000 * 1000);

    return 0;
  } else if (clk_id == CLOCK_REALTIME) {
    time_t seconds;
    SceDateTime time;
    sceRtcGetCurrentClockLocalTime(&time);

    sceRtcGetTime_t(&time, &seconds);

    tp->tv_sec = seconds;
    tp->tv_nsec = time.microsecond * 1000;

    return 0;
  }

  return -ENOSYS;
}

int pthread_mutex_init_fake(SceKernelLwMutexWork **work) {
  *work = (SceKernelLwMutexWork *)memalign(8, sizeof(SceKernelLwMutexWork));
  if (sceKernelCreateLwMutex(*work, "mutex", 0, 0, NULL) < 0)
    return -1;
  return 0;
}

int pthread_mutex_destroy_fake(SceKernelLwMutexWork **work) {
  if (sceKernelDeleteLwMutex(*work) < 0)
    return -1;
  free(*work);
  return 0;
}

int pthread_mutex_lock_fake(SceKernelLwMutexWork **work) {
  if (!*work)
    pthread_mutex_init_fake(work);
  if (sceKernelLockLwMutex(*work, 1, NULL) < 0)
    return -1;
  return 0;
}

int pthread_mutex_trylock_fake(SceKernelLwMutexWork **work) {
  if (!*work)
    pthread_mutex_init_fake(work);
  if (sceKernelTryLockLwMutex(*work, 1) < 0)
    return -1;
  return 0;
}

int pthread_mutex_unlock_fake(SceKernelLwMutexWork **work) {
  if (sceKernelUnlockLwMutex(*work, 1) < 0)
    return -1;
  return 0;
}

int pthread_cond_init_fake(pthread_cond_t **cnd, const int *condattr) {
  pthread_cond_t *c = calloc(1, sizeof(pthread_cond_t));
  if (!c) return -1;

  *c = PTHREAD_COND_INITIALIZER;

  int ret = pthread_cond_init(c, NULL);
  if (ret < 0) {
    free(c);
    return -1;
  }

  *cnd = c;

  return 0;
}

int pthread_cond_broadcast_fake(pthread_cond_t **cnd) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }
  return pthread_cond_broadcast(*cnd);
}

int pthread_cond_signal_fake(pthread_cond_t **cnd) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }
  return pthread_cond_signal(*cnd);
}

int pthread_cond_destroy_fake(pthread_cond_t **cnd) {
  if (cnd && *cnd) {
    pthread_cond_destroy(*cnd);
    free(*cnd);
    *cnd = NULL;
  }
  return 0;
}

int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }
  return pthread_cond_wait(*cnd, *mtx);
}

int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, const struct timespec *t) {
  if (!*cnd) {
    if (pthread_cond_init_fake(cnd, NULL) < 0)
      return -1;
  }
  return pthread_cond_timedwait(*cnd, *mtx, t);
}

int pthread_create_fake(pthread_t *thread, const void *unused, void *entry, void *arg) {
  return pthread_create(thread, NULL, entry, arg);
}

int pthread_once_fake(volatile int *once_control, void (*init_routine) (void)) {
  if (!once_control || !init_routine)
    return -1;
  if (__sync_lock_test_and_set(once_control, 1) == 0)
    (*init_routine)();
  return 0;
}

typedef struct {
  int type;
  float x;
  float y;
  float unk;
  float w;
  float h;
  float tex_x;
  float tex_y;
  float tex_w;
  float tex_h;
  int unk2;
  float unk3;
  int unk4;
  int64_t reserved[6];
} NaomiSprite;

static int (* nlSprPut)(NaomiSprite *sprite);
static int (* Voice_ShutUp)(int id);
static int (* Voice_Request)(int a1, int a2, int64_t a3);

void putGameUI(float x, float y, float x_scale, float y_scale, float tex_x, float tex_y, float w, float h) {
  if (w != 170.0f)
    return;

  // Making direction icons smaller like they are on console/arcade
  x_scale /= 2;
  y_scale /= 2;

  NaomiSprite s;
  s.type = 0x410C;
  s.unk = 0.04f;
  s.x = x + w / 4;
  s.y = y == 312.0f ? (y + 10.0f + h / 4) : y;
  s.w = (w * 0.0019531f) * x_scale;
  s.h = (h * 0.0019531f) * y_scale;
  s.tex_x = tex_x * 0.0019531f;
  s.tex_y = 1.0f - (tex_y * 0.0019531f);
  s.tex_w = (tex_x + w) * 0.0019531f;
  s.tex_h = 1.0f - ((tex_y + h) * 0.0019531f);
  s.unk2 = 0;
  s.unk3 = 1.0f;
  *(&s.unk4 + 1) = 5;
  sceClibMemset(s.reserved, 0, sizeof(int64_t) * 6);
  nlSprPut(&s);
}

static void *g_GameCT = NULL;
static int (*GameCT__Update)(void *GameCT);

void taxi_game_update(void *a1) {
  GameCT__Update(g_GameCT);
}

extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;

// Original locations names
const char *PizzaHutStr = "Pizza Hut";
const char *KFCStr = "Kentucky Fried Chicken";
const char *FILAStr = "FILA";
const char *LeviStr = "The Original Levi's\x7E store";
const char *TowerStr = "Tower Records";

int MendReplaceTex(int a1) {
  int NLS = (int *)(crazytaxi_mod.text_base + 0x0081B15C);
  return *(int *)(NLS + 44 * a1 + 16);
}

int MendChangeZBias(int a1, int *a2, int *a3) {
  int v3, v4, v5, v6, v7, v8, v9, result;
  int CourseMode = *(int *)(crazytaxi_mod.text_base + 0x0078B7DC);
  int s_uSaxA = *(int *)(crazytaxi_mod.text_base + 0x007DE668);
  int s_uShinobiDoor = *(int *)(crazytaxi_mod.text_base + 0x007DE664);
  
  // Fix for Pizza Hut text not showing
  if (a1 == 0xCC0B3A0 || a1 == 0xCBFDF60) {
    v5 = *a3 + 9;
    goto LABEL_277;
  }
  
  if ( CourseMode == 2 )
  {
    if ( a1 > 213406111 )
    {
      if ( a1 > 213697759 )
      {
        if ( a1 <= 213813599 )
        {
          if ( a1 != 213697760 && a1 != 213708032 )
            goto LABEL_278;
          goto LABEL_276;
        }
        if ( a1 != 214424416 && a1 != 213813600 )
          goto LABEL_278;
        goto LABEL_257;
      }
      if ( a1 > 213449951 )
      {
        if ( a1 == 213449952 )
        {
          v5 = 102;
        }
        else
        {
          if ( a1 != 213549984 )
            goto LABEL_278;
          v5 = -10;
        }
        goto LABEL_277;
      }
      if ( a1 != 213406112 )
      {
        if ( a1 == 213410176 )
          goto LABEL_276;
        goto LABEL_278;
      }
      goto LABEL_193;
    }
    if ( a1 <= 213302623 )
    {
      if ( a1 > 213205311 )
      {
        if ( a1 != 213205312 && a1 != 213207360 )
          goto LABEL_278;
        goto LABEL_276;
      }
      if ( a1 == 213138848 )
        goto LABEL_257;
      if ( a1 == 213203264 )
        goto LABEL_276;
      goto LABEL_278;
    }
    if ( a1 > 213343039 )
    {
      if ( a1 == 213343040 )
        goto LABEL_276;
      if ( a1 != 213399264 )
        goto LABEL_278;
      goto LABEL_193;
    }
    if ( a1 == 213302624 )
      goto LABEL_257;
    goto LABEL_192;
  }
  if ( CourseMode != 1 )
  {
    if ( CourseMode )
      goto LABEL_278;
    if ( a1 > 213796031 )
    {
      if ( a1 <= 214026751 )
      {
        if ( a1 > 213949311 )
        {
          if ( a1 > 213986303 )
          {
            if ( a1 > 213993151 )
            {
              v3 = a1 == 213993152;
              if ( a1 != 213993152 )
                v3 = a1 == 213996576;
              if ( !v3 )
              {
                if ( a1 != 214000000 )
                  goto LABEL_278;
                goto LABEL_275;
              }
              goto LABEL_276;
            }
            if ( a1 != 213986304 && a1 != 213989728 )
              goto LABEL_278;
            goto LABEL_257;
          }
          if ( a1 <= 213979455 )
          {
            if ( a1 != 213949312 )
            {
              if ( a1 == 213972608 )
                goto LABEL_276;
              goto LABEL_278;
            }
            goto LABEL_257;
          }
          if ( a1 == 213979456 )
            goto LABEL_276;
          if ( a1 != 213982880 )
            goto LABEL_278;
LABEL_275:
          v5 = *a3 + 6;
          goto LABEL_277;
        }
        if ( a1 <= 213905599 )
        {
          if ( a1 > 213902175 )
          {
            if ( a1 == 213902176 )
              goto LABEL_276;
            if ( a1 != 213905408 )
              goto LABEL_278;
            goto LABEL_257;
          }
          if ( a1 == 213796032 )
            goto LABEL_257;
          if ( a1 == 213895328 )
            goto LABEL_276;
          goto LABEL_278;
        }
        if ( a1 <= 213942463 )
        {
          if ( a1 == 213905600 )
            goto LABEL_276;
          if ( a1 != 213939040 )
            goto LABEL_278;
          goto LABEL_275;
        }
        if ( a1 != 213942464 && a1 != 213945888 )
          goto LABEL_278;
        goto LABEL_193;
      }
      if ( a1 <= 214200831 )
      {
        if ( a1 <= 214140479 )
        {
          if ( a1 > 214083615 )
          {
            if ( a1 == 214083616 )
              goto LABEL_276;
            if ( a1 != 214087040 )
              goto LABEL_278;
            goto LABEL_257;
          }
          if ( a1 == 214026752 )
            goto LABEL_257;
          if ( a1 == 214039776 )
            goto LABEL_276;
          goto LABEL_278;
        }
        if ( a1 <= 214183711 )
        {
          if ( a1 != 214140480 && a1 != 214154912 )
            goto LABEL_278;
          goto LABEL_257;
        }
        if ( a1 == 214183712 )
          goto LABEL_276;
        if ( a1 != 214190560 )
          goto LABEL_278;
        goto LABEL_193;
      }
      if ( a1 <= 214233759 )
      {
        if ( a1 > 214223487 )
        {
          if ( a1 != 214223488 && a1 != 214226912 )
            goto LABEL_278;
        }
        else if ( a1 != 214200832 )
        {
          if ( a1 != 214213184 )
            goto LABEL_278;
          v5 = 2;
          goto LABEL_277;
        }
        goto LABEL_276;
      }
      if ( a1 <= 214278335 )
      {
        if ( a1 == 214233760 )
          goto LABEL_276;
        if ( a1 != 214240608 )
          goto LABEL_278;
        goto LABEL_257;
      }
      if ( a1 == 214278336 )
        goto LABEL_257;
      goto LABEL_135;
    }
    if ( a1 > 213495135 )
    {
      if ( a1 <= 213604927 )
      {
        if ( a1 <= 213576031 )
        {
          if ( a1 > 213508159 )
          {
            if ( a1 == 213508160 )
              goto LABEL_276;
            if ( a1 != 213556832 )
              goto LABEL_278;
          }
          else if ( a1 != 213495136 )
          {
            if ( a1 != 213504736 )
              goto LABEL_278;
            v5 = *a3 + 2;
            goto LABEL_277;
          }
          goto LABEL_257;
        }
        if ( a1 <= 213589727 )
        {
          if ( a1 != 213576032 && a1 != 213582880 )
            goto LABEL_278;
          goto LABEL_276;
        }
        if ( a1 != 213589728 && a1 != 213593152 )
          goto LABEL_278;
        goto LABEL_275;
      }
      if ( a1 > 213693919 )
      {
        if ( a1 <= 213742527 )
        {
          if ( a1 != 213693920 && a1 != 213728160 )
            goto LABEL_278;
          goto LABEL_257;
        }
        v4 = a1 == 213742528;
        if ( a1 != 213742528 )
          v4 = a1 == 213756224;
        if ( !v4 )
        {
          if ( a1 == 213780256 )
            goto LABEL_276;
          goto LABEL_278;
        }
LABEL_257:
        v5 = *a3 + 3;
        goto LABEL_277;
      }
      if ( a1 <= 213665791 )
      {
        if ( a1 != 213604928 && a1 != 213653440 )
          goto LABEL_278;
        goto LABEL_276;
      }
      if ( a1 != 213665792 )
      {
        if ( a1 != 213690496 )
          goto LABEL_278;
        goto LABEL_257;
      }
LABEL_276:
      v5 = 1;
      goto LABEL_277;
    }
    if ( a1 <= 213324959 )
    {
      if ( a1 <= 213286559 )
      {
        if ( a1 > 213239967 )
        {
          if ( a1 != 213239968 && a1 != 213257824 )
            goto LABEL_278;
          goto LABEL_257;
        }
        if ( a1 == 213132000 )
          goto LABEL_257;
        if ( a1 != 213236544 )
          goto LABEL_278;
        goto LABEL_193;
      }
      if ( a1 > 213320863 )
      {
        if ( a1 != 213320864 && a1 != 213322912 )
          goto LABEL_278;
        goto LABEL_276;
      }
      if ( a1 == 213286560 )
        goto LABEL_276;
LABEL_192:
      if ( a1 != 213308928 )
        goto LABEL_278;
      goto LABEL_193;
    }
    if ( a1 <= 213410175 )
    {
      if ( a1 <= 213341631 )
      {
        if ( a1 != 213324960 && a1 != 213329760 )
          goto LABEL_278;
        goto LABEL_276;
      }
      if ( a1 == 213341632 )
        goto LABEL_276;
      if ( a1 != 213350560 )
        goto LABEL_278;
LABEL_193:
      v5 = *a3 + 9;
LABEL_277:
      *a3 = v5;
      goto LABEL_278;
    }
    if ( a1 > 213432767 )
    {
      if ( a1 == 213432768 )
        goto LABEL_276;
      if ( a1 != 213444448 )
        goto LABEL_278;
      goto LABEL_275;
    }
    if ( a1 != 213410176 )
    {
      if ( a1 == 213429344 )
        goto LABEL_276;
      goto LABEL_278;
    }
LABEL_227:
    v5 = 101;
    goto LABEL_277;
  }
  if ( a1 <= 213834623 )
  {
    if ( a1 > 213468639 )
    {
      if ( a1 <= 213665791 )
      {
        if ( a1 <= 213530335 )
        {
          if ( a1 == 213468640 )
            goto LABEL_276;
          if ( a1 != 213476896 )
          {
            if ( a1 == 213517312 )
              goto LABEL_276;
            goto LABEL_278;
          }
          goto LABEL_275;
        }
        if ( a1 > 213618751 )
        {
          if ( a1 != 213618752 && a1 != 213659168 )
            goto LABEL_278;
          goto LABEL_276;
        }
        if ( a1 != 213530336 )
        {
          if ( a1 == 213544032 )
            goto LABEL_276;
          goto LABEL_278;
        }
        goto LABEL_227;
      }
      if ( a1 <= 213791455 )
      {
        if ( a1 == 213665792 )
          goto LABEL_276;
        if ( a1 != 213693920 )
        {
          if ( a1 == 213718112 )
            goto LABEL_276;
          goto LABEL_278;
        }
        goto LABEL_257;
      }
      if ( a1 <= 213824351 )
      {
        if ( a1 != 213791456 && a1 != 213814080 )
          goto LABEL_278;
        goto LABEL_257;
      }
      if ( a1 == 213824352 )
        goto LABEL_276;
      if ( a1 != 213827776 )
        goto LABEL_278;
    }
    else
    {
      if ( a1 > 213288831 )
      {
        if ( a1 > 213311455 )
        {
          if ( a1 <= 213363583 )
          {
            if ( a1 != 213311456 && a1 != 213361536 )
              goto LABEL_278;
            goto LABEL_276;
          }
          if ( a1 != 213363584 )
          {
            if ( a1 != 213452896 )
              goto LABEL_278;
            goto LABEL_257;
          }
          goto LABEL_276;
        }
        if ( a1 == 213288832 || a1 == 213305280 )
          goto LABEL_276;
        goto LABEL_192;
      }
      if ( a1 <= 213182559 )
      {
        if ( a1 != 213172960 )
        {
          if ( a1 == 213175712 || a1 == 213179136 )
          {
            v6 = *a3;
            if ( !(s_uSaxA << 31) )
              v6 += 3;
            *a3 = v6;
            s_uSaxA ^= 1u;
          }
          goto LABEL_278;
        }
        goto LABEL_257;
      }
      if ( a1 > 213225087 )
      {
        if ( a1 == 213225088 )
          goto LABEL_275;
        if ( a1 != 213276832 )
          goto LABEL_278;
        goto LABEL_193;
      }
      if ( a1 == 213182560 )
        goto LABEL_276;
      if ( a1 != 213190080 )
        goto LABEL_278;
    }
    v5 = 3;
    goto LABEL_277;
  }
  if ( a1 <= 213986111 )
  {
    if ( a1 > 213911423 )
    {
      if ( a1 <= 213935391 )
      {
        v9 = a1 == 213911424;
        if ( a1 != 213911424 )
          v9 = a1 == 213928544;
        if ( !v9 && a1 != 213931968 )
          goto LABEL_278;
        goto LABEL_257;
      }
      if ( a1 > 213972415 )
      {
        if ( a1 != 213972416 )
        {
          if ( a1 == 213979264 )
          {
            *a3 = s_uShinobiDoor & 1;
            s_uShinobiDoor ^= 1u;
          }
          goto LABEL_278;
        }
        goto LABEL_257;
      }
      if ( a1 == 213935392 )
        goto LABEL_276;
      if ( a1 == 213945664 )
        goto LABEL_257;
      goto LABEL_278;
    }
    if ( a1 <= 213888127 )
    {
      v7 = a1 == 213834624;
      if ( a1 != 213834624 )
        v7 = a1 == 213851072;
      if ( v7 )
        goto LABEL_257;
      if ( a1 != 213884704 )
        goto LABEL_278;
      goto LABEL_275;
    }
    if ( a1 > 213894975 )
    {
      if ( a1 != 213894976 )
      {
        if ( a1 == 213902176 )
          goto LABEL_276;
        goto LABEL_278;
      }
      goto LABEL_257;
    }
    if ( a1 != 213888128 && a1 != 213891552 )
      goto LABEL_278;
    goto LABEL_193;
  }
  if ( a1 <= 214091647 )
  {
    if ( a1 <= 214036127 )
    {
      v8 = a1 == 213986112;
      if ( a1 != 213986112 )
        v8 = a1 == 214009408;
      if ( v8 )
        goto LABEL_276;
      if ( a1 != 214029952 )
        goto LABEL_278;
      goto LABEL_257;
    }
    if ( a1 <= 214046399 )
    {
      if ( a1 != 214036128 && a1 != 214039552 )
        goto LABEL_278;
      goto LABEL_276;
    }
    if ( a1 != 214046400 )
    {
      if ( a1 != 214077216 )
        goto LABEL_278;
      goto LABEL_257;
    }
    goto LABEL_276;
  }
  if ( a1 > 214183487 )
  {
    if ( a1 <= 214424415 )
    {
      if ( a1 != 214183488 && a1 != 214216832 )
        goto LABEL_278;
      goto LABEL_257;
    }
LABEL_135:
    if ( a1 == 214424416 )
      goto LABEL_257;
    if ( a1 != 220822528 )
      goto LABEL_278;
    goto LABEL_193;
  }
  if ( a1 > 214130015 )
  {
    if ( a1 != 214130016 )
    {
      if ( a1 != 214133440 )
        goto LABEL_278;
      goto LABEL_257;
    }
    goto LABEL_275;
  }
  if ( a1 == 214091648 )
    goto LABEL_257;
  if ( a1 == 214109472 )
    goto LABEL_276;
LABEL_278:
  result = 0xC627F40;
  if ( a2 == (int *)0xC627F40 )
  {
    result = 0;
    *a3 = 0;
  }
  return result;
}

#define LOC(x) (int *)(crazytaxi_mod.text_base + x)
uint32_t Chat_TellDestination(int *a1) {
  int Game_No = *LOC(0x78B7E4);
  int TaxiDriver = *LOC(0x7D77EC);
  int *voice_table = (int *)LOC(0x608E80);
  int *voice_table2 = (int *)LOC(0x608BEC);
  int *voice_out = LOC(0x7D7750);
  
  int v3, v4, v6, v8;
  int *v5;
  int8_t *v7;

  switch (Game_No) {
  case 2:
    voice_out[0] = voice_table[(a1[82] & 7) + 5 * *LOC(0x7D771C)];
    *LOC(0x7D771C) = (*LOC(0x7D771C) + 1) % 5;
    break;
  case 1:
    switch (a1[80]) {
    case 1:
      v6 = voice_table2[(a1[82] & 7) + 40];
      break;
    case 2:
      v6 = voice_table2[(a1[82] & 7) + 55];
      break;
    case 3:
      v6 = voice_table2[(a1[82] & 7) + 60];
      break;
    case 4:
      v6 = voice_table2[(a1[82] & 7) + 65];
      break;
    case 5:
      v6 = voice_table2[(a1[82] & 7) + 70];
      break;
    case 13:
      v6 = voice_table2[(a1[82] & 7) + 95];
      break;
    case 15:
      v6 = voice_table2[(a1[82] & 7) + 110];
      break;
    case 16:
      v6 = voice_table2[(a1[82] & 7) + 35];
      break;
    case 17:
      v6 = voice_table2[(a1[82] & 7) + 45];
      break;
    case 18:
      v6 = voice_table2[(a1[82] & 7) + 150];
      break;
    case 22:
      v6 = voice_table2[(a1[82] & 7) + 130];
      break;
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
      v6 = voice_table2[(a1[82] & 7) + 85];
      break;
    default:
      v4 = a1[82] & 7;
      v3 = 5 * *LOC(0x7D771C);
      v5 = voice_table;
      v6 = v5[v3 + v4];
      break;
    }
    break;
  case 0:
    v3 = 5 * a1[80];
    v4 = a1[82] & 7;
    v5 = voice_table2;
    v6 = v5[v3 + v4];
    break;
  }
  voice_out[0] = v6;
  voice_out[1] = 2;
  voice_out[2] = 0x7FFFFFFF;
  v7 = (int8_t *)(crazytaxi_mod.text_base + 0x608B5C) + 12 * (*LOC(0x7D7714) % 3 + 3 * TaxiDriver);
  v8 = *(int64_t *)(v7 - 36);
  *LOC(0x7D7770) = *((int32_t *)v7 - 7);
  *(int64_t *)(crazytaxi_mod.text_base + 0x7D7768) = v8;
  Voice_ShutUp(1);
  Voice_Request((int)(crazytaxi_mod.text_base + 0x7D7768), 1, 127);
  Voice_ShutUp(2);
  return Voice_Request(voice_out, 2, 127);
}

uint64_t rumble_tick = 0;
void StartRumble (void) {
  if (!pstv_mode)
    return;
  SceCtrlActuator handle;
  handle.small = 100;
  handle.large = 100;
  sceCtrlSetActuator(1, &handle);
  rumble_tick = sceKernelGetProcessTimeWide();
}

void StopRumble (void) {
  SceCtrlActuator handle;
  handle.small = 0;
  handle.large = 0;
  sceCtrlSetActuator(1, &handle);
  rumble_tick = 0;
}

int VibStart(int id, int strength) {
  if (strength > 0)
    StartRumble();
  return 0;
}

//static int (*taxi_game_accelerometer)(float x, float y, float z);

void patch_game(void) {
  hook_addr(so_symbol(&crazytaxi_mod, "__cxa_guard_acquire"), (uintptr_t)&__cxa_guard_acquire);
  hook_addr(so_symbol(&crazytaxi_mod, "__cxa_guard_release"), (uintptr_t)&__cxa_guard_release);

  hook_addr(so_symbol(&crazytaxi_mod, "_ZNSt6__ndk113random_deviceC2ERKNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE"), (uintptr_t)&ret0);
  hook_addr(so_symbol(&crazytaxi_mod, "_ZNSt6__ndk113random_deviceD2Ev"), (uintptr_t)&ret0);
  
  g_GameCT = (void *)so_symbol(&crazytaxi_mod, "g_GameCT");
  GameCT__Update = (void *)so_symbol(&crazytaxi_mod, "_ZN6GameCT6UpdateEv");
  //taxi_game_accelerometer = (void *)so_symbol(&crazytaxi_mod, "_Z23taxi_game_accelerometerfff");
  hook_addr(so_symbol(&crazytaxi_mod, "_Z16taxi_game_updatev"), (uintptr_t)&taxi_game_update);

  // Nuke touch widgets
  nlSprPut = (void *)so_symbol(&crazytaxi_mod, "nlSprPut");
  hook_addr(so_symbol(&crazytaxi_mod, "_Z9putGameUIffffffff"), (uintptr_t)&putGameUI);

  // Restore original location names
  uint8_t *LandmarksPtrs = (uint8_t *)so_symbol(&crazytaxi_mod, "LandmarkID");
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x24), &PizzaHutStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x8C), &PizzaHutStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x3C), &KFCStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x9C), &KFCStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x30), &FILAStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x90), &FILAStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x34), &LeviStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x94), &LeviStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x38), &TowerStr, 4);
  kuKernelCpuUnrestrictedMemcpy((void *)(LandmarksPtrs + 0x98), &TowerStr, 4);
  
  // Restoring FILA as possible destination
  uint8_t *exec_KyakuMain = (uint8_t *)so_symbol(&crazytaxi_mod, "_Z14exec_KyakuMainv");
  uint16_t instr = 0xE04F; // b #0xa2
  kuKernelCpuUnrestrictedMemcpy((void *)(exec_KyakuMain + 0x3A0), &instr, 2);

  // Restoring voicelines for cut contents
  Voice_ShutUp = (void *)so_symbol(&crazytaxi_mod, "_Z12Voice_ShutUpi");
  Voice_Request = (void *)so_symbol(&crazytaxi_mod, "_Z13Voice_RequestP13tagVOICEENTRYiii");
  hook_addr(so_symbol(&crazytaxi_mod, "_Z20Chat_TellDestinationPv"), (uintptr_t)&Chat_TellDestination);
  
  // Restore Pizza Hut and FILA models
  hook_addr(so_symbol(&crazytaxi_mod, "_Z10MendNoDrawiRbfRiPf"), (uintptr_t)&ret0);
  
  // Restore original brands textures
  hook_addr(so_symbol(&crazytaxi_mod, "_Z14MendReplaceTexi"), (uintptr_t)&MendReplaceTex);
  
  // Restore original landsmark previews
  hook_addr(so_symbol(&crazytaxi_mod, "_Z20MendReplaceTexReloadi"), (uintptr_t)&ret0);
  
  // Fix Pizza Hut text not showing up
  hook_addr(so_symbol(&crazytaxi_mod, "_Z15MendChangeZBiasiPiRi"), (uintptr_t)&MendChangeZBias);
  
  // Add vibration support for PSTV
  hook_addr(so_symbol(&crazytaxi_mod, "_ZN11GuiJoystick8VibStartEii"), (uintptr_t)&VibStart);
}

extern void *__cxa_atexit;
extern void *__cxa_finalize;
extern void *__memcpy_chk;
extern void *__memmove_chk;
extern void *__stack_chk_fail;
extern void *__strcat_chk;
extern void *__strchr_chk;
extern void *__strcpy_chk;
extern void *__strlen_chk;
extern void *__strrchr_chk;
extern void *__vsnprintf_chk;
extern void *__vsprintf_chk;
extern void *strtoumax;
extern void *strtoimax;

static char *__ctype_ = (char *)&_ctype_;

static int __stack_chk_guard_fake = 0x42424242;

static FILE __sF_fake[0x100][3];

static const short _C_tolower_[] = {
  -1,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
  0x40, 'a',  'b',  'c',  'd',  'e',  'f',  'g',
  'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
  'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
  'x',  'y',  'z',  0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
  0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
  0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
  0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
  0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
  0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
  0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

const short *_tolower_tab_ = _C_tolower_;

int sysinfo_fake(unsigned long *info) {
  info[4] = MEMORY_NEWLIB_MB * 1024 * 1024; // total
  info[5] = (MEMORY_NEWLIB_MB - 16) * 1024 * 1024; // free
  return 0;
}

void glShaderSourceHook(GLuint shader, GLsizei count, const GLchar **string, const GLint *length) {
  uint32_t sha1[5];
  SHA1_CTX ctx;

  sha1_init(&ctx);
  sha1_update(&ctx, (uint8_t *)*string, strlen(*string));
  sha1_final(&ctx, (uint8_t *)sha1);

  char sha_name[64];
  snprintf(sha_name, sizeof(sha_name), "%08x%08x%08x%08x%08x", sha1[0], sha1[1], sha1[2], sha1[3], sha1[4]);

  char cg_path[128];
  snprintf(cg_path, sizeof(cg_path), "%s/%s.cg", CG_PATH, sha_name);

  FILE *file = fopen(cg_path, "rb");
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *source = malloc(size + 1);
  fread(source, 1, size, file);
  source[size] = '\0';
  fclose(file);

  const GLchar *sources[] = { source };
  glShaderSource(shader, 1, sources, NULL);

  free(source);
}

void glBindAttribLocationHook(GLuint prog, GLuint index, const GLchar *name) {
  char *new_name = "";
  if (strcmp(name, "xlat_attrib_position") == 0)
    new_name = "In.Pos";
  else if (strcmp(name, "xlat_attrib_texcoord0") == 0)
    new_name = "In.UV";
  else if (strcmp(name, "xlat_attrib_color0") == 0)
    new_name = "In.Color";
  else if (strcmp(name, "xlat_attrib_normal") == 0)
    new_name = "In.Normal";
  glBindAttribLocation(prog, index, new_name);
}

const GLubyte *glGetStringHook(GLenum name) {
  if (name == GL_EXTENSIONS)
    return (GLubyte *)"GL_IMG_texture_compression_pvrtc";
  return glGetString(name);
}

void glTexImage2DHook(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *data) {
  if (level == 0)
    glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
}

void glCompressedTexImage2DHook(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
  if (level == 0)
    glCompressedTexImage2D(target, level, format, width, height, border, imageSize, data);
}

static void *fb_data = NULL;
void glFramebufferTexture2DHook(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
  if (texture != 0) {
    if (!fb_data)
      fb_data = malloc(SCREEN_W * SCREEN_H * 4);
    glReadPixels(0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, fb_data);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_W, SCREEN_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, fb_data);
 }
}

void *sceClibMemclr(void *dst, SceSize len) {
  return sceClibMemset(dst, 0, len);
}

void *sceClibMemset2(void *dst, SceSize len, int ch) {
  return sceClibMemset(dst, ch, len);
}

static so_default_dynlib default_dynlib[] = {
  { "SL_IID_BUFFERQUEUE", (uintptr_t)&SL_IID_BUFFERQUEUE },
  { "SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE },
  { "SL_IID_ENVIRONMENTALREVERB", (uintptr_t)&SL_IID_ENVIRONMENTALREVERB },
  { "SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY },
  { "SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME },
  { "__aeabi_memclr", (uintptr_t)&sceClibMemclr },
  { "__aeabi_memclr4", (uintptr_t)&sceClibMemclr },
  { "__aeabi_memclr8", (uintptr_t)&sceClibMemclr },
  { "__aeabi_memcpy", (uintptr_t)&sceClibMemcpy },
  { "__aeabi_memcpy4", (uintptr_t)&sceClibMemcpy },
  { "__aeabi_memcpy8", (uintptr_t)&sceClibMemcpy },
  { "__aeabi_memmove", (uintptr_t)&sceClibMemmove },
  { "__aeabi_memmove4", (uintptr_t)&sceClibMemmove },
  { "__aeabi_memset", (uintptr_t)&sceClibMemset2 },
  { "__aeabi_memset4", (uintptr_t)&sceClibMemset2 },
  { "__aeabi_memset8", (uintptr_t)&sceClibMemset2 },
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__android_log_write", (uintptr_t)&__android_log_write },
  { "__assert2", (uintptr_t)&__assert2 },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
  { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
  { "__errno", (uintptr_t)&__errno },
  // { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx },
  { "__memcpy_chk", (uintptr_t)&__memcpy_chk },
  { "__memmove_chk", (uintptr_t)&__memmove_chk },
  // { "__open_2", (uintptr_t)&__open_2 },
  { "__sF", (uintptr_t)&__sF_fake },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
  { "__strcat_chk", (uintptr_t)&__strcat_chk },
  { "__strchr_chk", (uintptr_t)&strchr },
  { "__strcpy_chk", (uintptr_t)&__strcpy_chk },
  { "__strlen_chk", (uintptr_t)&strlen },
  { "__strrchr_chk", (uintptr_t)&strrchr },
  { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk },
  { "__vsprintf_chk", (uintptr_t)&__vsprintf_chk },
  { "_ctype_", (uintptr_t)&__ctype_ },
  { "_exit", (uintptr_t)&_exit },
  { "_tolower_tab_", (uintptr_t)&_tolower_tab_ },
  { "abort", (uintptr_t)&abort },
  // { "accept", (uintptr_t)&accept },
  { "acos", (uintptr_t)&acos },
  { "acosf", (uintptr_t)&acosf },
  { "asin", (uintptr_t)&asin },
  { "atan", (uintptr_t)&atan },
  { "atan2f", (uintptr_t)&atan2f },
  { "atanf", (uintptr_t)&atanf },
  { "atof", (uintptr_t)&atof },
  { "atoi", (uintptr_t)&atoi },
  { "atoll", (uintptr_t)&atoll },
  // { "bind", (uintptr_t)&bind },
  // { "bsd_signal", (uintptr_t)&bsd_signal },
  { "btowc", (uintptr_t)&btowc },
  { "calloc", (uintptr_t)&calloc },
  { "ceil", (uintptr_t)&ceil },
  { "ceilf", (uintptr_t)&ceilf },
  { "clock_gettime", (uintptr_t)&clock_gettime },
  // { "close", (uintptr_t)&close },
  // { "closedir", (uintptr_t)&closedir },
  // { "connect", (uintptr_t)&connect },
  { "cos", (uintptr_t)&cos },
  { "cosf", (uintptr_t)&cosf },
  // { "dladdr", (uintptr_t)&dladdr },
  // { "dlclose", (uintptr_t)&dlclose },
  // { "dlerror", (uintptr_t)&dlerror },
  // { "dlopen", (uintptr_t)&dlopen },
  // { "dlsym", (uintptr_t)&dlsym },
  { "exit", (uintptr_t)&exit },
  { "expf", (uintptr_t)&expf },
  { "fclose", (uintptr_t)&fclose },
  { "fcntl", (uintptr_t)&fcntl },
  { "feof", (uintptr_t)&feof },
  { "ferror", (uintptr_t)&ferror },
  { "fflush", (uintptr_t)&fflush },
  { "fgetc", (uintptr_t)&fgetc },
  { "fgets", (uintptr_t)&fgets },
  { "fileno", (uintptr_t)&fileno },
  { "floor", (uintptr_t)&floor },
  { "floorf", (uintptr_t)&floorf },
  { "fmodf", (uintptr_t)&fmodf },
  { "fopen", (uintptr_t)&fopen },
  { "fprintf", (uintptr_t)&fprintf },
  { "fputc", (uintptr_t)&fputc },
  { "fputs", (uintptr_t)&fputs },
  { "fread", (uintptr_t)&fread },
  { "free", (uintptr_t)&free },
  // { "freeaddrinfo", (uintptr_t)&freeaddrinfo },
  { "fseek", (uintptr_t)&fseek },
  { "fseeko", (uintptr_t)&fseeko },
  { "fstat", (uintptr_t)&fstat },
  { "ftell", (uintptr_t)&ftell },
  { "ftello", (uintptr_t)&ftello },
  { "ftruncate", (uintptr_t)&ftruncate },
  { "funopen", (uintptr_t)&funopen },
  { "fwrite", (uintptr_t)&fwrite },
  // { "gai_strerror", (uintptr_t)&gai_strerror },
  // { "getaddrinfo", (uintptr_t)&getaddrinfo },
  { "getenv", (uintptr_t)&getenv },
  // { "gethostbyname", (uintptr_t)&gethostbyname },
  // { "getnameinfo", (uintptr_t)&getnameinfo },
  // { "getpeername", (uintptr_t)&getpeername },
  // { "getpid", (uintptr_t)&getpid },
  // { "getpwuid", (uintptr_t)&getpwuid },
  // { "getrlimit", (uintptr_t)&getrlimit },
  // { "getsockname", (uintptr_t)&getsockname },
  // { "getsockopt", (uintptr_t)&getsockopt },
  { "gettimeofday", (uintptr_t)&gettimeofday },
  // { "getuid", (uintptr_t)&getuid },
  { "glActiveTexture", (uintptr_t)&glActiveTexture },
  { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocationHook },
  { "glBindBuffer", (uintptr_t)&glBindBuffer },
  { "glBindFramebuffer", (uintptr_t)&ret0 },
  { "glBindRenderbuffer", (uintptr_t)&ret0 },
  { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glBufferData", (uintptr_t)&glBufferData },
  { "glBufferSubData", (uintptr_t)&glBufferSubData },
  { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
  { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColor },
  { "glClearDepthf", (uintptr_t)&glClearDepthf },
  { "glClearStencil", (uintptr_t)&glClearStencil },
  { "glColorMask", (uintptr_t)&glColorMask },
  { "glCompileShader", (uintptr_t)&glCompileShader },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2DHook },
  // { "glCopyTexImage2D", (uintptr_t)&glCopyTexImage2D },
  { "glCreateProgram", (uintptr_t)&glCreateProgram },
  { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glCullFace", (uintptr_t)&glCullFace },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteRenderbuffers", (uintptr_t)&ret0 },
  { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
  { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask },
  { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
  { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawElements", (uintptr_t)&glDrawElements },
  { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glFramebufferRenderbuffer", (uintptr_t)&ret0 },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2DHook },
  { "glGenBuffers", (uintptr_t)&glGenBuffers },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
  { "glGenRenderbuffers", (uintptr_t)&ret0 },
  { "glGenTextures", (uintptr_t)&glGenTextures },
  { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap },
  { "glGetFramebufferAttachmentParameteriv", (uintptr_t)&ret0 },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
  { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv },
  { "glGetString", (uintptr_t)&glGetStringHook },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glLinkProgram", (uintptr_t)&glLinkProgram },
  { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glRenderbufferStorage", (uintptr_t)&ret0 },
  { "glScissor", (uintptr_t)&glScissor },
  { "glShaderSource", (uintptr_t)&glShaderSourceHook },
  { "glStencilFunc", (uintptr_t)&glStencilFunc },
  { "glStencilMask", (uintptr_t)&glStencilMask },
  { "glStencilOp", (uintptr_t)&glStencilOp },
  { "glTexImage2D", (uintptr_t)&glTexImage2DHook },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
  { "glUniform1fv", (uintptr_t)&glUniform1fv },
  { "glUniform1i", (uintptr_t)&glUniform1i },
  { "glUniform2fv", (uintptr_t)&glUniform2fv },
  { "glUniform3fv", (uintptr_t)&glUniform3fv },
  { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniformMatrix2fv", (uintptr_t)&glUniformMatrix2fv },
  { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
  { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
  { "glViewport", (uintptr_t)&glViewport },
  { "gmtime", (uintptr_t)&gmtime },
  { "gmtime_r", (uintptr_t)&gmtime_r },
  // { "inet_ntop", (uintptr_t)&inet_ntop },
  // { "inet_pton", (uintptr_t)&inet_pton },
  // { "initgroups", (uintptr_t)&initgroups },
  // { "ioctl", (uintptr_t)&ioctl },
  { "isalnum", (uintptr_t)&isalnum },
  { "isalpha", (uintptr_t)&isalpha },
  { "isblank", (uintptr_t)&isblank },
  { "islower", (uintptr_t)&islower },
  { "isspace", (uintptr_t)&isspace },
  { "isupper", (uintptr_t)&isupper },
  { "iswalpha", (uintptr_t)&iswalpha },
  { "iswcntrl", (uintptr_t)&iswcntrl },
  { "iswdigit", (uintptr_t)&iswdigit },
  { "iswlower", (uintptr_t)&iswlower },
  { "iswprint", (uintptr_t)&iswprint },
  { "iswpunct", (uintptr_t)&iswpunct },
  { "iswspace", (uintptr_t)&iswspace },
  { "iswupper", (uintptr_t)&iswupper },
  { "iswxdigit", (uintptr_t)&iswxdigit },
  { "isxdigit", (uintptr_t)&isxdigit },
  { "kill", (uintptr_t)&kill },
  { "ldexp", (uintptr_t)&ldexp },
  { "ldexpf", (uintptr_t)&ldexpf },
  // { "listen", (uintptr_t)&listen },
  { "localtime", (uintptr_t)&localtime },
  { "localtime_r", (uintptr_t)&localtime_r },
  { "log", (uintptr_t)&log },
  { "logf", (uintptr_t)&logf },
  { "log10", (uintptr_t)&log10 },
  { "log10f", (uintptr_t)&log10f },
  { "lrand48", (uintptr_t)&lrand48 },
  { "lseek", (uintptr_t)&lseek },
  { "malloc", (uintptr_t)&malloc },
  { "mbrlen", (uintptr_t)&mbrlen },
  { "mbtowc", (uintptr_t)&mbtowc },
  { "mbrtowc", (uintptr_t)&mbrtowc },
  { "mbstowcs", (uintptr_t)&mbstowcs },
  { "mbsnrtowcs", (uintptr_t)&mbsnrtowcs },
  { "memalign", (uintptr_t)&memalign },
  { "memchr", (uintptr_t)&memchr },
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmem", (uintptr_t)&memmem },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "mkdir", (uintptr_t)&mkdir },
  { "mktime", (uintptr_t)&mktime },
  // { "mlock", (uintptr_t)&mlock },
  // { "mmap", (uintptr_t)&mmap },
  { "modf", (uintptr_t)&modf },
  // { "mprotect", (uintptr_t)&mprotect },
  // { "munmap", (uintptr_t)&munmap },
  // { "nanosleep", (uintptr_t)&nanosleep },
  { "newlocale", (uintptr_t)&ret0 },
  // { "open", (uintptr_t)&open },
  // { "opendir", (uintptr_t)&opendir },
  { "perror", (uintptr_t)&perror },
  // { "pipe", (uintptr_t)&pipe },
  // { "poll", (uintptr_t)&poll },
  { "pow", (uintptr_t)&pow },
  { "powf", (uintptr_t)&powf },
  { "printf", (uintptr_t)&printf },
  { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_fake },
  { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_fake },
  { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_fake },
  { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_fake },
  { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_fake },
  { "pthread_create", (uintptr_t)&pthread_create_fake },
  { "pthread_detach", (uintptr_t)&pthread_detach },
  { "pthread_equal", (uintptr_t)&pthread_equal },
  { "pthread_exit", (uintptr_t)&pthread_exit },
  { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
  { "pthread_join", (uintptr_t)&pthread_join },
  { "pthread_key_create", (uintptr_t)&pthread_key_create },
  { "pthread_key_delete", (uintptr_t)&pthread_key_delete },
  { "pthread_kill", (uintptr_t)&pthread_kill },
  { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake },
  { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake },
  { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake },
  { "pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_fake },
  { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake },
  // { "pthread_mutexattr_destroy", (uintptr_t)&pthread_mutexattr_destroy },
  // { "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init },
  // { "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype },
  { "pthread_once", (uintptr_t)&pthread_once_fake },
  // { "pthread_rwlock_destroy", (uintptr_t)&pthread_rwlock_destroy },
  // { "pthread_rwlock_init", (uintptr_t)&pthread_rwlock_init },
  // { "pthread_rwlock_rdlock", (uintptr_t)&pthread_rwlock_rdlock },
  // { "pthread_rwlock_unlock", (uintptr_t)&pthread_rwlock_unlock },
  // { "pthread_rwlock_wrlock", (uintptr_t)&pthread_rwlock_wrlock },
  { "pthread_self", (uintptr_t)&pthread_self },
  { "pthread_setspecific", (uintptr_t)&pthread_setspecific },
  { "puts", (uintptr_t)&puts },
  { "qsort", (uintptr_t)&qsort },
  { "rand", (uintptr_t)&rand },
  { "raise", (uintptr_t)&raise },
  // { "read", (uintptr_t)&read },
  // { "readdir", (uintptr_t)&readdir },
  { "realloc", (uintptr_t)&realloc },
  // { "recv", (uintptr_t)&recv },
  // { "recvfrom", (uintptr_t)&recvfrom },
  { "remove", (uintptr_t)&remove },
  { "rename", (uintptr_t)&rename },
  { "rewind", (uintptr_t)&rewind },
  { "rintf", (uintptr_t)&rintf },
  { "rmdir", (uintptr_t)&rmdir },
  { "scalbn", (uintptr_t)&scalbn },
  { "sched_yield", (uintptr_t)&sched_yield },
  // { "select", (uintptr_t)&select },
  // { "send", (uintptr_t)&send },
  // { "sendto", (uintptr_t)&sendto },
  // { "setgid", (uintptr_t)&setgid },
  // { "setsockopt", (uintptr_t)&setsockopt },
  // { "setuid", (uintptr_t)&setuid },
  // { "shutdown", (uintptr_t)&shutdown },
  // { "sigaction", (uintptr_t)&sigaction },
  // { "sigprocmask", (uintptr_t)&sigprocmask },
  { "sincos", (uintptr_t)&sincos },
  { "sincosf", (uintptr_t)&sincosf },
  { "sin", (uintptr_t)&sin },
  { "sinf", (uintptr_t)&sinf },
  { "slCreateEngine", (uintptr_t)&slCreateEngine },
  { "snprintf", (uintptr_t)&snprintf },
  // { "socket", (uintptr_t)&socket },
  { "sprintf", (uintptr_t)&sprintf },
  { "srand", (uintptr_t)&srand },
  { "srand48", (uintptr_t)&srand48 },
  { "sscanf", (uintptr_t)&sscanf },
  // { "stat", (uintptr_t)&stat },
  { "strcasecmp", (uintptr_t)&strcasecmp },
  { "strcat", (uintptr_t)&strcat },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strcoll", (uintptr_t)&strcoll },
  { "strcpy", (uintptr_t)&strcpy },
  { "strcspn", (uintptr_t)&strcspn },
  { "strdup", (uintptr_t)&strdup },
  { "strerror", (uintptr_t)&strerror },
  { "strerror_r", (uintptr_t)&strerror_r },
  { "strftime", (uintptr_t)&strftime },
  { "strlen", (uintptr_t)&strlen },
  { "strncasecmp", (uintptr_t)&strncasecmp },
  { "strncmp", (uintptr_t)&strncmp },
  { "strncpy", (uintptr_t)&strncpy },
  { "strrchr", (uintptr_t)&strrchr },
  { "strspn", (uintptr_t)&strspn },
  { "strstr", (uintptr_t)&strstr },
  { "strtod", (uintptr_t)&strtod },
  { "strtoimax", (uintptr_t)&strtoimax },
  { "strtoumax", (uintptr_t)&strtoumax },
  { "strtol", (uintptr_t)&strtol },
  { "strtoll", (uintptr_t)&strtoll },
  { "strtoul", (uintptr_t)&strtoul },
  { "strtoull", (uintptr_t)&strtoull },
  // { "strtoumax", (uintptr_t)&strtoumax },
  { "strxfrm", (uintptr_t)&strxfrm },
  // { "syscall", (uintptr_t)&syscall },
  // { "sysconf", (uintptr_t)&sysconf },
  { "sysinfo", (uintptr_t)&sysinfo_fake },
  // { "syslog", (uintptr_t)&syslog },
  // { "system", (uintptr_t)&system },
  { "tan", (uintptr_t)&tan },
  { "tanf", (uintptr_t)&tanf },
  { "time", (uintptr_t)&time },
  { "tolower", (uintptr_t)&tolower },
  { "toupper", (uintptr_t)&toupper },
  { "towlower", (uintptr_t)&towlower },
  { "towupper", (uintptr_t)&towupper },
  // { "uname", (uintptr_t)&uname },
  { "unlink", (uintptr_t)&unlink },
  { "usleep", (uintptr_t)&usleep },
  { "vasprintf", (uintptr_t)&vasprintf },
  { "vfprintf", (uintptr_t)&vfprintf },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf },
  { "vsscanf", (uintptr_t)&vsscanf },
  { "wcscoll", (uintptr_t)&wcscoll },
  { "wcscat", (uintptr_t)&wcscat },
  { "wcscpy", (uintptr_t)&wcscpy },
  { "wcslen", (uintptr_t)&wcslen },
  { "wcrtomb", (uintptr_t)&wcrtomb },
  { "wcstombs", (uintptr_t)&wcstombs },
  { "wcstoul", (uintptr_t)&wcstoul },
  { "wcstoull", (uintptr_t)&wcstoull },
  { "wcstol", (uintptr_t)&wcstol },
  { "wcstoll", (uintptr_t)&wcstoll },
  { "wcsxfrm", (uintptr_t)&wcsxfrm },
  { "wctob", (uintptr_t)&wctob },
  { "wcstof", (uintptr_t)&wcstof },
  { "wcstod", (uintptr_t)&wcstod },
  { "wmemchr", (uintptr_t)&wmemchr },
  { "wmemcmp", (uintptr_t)&wmemcmp },
  { "wmemcpy", (uintptr_t)&wmemcpy },
  { "wmemmove", (uintptr_t)&wmemmove },
  { "wmemset", (uintptr_t)&wmemset },
  // { "write", (uintptr_t)&write },
};

int check_kubridge(void) {
  int search_unk[2];
  return _vshKernelSearchModuleByName("kubridge", search_unk);
}

int file_exists(const char *path) {
  SceIoStat stat;
  return sceIoGetstat(path, &stat) >= 0;
}

enum {
  AKEYCODE_DPAD_UP = 19,
  AKEYCODE_DPAD_DOWN = 20,
  AKEYCODE_DPAD_LEFT = 21,
  AKEYCODE_DPAD_RIGHT = 22,
  AKEYCODE_BUTTON_A = 96,
  AKEYCODE_BUTTON_B = 97,
  AKEYCODE_BUTTON_X = 99,
  AKEYCODE_BUTTON_Y = 100,
  AKEYCODE_BUTTON_L1 = 102,
  AKEYCODE_BUTTON_R1 = 103,
  AKEYCODE_BUTTON_START = 108,
  AKEYCODE_BUTTON_SELECT = 109,
};

typedef struct {
  uint32_t sce_button;
  uint32_t android_button;
} ButtonMapping;

static ButtonMapping mapping[] = {
  { SCE_CTRL_UP,        AKEYCODE_DPAD_UP },
  { SCE_CTRL_DOWN,      AKEYCODE_DPAD_DOWN },
  { SCE_CTRL_LEFT,      AKEYCODE_DPAD_LEFT },
  { SCE_CTRL_RIGHT,     AKEYCODE_DPAD_RIGHT },
  { SCE_CTRL_CROSS,     AKEYCODE_BUTTON_A },
  { SCE_CTRL_CIRCLE,    AKEYCODE_BUTTON_B },
  { SCE_CTRL_SQUARE,    AKEYCODE_BUTTON_X },
  { SCE_CTRL_TRIANGLE,  AKEYCODE_BUTTON_Y },
  { SCE_CTRL_L1,        AKEYCODE_BUTTON_L1 },
  { SCE_CTRL_R1,        AKEYCODE_BUTTON_R1 },
  { SCE_CTRL_START,     AKEYCODE_BUTTON_START },
  { SCE_CTRL_SELECT,    AKEYCODE_BUTTON_SELECT },
};

int vgl_inited = 0;

int main(int argc, char *argv[]) {
  SceAppUtilInitParam init_param;
  SceAppUtilBootParam boot_param;
  memset(&init_param, 0, sizeof(SceAppUtilInitParam));
  memset(&boot_param, 0, sizeof(SceAppUtilBootParam));
  sceAppUtilInit(&init_param, &boot_param);

  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  //sceMotionStartSampling();

  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);
  
  pstv_mode = sceCtrlIsMultiControllerSupported() ? 1 : 0;

  if (check_kubridge() < 0)
    fatal_error("Error kubridge.skprx is not installed.");

  if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
    fatal_error("Error libshacccg.suprx is not installed.");

  if (so_file_load(&crazytaxi_mod, DATA_PATH "/libgl2jni.so", LOAD_ADDRESS) < 0)
    fatal_error("Error could not load %s.", DATA_PATH "/libgl2jni.so");
  so_relocate(&crazytaxi_mod);
  so_resolve(&crazytaxi_mod, default_dynlib, sizeof(default_dynlib), 0);

  patch_game();
  so_flush_caches(&crazytaxi_mod);

  so_initialize(&crazytaxi_mod);

  vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_ENABLE, SHARK_ENABLE);
  vglInitExtended(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
  vgl_inited = 1;

  jni_load();

  int (* Java_com_sega_CrazyTaxi_GL2JNILib_init)(void *env, void *obj, int width, int height) = (void *)so_symbol(&crazytaxi_mod, "Java_com_sega_CrazyTaxi_GL2JNILib_init");
  int (* Java_com_sega_CrazyTaxi_GL2JNILib_resume)(void) = (void *)so_symbol(&crazytaxi_mod, "Java_com_sega_CrazyTaxi_GL2JNILib_resume");
  int (* Java_com_sega_CrazyTaxi_GL2JNILib_step)(void) = (void *)so_symbol(&crazytaxi_mod, "Java_com_sega_CrazyTaxi_GL2JNILib_step");
  int (* Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive)(void *env, void *obj, int active) = (void *)so_symbol(&crazytaxi_mod, "Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive");
  int (* Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton)(void *env, void *obj, int scan_code, int state, int disable_touch) = (void *)so_symbol(&crazytaxi_mod, "Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton");

  Java_com_sega_CrazyTaxi_GL2JNILib_onJoystickActive(fake_env, 0, 1);
  Java_com_sega_CrazyTaxi_GL2JNILib_init(fake_env, 0, SCREEN_W, SCREEN_H);
  Java_com_sega_CrazyTaxi_GL2JNILib_resume();

  uint32_t cur_buttons = 0, old_buttons = 0, changed_buttons = 0;

  while (1) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

    cur_buttons = pad.buttons;

    if (pad.ly < ANALOG_CENTER - ANALOG_THRESHOLD)
      cur_buttons |= SCE_CTRL_UP;
    if (pad.ly > ANALOG_CENTER + ANALOG_THRESHOLD)
      cur_buttons |= SCE_CTRL_DOWN;
    if (pad.lx < ANALOG_CENTER - ANALOG_THRESHOLD)
      cur_buttons |= SCE_CTRL_LEFT;
    if (pad.lx > ANALOG_CENTER + ANALOG_THRESHOLD)
      cur_buttons |= SCE_CTRL_RIGHT;

    changed_buttons = old_buttons ^ cur_buttons;
    old_buttons = cur_buttons;

    for (int i = 0; i < sizeof(mapping) / sizeof(ButtonMapping); i++) {
      if (changed_buttons & mapping[i].sce_button)
        Java_com_sega_CrazyTaxi_GL2JNILib_onJoyButton(fake_env, NULL, mapping[i].android_button, !!(cur_buttons & mapping[i].sce_button), 1);
    }

    //SceMotionSensorState sensor;
    //sceMotionGetSensorState(&sensor, 1);
    //taxi_game_accelerometer(sensor.accelerometer.x, sensor.accelerometer.y, sensor.accelerometer.z);

    Java_com_sega_CrazyTaxi_GL2JNILib_step();
    vglSwapBuffers(GL_FALSE);

    // Handling vibration
    if (rumble_tick != 0) {
      if (sceKernelGetProcessTimeWide() - rumble_tick > 500000) StopRumble(); // 0.5 sec
    }
  }

  return 0;
}
