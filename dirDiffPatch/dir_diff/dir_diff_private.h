// dir_diff_private.h
// hdiffz dir diff
//
/*
 The MIT License (MIT)
 Copyright (c) 2018-2019 HouSisong
 
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
#ifndef hdiff_dir_diff_private_h
#define hdiff_dir_diff_private_h
#include "dir_diff.h"
#if (_IS_NEED_DIR_DIFF_PATCH)
namespace hdiff_private{

template <class TVector>
static inline void swapClear(TVector& v){ TVector _t; v.swap(_t); }

static inline bool isDirName(const std::string& path_utf8){
    return 0!=hpatch_getIsDirName(path_utf8.c_str());
}

hpatch_StreamPos_t getFileSize(const std::string& fileName);

struct CFileResHandleLimit{
    CFileResHandleLimit(size_t _limitMaxOpenCount,size_t resCount);
    inline ~CFileResHandleLimit() { close(); }
    void addRes(const std::string& fileName,hpatch_StreamPos_t fileSize);
    void open();
    void close();
    
    struct CFile:public hpatch_TFileStreamInput{
        std::string  fileName;
    };
    hpatch_TResHandleLimit          limit;
    std::vector<CFile>              fileList;
    std::vector<hpatch_IResHandle>  resList;
    size_t                          limitMaxOpenCount;
    size_t                          curInsert;
    static hpatch_BOOL openRes(struct hpatch_IResHandle* res,hpatch_TStreamInput** out_stream);
    static hpatch_BOOL closeRes(struct hpatch_IResHandle* res,const hpatch_TStreamInput* stream);
};

struct CRefStream:public hpatch_TRefStream{
    inline CRefStream(){ hpatch_TRefStream_init(this); }
    void open(const hpatch_TStreamInput** refList,size_t refCount);
    inline ~CRefStream(){ hpatch_TRefStream_close(this); }
};

size_t pushNameList(std::vector<TByte>& out_data,const std::string& rootPath,
                    const std::vector<std::string>& nameList);
void pushList(std::vector<TByte>& out_data,const std::vector<hpatch_StreamPos_t>& list);
void pushIncList(std::vector<TByte>& out_data,const std::vector<size_t>& list);

}
#endif
#endif //hdiff_dir_diff_private_h
