// dir_diff_tools.cpp
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
#include "dir_diff_tools.h"
#if (_IS_NEED_DIR_DIFF_PATCH)

void assignDirTag(std::string& dir_utf8){
    if (dir_utf8.empty()||(dir_utf8[dir_utf8.size()-1]!=kPatch_dirSeparator))
        dir_utf8.push_back(kPatch_dirSeparator);
}

void sortDirPathList(std::vector<std::string>& fileList){
    std::sort(fileList.begin(),fileList.end());
}

    struct CDir{
        inline CDir(const std::string& dir):handle(0){ handle=hdiff_dirOpenForRead(dir.c_str()); }
        inline ~CDir(){ hdiff_dirClose(handle); }
        hdiff_TDirHandle handle;
    };
    static void _getDirSubFileList(const std::string& dirPath,std::vector<std::string>& out_list,
                                   IDirPathIgnore* filter,size_t rootPathNameLen,bool pathIsInOld){
        assert(!hdiff_private::isDirName(dirPath));
        std::vector<std::string> subDirs;
        {//serach cur dir
            CDir dir(dirPath);
            check((dir.handle!=0),"hdiff_dirOpenForRead \""+dirPath+"\" error!");
            while (true) {
                hpatch_TPathType  type;
                const char* path=0;
                check(hdiff_dirNext(dir.handle,&type,&path),"hdiff_dirNext \""+dirPath+"\" error!");
                if (path==0) break; //finish
                if ((0==strcmp(path,""))||(0==strcmp(path,"."))||(0==strcmp(path,"..")))
                    continue;
                std::string subName(dirPath+kPatch_dirSeparator+path);
                assert(!hdiff_private::isDirName(subName));
                switch (type) {
                    case kPathType_dir:{
                        assignDirTag(subName);
                        if (!filter->isNeedIgnore(subName,rootPathNameLen,pathIsInOld)){
                            subDirs.push_back(subName.substr(0,subName.size()-1)); //no '/'
                            out_list.push_back(subName); //add dir
                        }
                    } break;
                    case kPathType_file:{
                        if (!filter->isNeedIgnore(subName,rootPathNameLen,pathIsInOld))
                            out_list.push_back(subName); //add file
                    } break;
                    default:{
                        //nothing
                    } break;
                }
            }
        }
        
        for (size_t i=0; i<subDirs.size(); ++i) {
            assert(!hdiff_private::isDirName(subDirs[i]));
            _getDirSubFileList(subDirs[i],out_list,filter,rootPathNameLen,pathIsInOld);
        }
    }
void getDirAllPathList(const std::string& dirPath,std::vector<std::string>& out_list,
                       IDirPathIgnore* filter,bool pathIsInOld){
    assert(hdiff_private::isDirName(dirPath));
    out_list.push_back(dirPath);
    const std::string dirName(dirPath.c_str(),dirPath.c_str()+dirPath.size()-1); //without '/'
    _getDirSubFileList(dirName,out_list,filter,dirName.size(),pathIsInOld);
}

namespace hdiff_private{

    void CRefStream::open(const hpatch_TStreamInput** refList,size_t refCount){
        check(hpatch_TRefStream_open(this,refList,refCount),"TRefStream_open() refList error!");
    }
    
    hpatch_StreamPos_t getFileSize(const std::string& fileName){
        hpatch_TPathType   type;
        hpatch_StreamPos_t fileSize;
        checkv(hpatch_getPathStat(fileName.c_str(),&type,&fileSize));
        checkv(type==kPathType_file);
        return fileSize;
    }
    
    void packIncList(std::vector<TByte>& out_data,const std::vector<size_t>& list){
        size_t backValue=~(size_t)0;
        for (size_t i=0;i<list.size();++i){
            size_t curValue=list[i];
            assert(curValue>=(size_t)(backValue+1));
            packUInt(out_data,(size_t)(curValue-(size_t)(backValue+1)));
            backValue=curValue;
        }
    }
    
    void packList(std::vector<TByte>& out_data,const std::vector<hpatch_StreamPos_t>& list){
        for (size_t i=0;i<list.size();++i){
            packUInt(out_data,list[i]);
        }
    }
    
    
    static void formatDirTagForSave(std::string& path_utf8){
        if (kPatch_dirSeparator==kPatch_dirSeparator_saved) return;
        for (size_t i=0;i<path_utf8.size();++i){
            if (path_utf8[i]!=kPatch_dirSeparator)
                continue;
            else
                path_utf8[i]=kPatch_dirSeparator_saved;
        }
    }
    
