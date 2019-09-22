//  match_in_old.cpp
//  sync_client
//
//  Created by housisong on 2019-09-22.
//  Copyright © 2019 sisong. All rights reserved.

#include "match_in_old.h"
#include <algorithm> //sort, equal_range
#include "../../libHDiffPatch/HDiff/private_diff/mem_buf.h"
#include "../../libHDiffPatch/HDiff/private_diff/limit_mem_diff/adler_roll.h"
#include "../../libHDiffPatch/HDiff/private_diff/limit_mem_diff/bloom_filter.h"

using namespace hdiff_private;
typedef unsigned char TByte;

struct TIndex_comp{
    inline TIndex_comp(const roll_uint_t* _blocks) :blocks(_blocks){ }
    template<class TIndex>
    inline bool operator()(TIndex x,TIndex y)const{
        return blocks[x]<blocks[y];
    }
    private:
    const roll_uint_t* blocks;
};

struct TDigest_comp{
    inline explicit TDigest_comp(const roll_uint_t* _blocks):blocks(_blocks){ }
    struct TDigest{
        roll_uint_t value;
        inline explicit TDigest(roll_uint_t _value):value(_value){}
    };
    template<class TIndex> inline
    bool operator()(const TIndex& x,const TDigest& y)const { return blocks[x]<y.value; }
    template<class TIndex> inline
    bool operator()(const TDigest& x,const TIndex& y)const { return x.value<blocks[y]; }
    template<class TIndex> inline
    bool operator()(const TIndex& x, const TIndex& y)const { return blocks[x]<blocks[y]; }
protected:
    const roll_uint_t* blocks;
};


struct TOldDataCache {
    TOldDataCache(const hpatch_TStreamInput* oldStream,uint32_t kMatchBlockSize,
                  hpatch_TChecksum* strongChecksumPlugin):m_checksumHandle(0){
        //size_t cacheSize=kMatchBlockSize*2;
        //cacheSize=(cacheSize>=kMinCacheBufSize)?cacheSize:kMinCacheBufSize;
        m_kMatchBlockSize=kMatchBlockSize;
        m_strongChecksumPlugin=strongChecksumPlugin;
        m_checksumHandle=strongChecksumPlugin->open(strongChecksumPlugin);
        m_strongChecksum_buf.realloc((size_t)strongChecksumPlugin->checksumByteSize());
        m_cache.realloc(oldStream->streamSize);
        oldStream->read(oldStream,0,m_cache.data(),m_cache.data_end());
        if (oldStream->streamSize>=kMatchBlockSize){
            m_cur=m_cache.data();
            m_roolHash=roll_hash_start(m_cur,kMatchBlockSize);
        }else{
            m_cur=m_cache.data_end();
        }
    }
    ~TOldDataCache(){
        if (m_checksumHandle)
            m_strongChecksumPlugin->close(m_strongChecksumPlugin,m_checksumHandle);
    }
    inline bool isEnd()const{ return m_cur==m_cache.data_end(); }
    inline roll_uint_t hashValue()const{ return m_roolHash; }
    inline void roll(){
        const TByte* curIn=m_cur+m_kMatchBlockSize;
        if (curIn!=m_cache.data_end()){
            m_roolHash=roll_hash_roll(m_roolHash,m_kMatchBlockSize,*m_cur,*curIn);
            ++m_cur;
        }else{
            m_cur=m_cache.data_end();
        }
    }
    inline const TByte* calcPartStrongChecksum(){
        return _calcPartStrongChecksum(m_cur,m_kMatchBlockSize);
    }
    inline const TByte* calcLastPartStrongChecksum(size_t lastNewNodeSize){
        return _calcPartStrongChecksum(m_cache.data_end()-lastNewNodeSize,lastNewNodeSize);
    }
    inline hpatch_StreamPos_t curOldPos()const{
        return m_cur-m_cache.data();
    }
    TAutoMem                m_cache;
    TAutoMem                m_strongChecksum_buf;
    const TByte*            m_cur;
    roll_uint_t             m_roolHash;
    uint32_t                m_kMatchBlockSize;
    hpatch_TChecksum*       m_strongChecksumPlugin;
    hpatch_checksumHandle   m_checksumHandle;
    
