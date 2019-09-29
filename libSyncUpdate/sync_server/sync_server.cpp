//  sync_server.cpp
//  sync_server
//  Created by housisong on 2019-09-17.
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
#include "sync_server.h"
#include <stdexcept>
#include <vector>
#include <string>
#include "../../file_for_patch.h"
#include "match_in_new.h"
#include "../sync_client/mt_by_queue.h"

#define checki(value,info) { if (!(value)) { throw std::runtime_error(info); } }
#define check(value) checki(value,"check "#value" error!")

static void writeStream(const hpatch_TStreamOutput* out_stream,hpatch_StreamPos_t& outPos,
                       const TByte* buf,size_t byteSize){
    check(out_stream->write(out_stream,outPos,buf,buf+byteSize));
    outPos+=byteSize;
}
inline static void writeStream(const hpatch_TStreamOutput* out_stream,hpatch_StreamPos_t& outPos,
                              const std::vector<TByte>& buf){
    writeStream(out_stream,outPos,buf.data(),buf.size());
}

inline static void writeData(std::vector<TByte>& out_buf,const TByte* buf,size_t byteSize){
    out_buf.insert(out_buf.end(),buf,buf+byteSize);
}
inline static void writeCStr(std::vector<TByte>& out_buf,const char* str){
    writeData(out_buf,(const TByte*)str,strlen(str));
}

static void writeUInt(std::vector<TByte>& out_buf,
                      hpatch_StreamPos_t v,size_t byteSize){
    assert(sizeof(hpatch_StreamPos_t)>=byteSize);
    TByte buf[sizeof(hpatch_StreamPos_t)];
    size_t len=sizeof(hpatch_StreamPos_t);
    if (len>byteSize) len=byteSize;
    for (size_t i=0; i<len; ++i) {
        buf[i]=(TByte)v;
        v>>=8;
    }
    writeData(out_buf,buf,len);
}

template <class TUInt> inline static
void writeUInt(std::vector<TByte>& out_buf,TUInt v){
    writeUInt(out_buf,v,sizeof(v));
}

static void packUInt(std::vector<TByte>& out_buf,hpatch_StreamPos_t v){
    TByte buf[hpatch_kMaxPackedUIntBytes];
    TByte* curBuf=buf;
    check(hpatch_packUInt(&curBuf,buf+sizeof(buf),v));
    writeData(out_buf,buf,(curBuf-buf));
}

class _TAutoClose_checksumHandle{
public:
    inline _TAutoClose_checksumHandle(hpatch_TChecksum* cp,hpatch_checksumHandle ch)
    :_cp(cp),_ch(ch){}
    inline ~_TAutoClose_checksumHandle(){ _cp->close(_cp,_ch); }
private:
    hpatch_TChecksum*     _cp;
    hpatch_checksumHandle _ch;
};

inline static void _clear(std::vector<TByte>& buf){
    std::vector<TByte> _temp;
    _temp.swap(buf);
}

#define rollHashSize(self) (self->is32Bit_rollHash?sizeof(uint32_t):sizeof(uint64_t))

