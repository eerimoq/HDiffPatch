//  match_in_new.cpp
//  sync_server
//  Created by housisong on 2019-09-25.
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
#include "match_in_new.h"
#include <algorithm> //sort, equal_range
#include "../../libHDiffPatch/HDiff/private_diff/mem_buf.h"

#define check(value,info) { if (!(value)) { throw std::runtime_error(info); } }
#define checkv(value)     check(value,"check "#value" error!")

using namespace hdiff_private;
typedef unsigned char TByte;

template<class tm_roll_uint>
struct TIndex_comp{
    inline explicit TIndex_comp(const tm_roll_uint* _blocks):blocks(_blocks){ }
    struct TDigest{
        tm_roll_uint value;
        uint32_t     index;
        inline explicit TDigest(tm_roll_uint _value,uint32_t _index)
            :value(_value),index(_index){}
    };
    template<class TIndex> inline
    bool operator()(const TIndex& x,const TDigest& y)const {
        tm_roll_uint hx=blocks[x];
        if (hx!=y.value)
            return hx<y.value;
        else
            return (x>=y.index); //for: assert(newBlockIndex<i);
    }
    template<class TIndex> inline
    bool operator()(const TDigest& x,const TIndex& y)const {
        return x.value<blocks[y];
    }
    template<class TIndex> inline
    bool operator()(const TIndex& x, const TIndex& y)const {//for sort
        tm_roll_uint hx=blocks[x];
        tm_roll_uint hy=blocks[y];
        if (hx!=hy)
            return hx<hy; //value sort
        else
            return x>y; //index sort
    }
protected:
    const tm_roll_uint* blocks;
};

template<class tm_roll_uint>
void tm_matchNewDataInNew(TNewDataSyncInfo* newSyncInfo){
    uint32_t kBlockCount=(uint32_t)TNewDataSyncInfo_blockCount(newSyncInfo);
    const unsigned char* partChecksums=newSyncInfo->partChecksums;
    TSameNewDataPair* samePairList=newSyncInfo->samePairList;

    TAutoMem _mem(kBlockCount*(size_t)sizeof(uint32_t));
    uint32_t* sorted_newIndexs=(uint32_t*)_mem.data();
    for (uint32_t i=0; i<kBlockCount; ++i){
        sorted_newIndexs[i]=i;
    }
    {
        TIndex_comp<tm_roll_uint> icomp((tm_roll_uint*)newSyncInfo->rollHashs);
        std::sort(sorted_newIndexs,sorted_newIndexs+kBlockCount,icomp);
    }

    TIndex_comp<tm_roll_uint> dcomp((tm_roll_uint*)newSyncInfo->rollHashs);
    uint32_t matchedCount=0;
    const TByte* curChecksum=partChecksums;
    for (uint32_t i=0; i<kBlockCount; ++i,curChecksum+=kPartStrongChecksumByteSize){
        tm_roll_uint digest=((tm_roll_uint*)newSyncInfo->rollHashs)[i];
        typename TIndex_comp<tm_roll_uint>::TDigest digest_value(digest,i);
        std::pair<const uint32_t*,const uint32_t*>
            range=std::equal_range(sorted_newIndexs,sorted_newIndexs+kBlockCount,digest_value,dcomp);
        for (;range.first!=range.second; ++range.first) {
            uint32_t newBlockIndex=*range.first;
            //assert(newBlockIndex<i); // not need: if (newBlockIndex>=i) continue;
            const TByte* newChecksum=partChecksums+newBlockIndex*(size_t)kPartStrongChecksumByteSize;
            if (0==memcmp(newChecksum,curChecksum,kPartStrongChecksumByteSize)){
                samePairList[matchedCount].curIndex=i;
                samePairList[matchedCount].sameIndex=newBlockIndex;
                ++matchedCount;
                break;
            }
        }
    }
    newSyncInfo->samePairCount=matchedCount;
}


void matchNewDataInNew(TNewDataSyncInfo* newSyncInfo){
    if (newSyncInfo->is32Bit_rollHash)
        tm_matchNewDataInNew<uint32_t>(newSyncInfo);
    else
        tm_matchNewDataInNew<uint64_t>(newSyncInfo);
}
