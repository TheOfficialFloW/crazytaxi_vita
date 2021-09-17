/* jni_patch.c -- Fake Java Native Interface
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2/io/fcntl.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "main.h"
#include "config.h"
#include "so_util.h"

enum MethodIDs {
  UNKNOWN = 0,
  HAS_VIBRATOR,
  GET_LOCALE,
  GET_MODEL,
  GET_FILES_DIR,
  GET_PACKAGE_PATH,
  GET_LOCAL_PATH,
  GET_REGION_CODE,
  GET_LANGUAGE_CODE,
  GET_VALUE_DATA_STRING,
} MethodIDs;

typedef struct {
  char *name;
  enum MethodIDs id;
} NameToMethodID;

static NameToMethodID name_to_method_ids[] = {
  { "hasVibrator", HAS_VIBRATOR },
  { "getLocale", GET_LOCALE },
  { "getModel", GET_MODEL },
  { "getFilesDir", GET_FILES_DIR },
  { "getPackagePath", GET_PACKAGE_PATH },
  { "getLocalPath", GET_LOCAL_PATH },
  { "getRegionCode", GET_REGION_CODE },
  { "getLanguageCode", GET_LANGUAGE_CODE },
  { "getValueDataString", GET_VALUE_DATA_STRING },
};

char fake_vm[0x1000];
char fake_env[0x1000];

int GetMethodID(void *env, void *class, const char *name, const char *sig) {
  for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
    if (strcmp(name, name_to_method_ids[i].name) == 0) {
      return name_to_method_ids[i].id;
    }
  }

  return UNKNOWN;
}

int CallBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  return 0;
}

float CallFloatMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  return 0.0f;
}

int CallIntMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  return 0;
}

void *CallObjectMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  switch (methodID) {
    case GET_LOCAL_PATH:
      return DATA_PATH;
    default:
      return NULL;
  }
  return NULL;
}

void CallVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  return;
}

int GetFieldID(void *env, void *clazz, const char *name, const char *sig) {
  return 0;
}

int GetObjectField(void *env, void *obj, int fieldID) {
  return 0;
}

int GetStaticMethodID(void *env, void *class, const char *name, const char *sig) {
  for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
    if (strcmp(name, name_to_method_ids[i].name) == 0)
      return name_to_method_ids[i].id;
  }

  return UNKNOWN;
}

char *getLocale(void) {
  int lang = -1;
  sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
  switch (lang) {
    case SCE_SYSTEM_PARAM_LANG_FRENCH:
      return "fr";
    case SCE_SYSTEM_PARAM_LANG_SPANISH:
      return "es";
    case SCE_SYSTEM_PARAM_LANG_GERMAN:
      return "de";
    case SCE_SYSTEM_PARAM_LANG_ITALIAN:
      return "it";
    case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
    case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR:
      return "pt";
    case SCE_SYSTEM_PARAM_LANG_RUSSIAN:
      return "ru";
    default:
      return "en";
  }
}

void *CallStaticObjectMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  switch (methodID) {
    case GET_FILES_DIR:
      return DATA_PATH;
    case GET_PACKAGE_PATH:
      return DATA_PATH "/main.obb";
    case GET_LOCALE:
      return getLocale();
    case GET_LANGUAGE_CODE:
      return "en";
    case GET_REGION_CODE:
      return "US";
    case GET_MODEL:
      return "KFTHWI"; // for 60FPS
    case GET_VALUE_DATA_STRING:
      return "";
    default:
      return NULL;
  }
}

int CallStaticBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  switch (methodID) {
    case HAS_VIBRATOR:
      return 0;
    default:
      return 0;
  }
}

int CallStaticIntMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
  switch (methodID) {
    default:
      return 0;
  }
}

void CallStaticVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
}

char *NewStringUTF(void *env, char *bytes) {
  return bytes;
}

char *GetStringUTFChars(void *env, char *string, int *isCopy) {
  return string;
}

int GetArrayLength(void *env, void *array) {
  return 1;
}

void *GetObjectArrayElement(void *env, void *array, int index) {
  return array;
}

int dummy_array[2];
void *GetIntArrayElements(void *env, void *array, int *isCopy) {
  return &dummy_array;
}

void *NewGlobalRef(void) {
  return (void *)0x42424242;
}

int GetEnv(void *vm, void **env, int r2) {
  memset(fake_env, 'A', sizeof(fake_env));
  *(uintptr_t *)(fake_env + 0x00) = (uintptr_t)fake_env; // just point to itself...
  *(uintptr_t *)(fake_env + 0x18) = (uintptr_t)ret0; // FindClass
  *(uintptr_t *)(fake_env + 0x54) = (uintptr_t)NewGlobalRef;
  *(uintptr_t *)(fake_env + 0x5C) = (uintptr_t)ret0; // DeleteLocalRef
  *(uintptr_t *)(fake_env + 0x84) = (uintptr_t)GetMethodID;
  *(uintptr_t *)(fake_env + 0x8C) = (uintptr_t)CallObjectMethodV;
  *(uintptr_t *)(fake_env + 0x98) = (uintptr_t)CallBooleanMethodV;
  *(uintptr_t *)(fake_env + 0xC8) = (uintptr_t)CallIntMethodV;
  *(uintptr_t *)(fake_env + 0xE0) = (uintptr_t)CallFloatMethodV;
  *(uintptr_t *)(fake_env + 0xF8) = (uintptr_t)CallVoidMethodV;
  *(uintptr_t *)(fake_env + 0x178) = (uintptr_t)GetFieldID;
  *(uintptr_t *)(fake_env + 0x17C) = (uintptr_t)GetObjectField;
  *(uintptr_t *)(fake_env + 0x1C4) = (uintptr_t)GetStaticMethodID;
  *(uintptr_t *)(fake_env + 0x1CC) = (uintptr_t)CallStaticObjectMethodV;
  *(uintptr_t *)(fake_env + 0x1D8) = (uintptr_t)CallStaticBooleanMethodV;
  *(uintptr_t *)(fake_env + 0x208) = (uintptr_t)CallStaticIntMethodV;
  *(uintptr_t *)(fake_env + 0x238) = (uintptr_t)CallStaticVoidMethodV;
  *(uintptr_t *)(fake_env + 0x28C) = (uintptr_t)NewStringUTF;
  *(uintptr_t *)(fake_env + 0x2A4) = (uintptr_t)GetStringUTFChars;
  *(uintptr_t *)(fake_env + 0x2AC) = (uintptr_t)GetArrayLength;
  *(uintptr_t *)(fake_env + 0x2B4) = (uintptr_t)GetObjectArrayElement;
  *(uintptr_t *)(fake_env + 0x2EC) = (uintptr_t)GetIntArrayElements;
  *(uintptr_t *)(fake_env + 0x30C) = (uintptr_t)ret0;
  *env = fake_env;
  return 0;
}

int AttachCurrentThread(void *vm, void **p_env, void *thr_args) {
  GetEnv(vm, p_env, 0);
  return 0;
}

void jni_load(void) {
  memset(fake_vm, 'A', sizeof(fake_vm));
  *(uintptr_t *)(fake_vm + 0x00) = (uintptr_t)fake_vm; // just point to itself...
  *(uintptr_t *)(fake_vm + 0x10) = (uintptr_t)AttachCurrentThread;
  *(uintptr_t *)(fake_vm + 0x18) = (uintptr_t)GetEnv;

  int (* JNI_OnLoad)(void *vm, void *reserved) = (void *)so_symbol(&crazytaxi_mod, "JNI_OnLoad");
  JNI_OnLoad(fake_vm, NULL);
}