static bool TNewDataSyncInfo_saveTo(TNewDataSyncInfo*      self,
                                    const hpatch_TStreamOutput* out_stream,
                                    hpatch_TChecksum*      strongChecksumPlugin,
                                    const hdiff_TCompress* compressPlugin){
    const char* kSyncUpdateTypeVersion = "SyncUpdate19";
    if (compressPlugin){
        check(0==strcmp(compressPlugin->compressType(),self->compressType));
    }else{
        check(self->compressType==0); }
    check(0==strcmp(strongChecksumPlugin->checksumType(),self->strongChecksumType));
    hpatch_checksumHandle checksumInfo=strongChecksumPlugin->open(strongChecksumPlugin);
    check(checksumInfo!=0);
    _TAutoClose_checksumHandle _autoClose_checksumHandle(strongChecksumPlugin,checksumInfo);

    std::vector<TByte> head;
    {//head
        std::vector<TByte>& buf=head;
        writeCStr(buf,kSyncUpdateTypeVersion);
        buf.push_back('&');
        writeCStr(buf,self->strongChecksumType);
        buf.push_back('&');
        if (self->compressType)
            writeCStr(buf,self->compressType);
        buf.push_back('\0');//c string end tag
        
        packUInt(buf,self->kStrongChecksumByteSize);
        packUInt(buf,self->kMatchBlockSize);
        packUInt(buf,self->samePairCount);
        writeUInt(buf,self->is32Bit_rollHash);
        packUInt(buf,self->newDataSize);
        packUInt(buf,self->newSyncDataSize);
    }
    
    const uint32_t kBlockCount=(uint32_t)TNewDataSyncInfo_blockCount(self);
    std::vector<TByte> buf;
    {//samePairList
        uint32_t pre=0;
        for (size_t i=0;i<self->samePairCount;++i){
            const TSameNewDataPair& sp=self->samePairList[i];
            packUInt(buf,(uint32_t)(sp.curIndex-pre));
            packUInt(buf,(uint32_t)(sp.curIndex-sp.sameIndex));
            pre=sp.curIndex;
        }
    }
    if (compressPlugin){ //savedSizes
        uint32_t curPair=0;
        for (uint32_t i=0; i<kBlockCount; ++i){
            if ((curPair<self->samePairCount)
                &&(i==self->samePairList[curPair].curIndex)){ ++curPair; continue; }
            if (self->savedSizes[i]!=TNewDataSyncInfo_newDataBlockSize(self,i))
                packUInt(buf,self->savedSizes[i]);
            else
                packUInt(buf,(uint32_t)0);
        }
    }
    
    {//compress buf
        std::vector<TByte> cmbuf;
        if (compressPlugin){
            cmbuf.resize((size_t)compressPlugin->maxCompressedSize(buf.size()));
            size_t compressedSize=hdiff_compress_mem(compressPlugin,cmbuf.data(),cmbuf.data()+cmbuf.size(),
                                                     buf.data(),buf.data()+buf.size());
            check(compressedSize>0);
            if (compressedSize>=buf.size()) compressedSize=0; //not compressed
            cmbuf.resize(compressedSize);
        }
        
        //head +
        packUInt(head,buf.size());
        packUInt(head,cmbuf.size());
        if (cmbuf.size()>0){
            _clear(buf);
            buf.swap(cmbuf);
        }
    }
    {//newSyncInfoSize
        packUInt(head,(hpatch_StreamPos_t)0); //reservedDataSize , now not used
        //head +
        self->newSyncInfoSize = head.size() + sizeof(self->newSyncInfoSize) + buf.size();
        self->newSyncInfoSize +=(rollHashSize(self)+kPartStrongChecksumByteSize)
                                *(hpatch_StreamPos_t)(kBlockCount-self->samePairCount);
        self->newSyncInfoSize += kPartStrongChecksumByteSize;
        writeUInt(head,self->newSyncInfoSize);
        //end head
    }

    //out head buf
    #define _outBuf(_buf)    if (!_buf.empty()){ \
            strongChecksumPlugin->append(checksumInfo,_buf.data(),_buf.data()+_buf.size()); \
            writeStream(out_stream,outPos,_buf);  _buf.clear(); }
    hpatch_StreamPos_t outPos=0;
    strongChecksumPlugin->begin(checksumInfo);
    _outBuf(head); _clear(head);
    _outBuf(buf);
    
    {//rollHashs
        uint32_t curPair=0;
        bool is32Bit_rollHash=(0!=self->is32Bit_rollHash);
        uint32_t* rhashs32=(uint32_t*)self->rollHashs;
        uint64_t* rhashs64=(uint64_t*)self->rollHashs;
        for (size_t i=0; i<kBlockCount; ++i){
            if ((curPair<self->samePairCount)
                &&(i==self->samePairList[curPair].curIndex)){ ++curPair; continue; }
            if (is32Bit_rollHash)
                writeUInt(buf,rhashs32[i]);
            else
                writeUInt(buf,rhashs64[i]);
            if (buf.size()>=hpatch_kFileIOBufBetterSize)
                _outBuf(buf);
        }
    }
    {//partStrongChecksums
        uint32_t curPair=0;
        for (size_t i=0; i<kBlockCount; ++i){
            if ((curPair<self->samePairCount)
                &&(i==self->samePairList[curPair].curIndex)){ ++curPair; continue; }
            writeData(buf,self->partChecksums+i*(size_t)kPartStrongChecksumByteSize,
                      kPartStrongChecksumByteSize);
            if (buf.size()>=hpatch_kFileIOBufBetterSize)
                _outBuf(buf);
        }
    }
    _outBuf(buf); _clear(buf);
    
    {// out infoPartChecksum
        std::vector<TByte> checksumBuf(self->kStrongChecksumByteSize);
        strongChecksumPlugin->end(checksumInfo,checksumBuf.data(),checksumBuf.data()+checksumBuf.size());
        toPartChecksum(self->infoPartChecksum,checksumBuf.data(),checksumBuf.size());
        writeStream(out_stream,outPos,self->infoPartChecksum,kPartStrongChecksumByteSize);
        assert(outPos==self->newSyncInfoSize);
    }
    return true;
}

