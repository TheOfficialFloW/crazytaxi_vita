cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(CRAZYTAXI.elf C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "Crazy Taxi Classic")
set(VITA_TITLEID  "CRAZYTAXI")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wl,-q,--wrap,memcpy,--wrap,memmove,--wrap,memset,--allow-multiple-definition -fdiagnostics-color=always -D_GNU_SOURCE -Wall -O3 -mfloat-abi=softfp")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")

add_executable(CRAZYTAXI.elf
  loader/main.c
  loader/dialog.c
  loader/so_util.c
  loader/jni_patch.c
  loader/sha1.c
)

target_link_libraries(CRAZYTAXI.elf
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  stdc++
  sndfile
  vorbis
  vorbisenc
  vorbisfile
  ogg
  FLAC
  OpenSLES
  openal
  vitaGL
  vitashark
  SceShaccCgExt
  m
  mathneon
  taihen_stub
  kubridge_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceAudioIn_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceKernelDmacMgr_stub
  SceGxm_stub
  SceShaccCg_stub
  SceSysmodule_stub
  ScePower_stub
  SceTouch_stub
  SceVshBridge_stub
)

vita_create_self(eboot.bin CRAZYTAXI.elf UNSAFE)
vita_create_vpk(CRAZYTAXI.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
       ${CMAKE_SOURCE_DIR}/cg/0f81aafba1fe95c1bbbadf14b6ac5b1c1f6e57e1.cg cg/0f81aafba1fe95c1bbbadf14b6ac5b1c1f6e57e1.cg
       ${CMAKE_SOURCE_DIR}/cg/2cf7b3564cf4c3660cf7637d470b8485a79f9827.cg cg/2cf7b3564cf4c3660cf7637d470b8485a79f9827.cg
       ${CMAKE_SOURCE_DIR}/cg/3fa8879b4dbc55d566de2023fbc60470385a310b.cg cg/3fa8879b4dbc55d566de2023fbc60470385a310b.cg
       ${CMAKE_SOURCE_DIR}/cg/4b6f29c631d7ea36e6fe2aad196783c648f2e2b1.cg cg/4b6f29c631d7ea36e6fe2aad196783c648f2e2b1.cg
       ${CMAKE_SOURCE_DIR}/cg/5d917223a3f68a9a11ad14671fe16c803bf4d127.cg cg/5d917223a3f68a9a11ad14671fe16c803bf4d127.cg
       ${CMAKE_SOURCE_DIR}/cg/6fb7bcdd31434fd18d723ae7b1574a2bf85562ac.cg cg/6fb7bcdd31434fd18d723ae7b1574a2bf85562ac.cg
       ${CMAKE_SOURCE_DIR}/cg/7d8937114687d6125a735bdd1422f4b4fdc62e8c.cg cg/7d8937114687d6125a735bdd1422f4b4fdc62e8c.cg
       ${CMAKE_SOURCE_DIR}/cg/8c89623270c409b8afd002726956d0d46bcaa6bf.cg cg/8c89623270c409b8afd002726956d0d46bcaa6bf.cg
       ${CMAKE_SOURCE_DIR}/cg/67fdd997d7bb747c3c2cddc56672409e0a1bddfc.cg cg/67fdd997d7bb747c3c2cddc56672409e0a1bddfc.cg
       ${CMAKE_SOURCE_DIR}/cg/94d98dc02c1285677790b7dbdb07644317f414f7.cg cg/94d98dc02c1285677790b7dbdb07644317f414f7.cg
       ${CMAKE_SOURCE_DIR}/cg/215eea4f9d5f601c1df8f0409ddf4b6ae5db6c59.cg cg/215eea4f9d5f601c1df8f0409ddf4b6ae5db6c59.cg
       ${CMAKE_SOURCE_DIR}/cg/93462c3df6fc1888bd504c2e446123afe9f4f82b.cg cg/93462c3df6fc1888bd504c2e446123afe9f4f82b.cg
       ${CMAKE_SOURCE_DIR}/cg/a3ae8423eeae786723c1f7103c16df4a020ab0a9.cg cg/a3ae8423eeae786723c1f7103c16df4a020ab0a9.cg
       ${CMAKE_SOURCE_DIR}/cg/bb3dbe6f0d2484bff4b6c70a8a8ceaf76cba940d.cg cg/bb3dbe6f0d2484bff4b6c70a8a8ceaf76cba940d.cg
       ${CMAKE_SOURCE_DIR}/cg/f9c33151786a88a9ad36645c04f4e4584dfe8537.cg cg/f9c33151786a88a9ad36645c04f4e4584dfe8537.cg
)

add_custom_target(copy
  COMMAND cp eboot.bin E:/app/${VITA_TITLEID}/eboot.bin
  DEPENDS eboot.bin
)