    inline const TByte* _calcPartStrongChecksum(const TByte* buf,size_t bufSize){
        m_strongChecksumPlugin->begin(m_checksumHandle);
        m_strongChecksumPlugin->append(m_checksumHandle,buf,buf+bufSize);
        m_strongChecksumPlugin->end(m_checksumHandle,m_strongChecksum_buf.data(),
                                    m_strongChecksum_buf.data_end());
        toPartChecksum(m_strongChecksum_buf.data(),m_strongChecksum_buf.data(),m_strongChecksum_buf.size());
        return m_strongChecksum_buf.data();
    }
};

//cpp code used stdexcept
void matchNewDataInOld(hpatch_StreamPos_t* out_newDataPoss,uint32_t* out_needSyncCount,
                       const TNewDataSyncInfo* newSyncInfo,const hpatch_TStreamInput* oldStream,
                       hpatch_TChecksum* strongChecksumPlugin){
    uint32_t kMatchBlockSize=newSyncInfo->kMatchBlockSize;
    uint32_t kBlockCount=(uint32_t)TNewDataSyncInfo_blockCount(newSyncInfo);

    TAutoMem _mem(kBlockCount*sizeof(uint32_t));
    uint32_t* sorted_newIndexs=(uint32_t*)_mem.data();
    TBloomFilter<roll_uint_t> filter; filter.init(kBlockCount);
    for (uint32_t i=0; i<kBlockCount; ++i){
        out_newDataPoss[i]=kBlockType_needSync;
        sorted_newIndexs[i]=i;
        filter.insert(newSyncInfo->rollHashs[i]);
    }
    {
        TIndex_comp icomp(newSyncInfo->rollHashs);
        std::sort(sorted_newIndexs,sorted_newIndexs+kBlockCount,icomp);
    }

    TOldDataCache oldData(oldStream,kMatchBlockSize,strongChecksumPlugin);
    TDigest_comp dcomp(newSyncInfo->rollHashs);
    uint32_t matchedCount=0;
    for (;!oldData.isEnd();oldData.roll()) {
        roll_uint_t digest=oldData.hashValue();
        if (!filter.is_hit(digest)) continue;
        
        typename TDigest_comp::TDigest digest_value(digest);
        std::pair<const uint32_t*,const uint32_t*>
            range=std::equal_range(sorted_newIndexs,sorted_newIndexs+kBlockCount,digest_value,dcomp);
        if (range.first!=range.second){
            const TByte* oldPartStrongChecksum=0;
            do {
                uint32_t newBlockIndex=*range.first;
                if (out_newDataPoss[newBlockIndex]==kBlockType_needSync){
                    if (oldPartStrongChecksum==0)
                        oldPartStrongChecksum=oldData.calcPartStrongChecksum();
                    const TByte* newPairStrongChecksum = newSyncInfo->partChecksums
                                                    + newBlockIndex*(size_t)kPartStrongChecksumByteSize;
                    if (0==memcmp(oldPartStrongChecksum,newPairStrongChecksum,kPartStrongChecksumByteSize)){
                        out_newDataPoss[newBlockIndex]=oldData.curOldPos();
                        matchedCount++;
                    }
                }
                ++range.first;
            }while (range.first!=range.second);
        }
    }
    _mem.clear();
    
    //try match last newBlock data
    if ((kBlockCount>0)&&(out_newDataPoss[kBlockCount-1]==kBlockType_needSync)){
        size_t lastNewNodeSize=(size_t)(newSyncInfo->newDataSize%kMatchBlockSize);
        if ((lastNewNodeSize>0)&&(oldStream->streamSize>=lastNewNodeSize)){
            uint32_t newBlockIndex=kBlockCount-1;
            const TByte* oldPartStrongChecksum = oldData.calcLastPartStrongChecksum(lastNewNodeSize);
            const TByte* newPairStrongChecksum = newSyncInfo->partChecksums
                                                + newBlockIndex*(size_t)kPartStrongChecksumByteSize;
            if (0==memcmp(oldPartStrongChecksum,newPairStrongChecksum,kPartStrongChecksumByteSize)){
                out_newDataPoss[newBlockIndex]=oldStream->streamSize-lastNewNodeSize;
                ++matchedCount;
            }
        }
    }
    *out_needSyncCount=kBlockCount-matchedCount;
}