class CNewDataSyncInfo :public TNewDataSyncInfo{
public:
    inline uint32_t blockCount()const{
        hpatch_StreamPos_t result=TNewDataSyncInfo_blockCount(this);
        check(result==(uint32_t)result);
        return (uint32_t)result; }
    inline CNewDataSyncInfo(hpatch_TChecksum*      strongChecksumPlugin,
                            const hdiff_TCompress* compressPlugin,
                            hpatch_StreamPos_t newDataSize,uint32_t kMatchBlockSize){
        TNewDataSyncInfo_init(this);
        if (compressPlugin){
            this->_compressType.assign(compressPlugin->compressType());
            this->compressType=this->_compressType.c_str();
        }
        this->_strongChecksumType.assign(strongChecksumPlugin->checksumType());
        this->strongChecksumType=this->_strongChecksumType.c_str();
        this->kStrongChecksumByteSize=(uint32_t)strongChecksumPlugin->checksumByteSize();
        this->kMatchBlockSize=kMatchBlockSize;
        this->newDataSize=newDataSize;
        
        const uint32_t kBlockCount=this->blockCount();
        this->samePairCount=0;
        this->_samePairList.resize(kBlockCount);
        this->samePairList=this->_samePairList.data();
        
        this->is32Bit_rollHash=isCanUse32bitRollHash(newDataSize,kMatchBlockSize);
        this->_rollHashs.resize(this->is32Bit_rollHash?((kBlockCount+1)/2):kBlockCount);
        this->rollHashs=this->_rollHashs.data();
        
        this->_infoPartChecksum.resize(kPartStrongChecksumByteSize,0);
        this->infoPartChecksum=this->_infoPartChecksum.data();
        hpatch_StreamPos_t _checksumsBufSize=kBlockCount*(hpatch_StreamPos_t)kPartStrongChecksumByteSize;
        check(_checksumsBufSize==(size_t)_checksumsBufSize);
        this->_partStrongChecksums.resize((size_t)_checksumsBufSize);
        this->partChecksums=this->_partStrongChecksums.data();
        if (compressPlugin){
            this->_savedSizes.resize(kBlockCount);
            this->savedSizes=this->_savedSizes.data();
        }
    }
    ~CNewDataSyncInfo(){}
    void insetSamePair(uint32_t curIndex,uint32_t sameIndex){
        TSameNewDataPair& samePair=this->samePairList[this->samePairCount];
        samePair.curIndex=curIndex;
        samePair.sameIndex=sameIndex;
        ++this->samePairCount;
    }
private:
    std::string                 _compressType;
    std::string                 _strongChecksumType;
    std::vector<TByte>          _infoPartChecksum;
    std::vector<TSameNewDataPair> _samePairList;
    std::vector<uint32_t>       _savedSizes;
    std::vector<uint64_t>       _rollHashs;
    std::vector<TByte>          _partStrongChecksums;
    std::vector<TByte>          _newDataInsureStrongChecksums;
};

struct _TCreateDatas {
    const hpatch_TStreamInput*  newData;
    TNewDataSyncInfo*           out_newSyncInfo;
    const hpatch_TStreamOutput* out_newSyncData;
    const hdiff_TCompress*      compressPlugin;
    hpatch_TChecksum*           strongChecksumPlugin;
    uint32_t                    kMatchBlockSize;
    hpatch_StreamPos_t          curOutPos;
};

