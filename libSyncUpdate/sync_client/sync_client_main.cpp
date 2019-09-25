//  sync_client_main.cpp
//  sync_client
//
//  Created by housisong on 2019-09-18.
//  Copyright © 2019 sisong. All rights reserved.
#include "sync_client.h"
#include "download_emulation.h"
#include "../../_clock_for_demo.h"

#ifndef _IS_NEED_DEFAULT_CompressPlugin
#   define _IS_NEED_DEFAULT_CompressPlugin 1
#endif
#if (_IS_NEED_DEFAULT_CompressPlugin)
//===== select needs decompress plugins or change to your plugin=====
#   define _CompressPlugin_zlib
#   define _CompressPlugin_lzma
#endif

#include "../../decompress_plugin_demo.h"

#ifndef _IS_NEED_DEFAULT_ChecksumPlugin
#   define _IS_NEED_DEFAULT_ChecksumPlugin 1
#endif
#if (_IS_NEED_DEFAULT_ChecksumPlugin)
//===== select needs checksum plugins or change to your plugin=====
#   define _ChecksumPlugin_md5      // ? 128 bit effective
#endif

#include "../../checksum_plugin_demo.h"

//ISyncPatchListener::isChecksumNewSyncInfo
static bool isChecksumNewSyncInfo(ISyncPatchListener* listener){
    return true;
}
//ISyncPatchListener::findDecompressPlugin
static hpatch_TDecompress* findDecompressPlugin(ISyncPatchListener* listener,const char* compressType){
    if (compressType==0) return 0; //ok
    hpatch_TDecompress* decompressPlugin=0;
#ifdef  _CompressPlugin_zlib
    if ((!decompressPlugin)&&zlibDecompressPlugin.is_can_open(compressType))
        decompressPlugin=&zlibDecompressPlugin;
#endif
#ifdef  _CompressPlugin_lzma
    if ((!decompressPlugin)&&lzmaDecompressPlugin.is_can_open(compressType))
        decompressPlugin=&lzmaDecompressPlugin;
#endif
    if (decompressPlugin==0){
        printf("sync_patch can't decompress type: \"%s\"\n",compressType);
        return 0; //unsupport error
    }else{
        printf("sync_patch run with decompress plugin: \"%s\"\n",compressType);
        return decompressPlugin; //ok
    }
}
//ISyncPatchListener::findChecksumPlugin
static hpatch_TChecksum* findChecksumPlugin(ISyncPatchListener* listener,const char* strongChecksumType){
    if (strongChecksumType==0) return 0; //ok
    hpatch_TChecksum* strongChecksumPlugin=0;
#ifdef  _ChecksumPlugin_md5
    if ((!strongChecksumPlugin)&&(0==strcmp(strongChecksumType,md5ChecksumPlugin.checksumType())))
        strongChecksumPlugin=&md5ChecksumPlugin;
#endif
    if (strongChecksumPlugin==0){
        printf("sync_patch can't found checksum type: \"%s\"\n",strongChecksumType);
        return 0; //unsupport error
    }else{
        printf("sync_patch run with strongChecksum plugin: \"%s\"\n",strongChecksumType);
        return strongChecksumPlugin; //ok
    }
}

int main(int argc, const char * argv[]) {
    double time0=clock_s();
    if (argc!=1+4){
        printf("emulation sync_patch: out_newPath newSyncInfoPath oldPath test_newSyncDataPath\n");
        return -1;
    }
    
    const char* oldPath=argv[1];
    const char* newSyncInfoPath=argv[2];
    const char* test_newSyncDataPath=argv[3]; //for test
    const char* out_newPath=argv[4];
    ISyncPatchListener emulation;
    memset(&emulation,0,sizeof(emulation));
    if (!downloadEmulation_open_by_file(&emulation,test_newSyncDataPath))
        return kSyncClient_readSyncDataError;
    emulation.findChecksumPlugin=findChecksumPlugin;
    emulation.findDecompressPlugin=findDecompressPlugin;
    emulation.isChecksumNewSyncInfo=isChecksumNewSyncInfo;
    
    int result=sync_patch_by_file(out_newPath,oldPath,newSyncInfoPath,&emulation);
    downloadEmulation_close(&emulation);
    double time1=clock_s();
    printf("emulation sync_patch time: %.3f s\n\n",(time1-time0));
    return result;
}
