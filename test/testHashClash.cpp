// testHashClash.cpp
// tool for HDiff
// An estimation method for detecting hash clashs
/*
 The MIT License (MIT)
 Copyright (c) 2012-2019 HouSisong
 
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

#include <iostream>
#include <stdlib.h>
#include <vector>
#include <unordered_map>
#include "zlib.h"
#include "md5.h" // https://sourceforge.net/projects/libmd5-rfc
#include "../libHDiffPatch/HDiff/private_diff/limit_mem_diff/adler_roll.h"
#include "../_clock_for_demo.h"
typedef unsigned char   TByte;
typedef ptrdiff_t       TInt;
typedef size_t          TUInt;

/*
struct THash{
    typename TValue;
    static const char* name() const;
    void hash_begin();
    void hash(const TByte* pdata,const TByte* pdataEnd);
    void hash_finish(TValue* hv);
};*/

struct THash_md5_128{
    typedef std::pair<uint64_t,uint64_t> TValue;
    inline static const char* name() { return "md5_128"; }
    md5_state_t _hv;
    inline void hash_begin() { md5_init(&_hv); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
        { md5_append(&_hv,pdata,(int)(pdata_end-pdata)); }
    inline void hash_end(TValue* hv) { md5_finish(&_hv,(TByte*)hv); }
};

namespace std{
    template<> struct hash<THash_md5_128::TValue>{
        inline size_t operator()(const THash_md5_128::TValue& v) const{
            return v.first^v.second; }
    };
    template<> struct less<THash_md5_128::TValue>{
        inline bool operator()(const THash_md5_128::TValue& x,const THash_md5_128::TValue& y)const{
            return (x.first^x.second) < (y.first^y.second); }
    };
}

struct THash_crc32{
    typedef uint32_t TValue;
    inline static const char* name() { return "crc32"; }
    TValue _hv;
    inline void hash_begin() { _hv=(TValue)crc32(0,0,0); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
                        { _hv=(TValue)crc32(_hv,pdata,(uInt)(pdata_end-pdata)); }
    inline void hash_end(TValue* hv) { *hv=_hv; }
};

struct THash_adler32{
    typedef uint32_t TValue;
    inline static const char* name() { return "adler32"; }
    TValue _hv;
    inline void hash_begin() { _hv=(TValue)adler32(0,0,0); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
                        { _hv=(TValue)adler32(_hv,pdata,(uInt)(pdata_end-pdata)); }
    inline void hash_end(TValue* hv) { *hv=_hv; }
};


struct THash_adler32h{
    typedef uint32_t TValue;
    inline static const char* name() { return "adler32h"; }
    TValue _hv;
    inline void hash_begin() { _hv=adler32_start(0,0); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
                        { _hv=adler32_append(_hv,pdata,(pdata_end-pdata));
                            //assert(_hv==(TValue)adler32(1,pdata,(pdata_end-pdata)));
                        }
    inline void hash_end(TValue* hv) { *hv=_hv; }
};

struct THash_fadler32{
    typedef uint32_t TValue;
    inline static const char* name() { return "fadler32"; }
    TValue _hv;
    inline void hash_begin() { _hv=fast_adler32_start(0,0); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
                    { _hv=fast_adler32_append(_hv,pdata,(pdata_end-pdata)); }
    inline void hash_end(TValue* hv) { *hv=_hv; }
};

struct THash_adler64h{
    typedef uint64_t TValue;
    inline static const char* name() { return "adler64h"; }
    TValue _hv;
    inline void hash_begin() { _hv=adler64_start(0,0); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
                    { _hv=adler64_append(_hv,pdata,(pdata_end-pdata)); }
    inline void hash_end(TValue* hv) { *hv=_hv; }
};

struct THash_fadler64{
    typedef uint64_t TValue;
    inline static const char* name() { return "fadler64"; }
    TValue _hv;
    inline void hash_begin() { _hv=fast_adler64_start(0,0); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
                    { _hv=fast_adler64_append(_hv,pdata,(pdata_end-pdata)); }
    inline void hash_end(TValue* hv) { *hv=_hv; }
};

struct THash_fadler128{
    typedef adler128_t TValue;
    inline static const char* name() { return "fadler128"; }
    TValue _hv;
    inline void hash_begin() { _hv=fast_adler128_start(0,0); }
    inline void hash(const TByte* pdata,const TByte* pdata_end)
                    { _hv=fast_adler128_append(_hv,pdata,(pdata_end-pdata)); }
    inline void hash_end(TValue* hv) { *hv=_hv; }
};

const uint64_t kMaxMapNodeSize=80000000ull; //run test memory ctrl
const size_t   kRandTestMaxSize=1024*1024*1024;//test rand data size
const size_t   kMaxHashDataSize=256;
      size_t   kMinClash=0; //run test max time ctrl

template <class THash,class TUInt,size_t kTestBit=sizeof(TUInt)*8>
void test(const TByte* data,const TByte* data_end){
    typedef typename THash::TValue                TValue;
    typedef std::pair<const TByte*,const TByte*>  TPair;
    typedef std::unordered_map<TUInt,TPair>       TMap;
    double time0=clock_s();
    const size_t clip_count=sizeof(TValue)/sizeof(TUInt);
    assert(clip_count*sizeof(TUInt)==sizeof(TValue)); //unsupport other bit
    TMap maps[clip_count];
    for (size_t m=0;m<clip_count;++m)
        maps[m].reserve(kMaxMapNodeSize*3/clip_count);
    unsigned int rand_seed=7;
    printf("%s%s ",THash::name(),std::string(10-strlen(THash::name()),' ').c_str());
    
    uint64_t    curClashMin=0;
    uint64_t    clashs[clip_count]={0};
    double clashBases[clip_count]={0};
    size_t i=0;
    while (curClashMin<kMinClash) {
        uint64_t    clashMin=-(uint64_t)1;
        for (size_t m=0;m<clip_count;++m){
            if (clashs[m]<clashMin) clashMin=clashs[m];
        }
        curClashMin=clashMin;
        
        size_t dlen=rand_r(&rand_seed) % kMaxHashDataSize;
        size_t dstrat=rand_r(&rand_seed) % ((data_end-data) - dlen);
        assert(dstrat+dlen<=(data_end-data));
        const TByte* pv    =data+dstrat;
        const TByte* pv_end=pv+dlen;
        
        THash th;
        typename THash::TValue hvs;
        th.hash_begin();
        th.hash(pv,pv_end);
        th.hash_end(&hvs);
        for (size_t m=0;m<clip_count;++m){
            TMap&     map=maps[m];
            uint64_t& clash=clashs[m];
            double& clashBase=clashBases[m];
            TUInt hv=((TUInt*)&hvs)[m];
            hv<<=(sizeof(TUInt)*8-kTestBit);//for test
            auto it=map.find(hv);
            if (it==map.end()){
                if (map.size()*clip_count<kMaxMapNodeSize)
                    map[hv]=TPair(pv,pv_end);
                clashBase+=map.size();
                ++i;
            }else{
                const TPair& v=it->second;
                const TByte* vf=v.first;
                if ((pv_end-pv)!=(v.second-vf)){
                    ++clash;
                    clashBase+=map.size();
                    ++i;
                } else if (pv==vf){
                    //same i
                }else{
                    bool isEq=true;
                    for (size_t e=0; e<dlen; ++e) {
                        if (pv[e]==vf[e]){
                            continue;
                        }else{
                            isEq=false;
                            break;
                        }
                    }
                    if (isEq){
                        //same i
                    }else{
                        ++clash;
                        clashBase+=map.size();
                        ++i;
                    }
                }
            }
        }
    }
    for (size_t m=0;m<clip_count;++m)
        maps[m].clear();
    
    printf("test %dbitx%d, clash rate: ",(int)kTestBit,(int)clip_count);
    if (clip_count>0) printf("[");
    for (size_t m=0;m<clip_count; ++m) {
        double clashR=clashs[m]/clashBases[m];
        if (m>0) printf(", ");
        if (clashR>0){
            printf("%.3e(%.1fbit)",clashR,log2(1/clashR));
        }else{
            printf("0/%.3e",clashBases[m]);
        }
    }
    if (clip_count>0) printf("]");
    printf(" \ttime: %.1f s\n",(clock_s()-time0));
}

int main() {
    double bestCR_32bit =1.0/(((uint64_t)1)<<32);
    double bestCR_64bit =bestCR_32bit*bestCR_32bit;
    double bestCR_128bit=bestCR_64bit*bestCR_64bit;
    printf("32bit hash best\tclash rate: %.3e (1/%llu) \n",
           bestCR_32bit,(((uint64_t)1)<<32));
    printf("48bit hash best\tclash rate: %.3e (1/%llu) \n",
           1.0/(((uint64_t)1)<<48),(((uint64_t)1)<<48));
    printf("64bit hash best\tclash rate: %.3e (1/%llu%llu) \n",
           bestCR_64bit,(((uint64_t)(-(uint64_t)1)))/10,(((uint64_t)(-(uint64_t)1)))%10+1);
    printf("128bithash best\tclash rate: %.3e (1/%.3e) \n\n",
           bestCR_128bit,1/bestCR_128bit);
    
    std::vector<TByte> data(kRandTestMaxSize);
    unsigned int rand_seed=0;
    for (size_t i=0; i<data.size(); ++i) {
        data[i]=(TByte)rand_r(&rand_seed);
    }
    //*
    kMinClash=10000;
    test<THash_crc32,uint32_t>(data.data(),data.data()+data.size());
    test<THash_adler32,uint32_t>(data.data(),data.data()+data.size());
    test<THash_adler32h,uint32_t>(data.data(),data.data()+data.size());
    test<THash_fadler32,uint32_t>(data.data(),data.data()+data.size());
    test<THash_adler64h,uint32_t>(data.data(),data.data()+data.size());
    test<THash_fadler64,uint32_t>(data.data(),data.data()+data.size());
    test<THash_fadler128,uint32_t>(data.data(),data.data()+data.size());
    test<THash_md5_128,uint32_t>(data.data(),data.data()+data.size());
    printf("\n");
    test<THash_adler64h,uint64_t>(data.data(),data.data()+data.size());
    kMinClash=10;
    test<THash_fadler128,uint64_t>(data.data(),data.data()+data.size());
    printf("\n");
    //*/
    //*
    kMinClash=5;
    test<THash_adler64h,uint64_t,52>(data.data(),data.data()+data.size());
    test<THash_fadler64,uint64_t,52>(data.data(),data.data()+data.size());
    test<THash_fadler128,uint64_t,52>(data.data(),data.data()+data.size());
    test<THash_md5_128,uint64_t,52>(data.data(),data.data()+data.size());
    printf("\n");
    //*/
    //*
    printf("NOTE: not enough time to get next test results ...\n");
    kMinClash=1; // for timesaving but increase deviation
    test<THash_fadler64,uint64_t>(data.data(),data.data()+data.size());
    test<THash_md5_128,uint64_t>(data.data(),data.data()+data.size());
    //*/
    return 0;
}