static void mt_create_sync_data(_TCreateDatas& cd,void* _mt=0,int threadIndex=0){
    const uint32_t kMatchBlockSize=cd.kMatchBlockSize;
    TNewDataSyncInfo*       out_newSyncInfo=cd.out_newSyncInfo;
    const hdiff_TCompress*  compressPlugin=cd.compressPlugin;
    hpatch_TChecksum*       strongChecksumPlugin=cd.strongChecksumPlugin;
    const uint32_t kBlockCount=(uint32_t)getBlockCount(out_newSyncInfo->newDataSize,kMatchBlockSize);
    std::vector<TByte> buf(kMatchBlockSize);
    std::vector<TByte> cmbuf(compressPlugin?((size_t)compressPlugin->maxCompressedSize(kMatchBlockSize)):0);
    const size_t checksumByteSize=strongChecksumPlugin->checksumByteSize();
    check((checksumByteSize==(uint32_t)checksumByteSize)
          &&(checksumByteSize>=kPartStrongChecksumByteSize)
          &&(checksumByteSize%kPartStrongChecksumByteSize==0));
    std::vector<TByte> checksumBlockData_buf(checksumByteSize);
    hpatch_checksumHandle checksumBlockData=strongChecksumPlugin->open(strongChecksumPlugin);
    check(checksumBlockData!=0);
    _TAutoClose_checksumHandle _autoClose_checksumHandle(strongChecksumPlugin,checksumBlockData);
    
    hpatch_StreamPos_t curReadPos=0;
    for (uint32_t i=0; i<kBlockCount; ++i,curReadPos+=kMatchBlockSize) {
#if (_IS_USED_MULTITHREAD)
        if (_mt) { if (!((TMt_by_queue*)_mt)->getWork(threadIndex,i)) continue; } //next work;
#endif
        size_t dataLen=buf.size();
        if (i==kBlockCount-1) dataLen=(size_t)(cd.newData->streamSize-curReadPos);
        {//read data
#if (_IS_USED_MULTITHREAD)
            TMt_by_queue::TAutoInputLocker _autoLocker((TMt_by_queue*)_mt);
#endif
            check(cd.newData->read(cd.newData,curReadPos,buf.data(),buf.data()+dataLen));
        }
        size_t backZeroLen=kMatchBlockSize-dataLen;
        if (backZeroLen>0)
            memset(buf.data()+dataLen,0,backZeroLen);
        
        uint64_t rollHash;
        //rool hash
        if (out_newSyncInfo->is32Bit_rollHash)
            rollHash=roll_hash_start((uint32_t*)0,buf.data(),kMatchBlockSize);
        else
            rollHash=roll_hash_start((uint64_t*)0,buf.data(),kMatchBlockSize);
        //strong hash
        strongChecksumPlugin->begin(checksumBlockData);
        strongChecksumPlugin->append(checksumBlockData,buf.data(),buf.data()+kMatchBlockSize);
        strongChecksumPlugin->end(checksumBlockData,checksumBlockData_buf.data(),
                                  checksumBlockData_buf.data()+checksumByteSize);
        toPartChecksum(checksumBlockData_buf.data(),checksumBlockData_buf.data(),checksumByteSize);
        //compress
        size_t compressedSize=0;
        if (compressPlugin){
            compressedSize=hdiff_compress_mem(compressPlugin,cmbuf.data(),cmbuf.data()+cmbuf.size(),
                                              buf.data(),buf.data()+dataLen);
            check(compressedSize>0);
            if (compressedSize+sizeof(uint32_t)>=dataLen)
                compressedSize=0; //not compressed
            //save compressed size
            check(compressedSize==(uint32_t)compressedSize);
        }
        {//save data
#if (_IS_USED_MULTITHREAD)
            TMt_by_queue::TAutoOutputLocker _autoLocker((TMt_by_queue*)_mt,threadIndex,i);
#endif
            if (out_newSyncInfo->is32Bit_rollHash)
                ((uint32_t*)out_newSyncInfo->rollHashs)[i]=(uint32_t)rollHash;
            else
                ((uint64_t*)out_newSyncInfo->rollHashs)[i]=rollHash;
            memcpy(out_newSyncInfo->partChecksums+i*(size_t)kPartStrongChecksumByteSize,
                   checksumBlockData_buf.data(),kPartStrongChecksumByteSize);
            if (compressPlugin){
                if (compressedSize>0)
                    out_newSyncInfo->savedSizes[i]=(uint32_t)compressedSize;
                else
                    out_newSyncInfo->savedSizes[i]=(uint32_t)dataLen;
            }
            
            if (compressedSize>0){
                if (cd.out_newSyncData)
                    writeStream(cd.out_newSyncData,cd.curOutPos, cmbuf.data(),compressedSize);
                out_newSyncInfo->newSyncDataSize+=compressedSize;
            }else{
                if (cd.out_newSyncData)
                    writeStream(cd.out_newSyncData,cd.curOutPos, buf.data(),dataLen);
                out_newSyncInfo->newSyncDataSize+=dataLen;
            }
        }
    }
}

#if (_IS_USED_MULTITHREAD)
struct TMt_threadDatas{
    _TCreateDatas*      createDatas;
    TMt_by_queue*       shareDatas;
};

static void _mt_threadRunCallBackProc(int threadIndex,void* workData){
    TMt_threadDatas* tdatas=(TMt_threadDatas*)workData;
    mt_create_sync_data(*tdatas->createDatas,tdatas->shareDatas,threadIndex);
    tdatas->shareDatas->finish();
    bool isMainThread=(threadIndex==tdatas->shareDatas->threadNum-1);
    if (isMainThread) tdatas->shareDatas->waitAllFinish();
}
#endif

