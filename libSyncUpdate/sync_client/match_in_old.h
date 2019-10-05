//  match_in_old.h
//  sync_client
//  Created by housisong on 2019-09-22.
/*
 The MIT License (MIT)
 Copyright (c) 2019-2019 HouSisong
 
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
#ifndef match_in_old_h
#define match_in_old_h
#include "sync_client_type.h"
#include "../../libHDiffPatch/HPatch/checksum_plugin.h"

static const hpatch_StreamPos_t kBlockType_needSync =~(hpatch_StreamPos_t)0; //download, default
//                                     value[0--oldDataSize) mead: this data read from oldStream
//   value[oldDataSize--oldDataSize+needCacheSyncCount) mead: this data have same data,can cache

//used stdexcept
void matchNewDataInOld(hpatch_StreamPos_t* out_newDataPoss,const TNewDataSyncInfo* newSyncInfo,
                       const hpatch_TStreamInput* oldStream,hpatch_TChecksum* strongChecksumPlugin,int threadNum=0);

#endif // match_in_old_h
