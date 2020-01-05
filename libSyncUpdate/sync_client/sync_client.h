//  sync_client.h
//  sync_client
//  Created by housisong on 2019-09-18.
/*
 The MIT License (MIT)
 Copyright (c) 2019-2020 HouSisong
 
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:
 
 The above copyright notice and this permission notice shall be
 included in all copies of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef sync_client_h
#define sync_client_h
#include "sync_client_type.h"
    
typedef enum TSyncClient_resultType{
    kSyncClient_ok,
    kSyncClient_optionsError, //cmdline error
    kSyncClient_memError,
    kSyncClient_tempFileError,
    kSyncClient_pathTypeError,
    kSyncClient_newSyncInfoTypeError,
    kSyncClient_noStrongChecksumPluginError,
    kSyncClient_strongChecksumByteSizeError,
    kSyncClient_noDecompressPluginError,
    kSyncClient_newSyncInfoDataError,
    kSyncClient_newSyncInfoChecksumError,
    kSyncClient_newSyncInfoOpenError,
    kSyncClient_newSyncInfoCloseError,
    kSyncClient_oldFileOpenError,
    kSyncClient_oldFileCloseError,
    kSyncClient_newFileCreateError,
    kSyncClient_newFileCloseError,
    kSyncClient_matchNewDataInOldError,
    kSyncClient_readSyncDataError,
    kSyncClient_decompressError,
    kSyncClient_readOldDataError,
    kSyncClient_writeNewDataError,
    kSyncClient_strongChecksumOpenError,
    kSyncClient_checksumSyncDataError,
} TNewDataSyncInfo_resultType;
    
    
    typedef struct TNeedSyncInfo{
        uint32_t needSyncCount;
        uint32_t needCacheSyncCount;
        hpatch_StreamPos_t needSyncSize;
        hpatch_StreamPos_t needCacheSyncSize;
    } TNeedSyncInfo;
    
typedef hpatch_StreamPos_t TSyncDataType;
static const TSyncDataType kSyncDataType_needSync=~(TSyncDataType)0; // download, default
//                                                          other value mead: cache index
typedef struct TSyncPatchChecksumSet{
    bool    isChecksumNewSyncInfo;
    bool    isChecksumNewSyncData;
} TSyncPatchChecksumSet;

struct TDownloadCacheIO{
    // .read_writed can't null
    hpatch_TStreamOutput* streamIO;
    bool (*deleteCacheIO)(const struct TDownloadCacheIO* cacheIO);
};

typedef struct ISyncPatchListener{
    void*             import;
    TSyncPatchChecksumSet checksumSet;
    hpatch_TDecompress* (*findDecompressPlugin)(ISyncPatchListener* listener,const char* compressType);
    hpatch_TChecksum*   (*findChecksumPlugin)  (ISyncPatchListener* listener,const char* strongChecksumType);
    //needSyncMsg can null; return a stream I/O for cache repeat downloaded data, can return null;
    const TDownloadCacheIO*  (*needSyncMsg)    (ISyncPatchListener* listener,const TNeedSyncInfo* needSyncInfo);
    //needSyncDataMsg can null; called befor all readSyncData called;
    void (*needSyncDataMsg)(ISyncPatchListener* listener,hpatch_StreamPos_t posInNewSyncData,
                            uint32_t syncDataSize,TSyncDataType samePosInNewSyncData);
    //download data
    bool (*readSyncData)   (ISyncPatchListener* listener,hpatch_StreamPos_t posInNewSyncData,
                            uint32_t syncDataSize,TSyncDataType cacheIndex,unsigned char* out_syncDataBuf);
} ISyncPatchListener;

int  TNewDataSyncInfo_open_by_file(TNewDataSyncInfo* self,const char* newSyncInfoFile,
                                   ISyncPatchListener* listener);
int  TNewDataSyncInfo_open        (TNewDataSyncInfo* self,const hpatch_TStreamInput* newSyncInfo,
                                   ISyncPatchListener* listener);
void TNewDataSyncInfo_close       (TNewDataSyncInfo* self);

int sync_patch_file2file(ISyncPatchListener* listener,const char* outNewFile,const char* oldFile,
                         const char* newSyncInfoFile,int threadNum=0);

int sync_patch(ISyncPatchListener* listener,const hpatch_TStreamOutput* out_newStream,
               const hpatch_TStreamInput* oldStream,const TNewDataSyncInfo* newSyncInfo,int threadNum=0);
        
#endif // sync_client_h