static void _create_sync_data(_TCreateDatas& createDatas,size_t threadNum){
    check(createDatas.kMatchBlockSize>=kMatchBlockSize_min);
    if (createDatas.compressPlugin) check(createDatas.out_newSyncData!=0);
    
#if (_IS_USED_MULTITHREAD)
    if (threadNum>1){
        const uint32_t kBlockCount=(uint32_t)getBlockCount(createDatas.out_newSyncInfo->newDataSize,
                                                           createDatas.kMatchBlockSize);
        TMt_by_queue   shareDatas((int)threadNum,kBlockCount,true);
        TMt_threadDatas  tdatas;  memset(&tdatas,0,sizeof(tdatas));
        tdatas.shareDatas=&shareDatas;
        tdatas.createDatas=&createDatas;
        thread_parallel((int)threadNum,_mt_threadRunCallBackProc,&tdatas,1);
    }else
#endif
    {
        mt_create_sync_data(createDatas);
    }
    matchNewDataInNew(createDatas.out_newSyncInfo);
}

void create_sync_data(const hpatch_TStreamInput*  newData,
                      const hpatch_TStreamOutput* out_newSyncInfo,
                      const hpatch_TStreamOutput* out_newSyncData,
                      const hdiff_TCompress* compressPlugin,
                      hpatch_TChecksum*      strongChecksumPlugin,
                      uint32_t kMatchBlockSize,size_t threadNum){
    CNewDataSyncInfo newSyncInfo(strongChecksumPlugin,compressPlugin,
                                 newData->streamSize,kMatchBlockSize);
    _TCreateDatas  createDatas;
    createDatas.newData=newData;
    createDatas.out_newSyncInfo=&newSyncInfo;
    createDatas.out_newSyncData=out_newSyncData;
    createDatas.compressPlugin=compressPlugin;
    createDatas.strongChecksumPlugin=strongChecksumPlugin;
    createDatas.kMatchBlockSize=kMatchBlockSize;
    createDatas.curOutPos=0;
    _create_sync_data(createDatas,threadNum);
    check(TNewDataSyncInfo_saveTo(&newSyncInfo,out_newSyncInfo,
                                  strongChecksumPlugin,compressPlugin));
}

void create_sync_data(const char* newDataPath,
                      const char* out_newSyncInfoFile,
                      const char* out_newSyncDataFile,
                      const hdiff_TCompress* compressPlugin,
                      hpatch_TChecksum*      strongChecksumPlugin,
                      uint32_t kMatchBlockSize,size_t threadNum){
    hpatch_TFileStreamInput  newData;
    hpatch_TFileStreamOutput out_newSyncInfo;
    hpatch_TFileStreamOutput out_newSyncData;
    
    hpatch_TFileStreamInput_init(&newData);
    hpatch_TFileStreamOutput_init(&out_newSyncInfo);
    hpatch_TFileStreamOutput_init(&out_newSyncData);
    check(hpatch_TFileStreamInput_open(&newData,newDataPath));
    check(hpatch_TFileStreamOutput_open(&out_newSyncInfo,out_newSyncInfoFile,
                                        (hpatch_StreamPos_t)(-1)));
    if (out_newSyncDataFile)
        check(hpatch_TFileStreamOutput_open(&out_newSyncData,out_newSyncDataFile,
                                            (hpatch_StreamPos_t)(-1)));
    
    create_sync_data(&newData.base,&out_newSyncInfo.base,
                     (out_newSyncDataFile)?&out_newSyncData.base:0,
                     compressPlugin,strongChecksumPlugin,kMatchBlockSize,threadNum);
    check(hpatch_TFileStreamOutput_close(&out_newSyncData));
    check(hpatch_TFileStreamOutput_close(&out_newSyncInfo));
    check(hpatch_TFileStreamInput_close(&newData));
}

void create_sync_data(const char* newDataPath,
                      const char* out_newSyncInfoFile,
                      hpatch_TChecksum*      strongChecksumPlugin,
                      uint32_t kMatchBlockSize,size_t threadNum){
    create_sync_data(newDataPath,out_newSyncInfoFile,0,0,strongChecksumPlugin,kMatchBlockSize,threadNum);
}

void create_sync_data(const hpatch_TStreamInput*  newData,
                      const hpatch_TStreamOutput* out_newSyncInfo, //newSyncData same as newData
                      hpatch_TChecksum*      strongChecksumPlugin,
                      uint32_t kMatchBlockSize,size_t threadNum){
    create_sync_data(newData,out_newSyncInfo,0,0,strongChecksumPlugin,kMatchBlockSize,threadNum);
}