    size_t pushNameList(std::vector<TByte>& out_data,const std::string& rootPath,
                        const std::vector<std::string>& nameList){
        const size_t rootLen=rootPath.size();
        std::string utf8;
        size_t outSize=0;
        for (size_t i=0;i<nameList.size();++i){
            const std::string& name=nameList[i];
            const size_t nameSize=name.size();
            assert(nameSize>=rootLen);
            assert(0==memcmp(name.data(),rootPath.data(),rootLen));
            const char* subName=name.c_str()+rootLen;
            const char* subNameEnd=subName+(nameSize-rootLen);
            utf8.assign(subName,subNameEnd);
            formatDirTagForSave(utf8);
            size_t writeLen=utf8.size()+1; // '\0'
            out_data.insert(out_data.end(),utf8.c_str(),utf8.c_str()+writeLen);
            outSize+=writeLen;
        }
        return outSize;
    }
    
    void pushTypes(std::vector<TByte>& out_data,const char* kTypeAndVersion,
                  const hdiff_TCompress* compressPlugin,hpatch_TChecksum* checksumPlugin){
        //type version
        pushCStr(out_data,kTypeAndVersion);
        pushCStr(out_data,"&");
        {//compressType
            const char* compressType="";
            if (compressPlugin)
                compressType=compressPlugin->compressType();
            size_t compressTypeLen=strlen(compressType);
            check(compressTypeLen<=hpatch_kMaxPluginTypeLength,"compressTypeLen error!");
            check(0==strchr(compressType,'&'), "compressType cannot contain '&'");
            pushCStr(out_data,compressType);
        }
        pushCStr(out_data,"&");
        {//checksumType
            const char* checksumType="";
            if (checksumPlugin)
                checksumType=checksumPlugin->checksumType();
            size_t checksumTypeLen=strlen(checksumType);
            check(checksumTypeLen<=hpatch_kMaxPluginTypeLength,"checksumTypeLen error!");
            check(0==strchr(checksumType,'&'), "checksumType cannot contain '&'");
            pushCStr(out_data,checksumType);
        }
        const TByte _cstrEndTag='\0';//c string end tag
        pushBack(out_data,&_cstrEndTag,(&_cstrEndTag)+1);
    }
    
    CFileResHandleLimit::CFileResHandleLimit(size_t _limitMaxOpenCount,size_t resCount)
    :limitMaxOpenCount(_limitMaxOpenCount),curInsert(0){
        hpatch_TResHandleLimit_init(&limit);
        resList.resize(resCount);
        memset(resList.data(),0,sizeof(hpatch_IResHandle)*resCount);
        fileList.resize(resCount);
        for(size_t i=0;i<resCount;++i)
            hpatch_TFileStreamInput_init(&fileList[i]);
    }
    
    void CFileResHandleLimit::addRes(const std::string& fileName,hpatch_StreamPos_t fileSize){
        assert(curInsert<resList.size());
        fileList[curInsert].fileName=fileName;
        hpatch_IResHandle* res=&resList[curInsert];
        res->open=CFileResHandleLimit::openRes;
        res->close=CFileResHandleLimit::closeRes;
        res->resImport=&fileList[curInsert];
        res->resStreamSize=fileSize;
        ++curInsert;
    }
    void CFileResHandleLimit::open(){
        assert(curInsert==resList.size());
        check(hpatch_TResHandleLimit_open(&limit,limitMaxOpenCount,resList.data(),
                                          resList.size()),"TResHandleLimit_open error!");
    }
    void CFileResHandleLimit::close(){
        check(hpatch_TResHandleLimit_close(&limit),"TResHandleLimit_close error!");
    }
    
    hpatch_BOOL CFileResHandleLimit::openRes(struct hpatch_IResHandle* res,hpatch_TStreamInput** out_stream){
        CFile* self=(CFile*)res->resImport;
        assert(self->m_file==0);
        check(hpatch_TFileStreamInput_open(self,self->fileName.c_str()),"CFileResHandleLimit open file error!");
        *out_stream=&self->base;
        return hpatch_TRUE;
    }
    hpatch_BOOL CFileResHandleLimit::closeRes(struct hpatch_IResHandle* res,const hpatch_TStreamInput* stream){
        CFile* self=(CFile*)res->resImport;
        assert(stream==&self->base);
        check(hpatch_TFileStreamInput_close(self),"CFileResHandleLimit close file error!");
        return hpatch_TRUE;
    }
    
}
#endif
