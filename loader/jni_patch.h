#ifndef __JNI_PATCH_H__
#define __JNI_PATCH_H__

extern char fake_vm[0x1000];
extern char fake_env[0x1000];

void jni_load(void);

#endif
