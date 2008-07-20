/*------------------------------------------------------------------------------
* Copyright (C) 2003-2006 Ben van Klinken and the CLucene Team
* 
* Distributable under the terms of either the Apache License (Version 2.0) or 
* the GNU Lesser General Public License, as specified in the COPYING file.
------------------------------------------------------------------------------*/
#include "CLucene/_ApiHeader.h"
#include "_CompoundFile.h"
#include "CLucene/util/Misc.h"
#include "CLucene/store/IndexInput.h"
#include "CLucene/store/IndexOutput.h"

CL_NS_USE(store)
CL_NS_USE(util)
CL_NS_DEF(index)


class WriterFileEntry:LUCENE_BASE {
public:
	WriterFileEntry(){
		directoryOffset=0;
		dataOffset=0;
	}
	~WriterFileEntry(){
	}
	/** source file */
	char file[CL_MAX_PATH];

	/** temporary holder for the start of directory entry for this file */
	int64_t directoryOffset;

	/** temporary holder for the start of this file's data section */
	int64_t dataOffset;

};


/** Implementation of an IndexInput that reads from a portion of the
 *  compound file. The visibility is left as "package" *only* because
 *  this helps with testing since JUnit test cases in a different class
 *  can then access package fields of this class.
 */
class CSIndexInput:public CL_NS(store)::BufferedIndexInput {
private:
	CL_NS(store)::IndexInput* base;
	int64_t fileOffset;
	int64_t _length;
protected:
	/** Expert: implements buffer refill.  Reads uint8_ts from the current
	*  position in the input.
	* @param b the array to read uint8_ts into
	* @param length the number of uint8_ts to read
	*/
	void readInternal(uint8_t* b, const int32_t len);
	void seekInternal(const int64_t pos)
	{
	}

public:
	CSIndexInput(CL_NS(store)::IndexInput* base, const int64_t fileOffset, const int64_t length);
	CSIndexInput(const CSIndexInput& clone);
	~CSIndexInput();

	/** Closes the stream to futher operations. */
	void close();
	CL_NS(store)::IndexInput* clone() const;

	int64_t length() const { return _length; }
	
	const char* getDirectoryType() const{ return CompoundFileReader::DirectoryType(); }
};

class ReaderFileEntry:LUCENE_BASE {
public:
	int64_t offset;
	int64_t length;
	ReaderFileEntry(){
		offset=0;
		length=0;
	}
	~ReaderFileEntry(){
	}
};


CSIndexInput::CSIndexInput(CL_NS(store)::IndexInput* base, const int64_t fileOffset, const int64_t length){
   this->base = base;
   this->fileOffset = fileOffset;
   this->_length = length;
}
	
void CSIndexInput::readInternal(uint8_t* b, const int32_t len)
{
   SCOPED_LOCK_MUTEX(base->THIS_LOCK)

   int64_t start = getFilePointer();
   if(start + len > _length)
      _CLTHROWA(CL_ERR_IO,"read past EOF");
   base->seek(fileOffset + start);
   base->readBytes(b, len);
}
CSIndexInput::~CSIndexInput(){
}
IndexInput* CSIndexInput::clone() const
{
	return _CLNEW CSIndexInput(*this);
}
CSIndexInput::CSIndexInput(const CSIndexInput& clone): BufferedIndexInput(clone){
   this->base = clone.base; //no need to clone this..
   this->fileOffset = clone.fileOffset;
   this->_length = clone._length;
}

void CSIndexInput::close(){
}



CompoundFileReader::CompoundFileReader(Directory* dir, char* name):
	entries(_CLNEW EntriesType(true,true))
{
   directory = dir;
   fileName = STRDUP_AtoA(name);

   bool success = false;

   try {
      stream = dir->openInput(name);

      // read the directory and init files
      int32_t count = stream->readVInt();
      ReaderFileEntry* entry = NULL;
      TCHAR tid[CL_MAX_PATH];
      for (int32_t i=0; i<count; i++) {
            int64_t offset = stream->readLong();
            stream->readString(tid,CL_MAX_PATH);
            char* aid = STRDUP_TtoA(tid);

            if (entry != NULL) {
               // set length of the previous entry
               entry->length = offset - entry->offset;
            }

            entry = _CLNEW ReaderFileEntry();
            entry->offset = offset;
            entries->put(aid, entry);
      }

      // set the length of the final entry
      if (entry != NULL) {
            entry->length = stream->length() - entry->offset;
      }

      success = true;

   }_CLFINALLY(
      if (! success && (stream != NULL)) {
            try {
               stream->close();
               _CLDELETE(stream);
			   } catch (CLuceneError& err){
                if ( err.number() != CL_ERR_IO )
                    throw err;
				   //else ignore
            }
      }
   )
}

CompoundFileReader::~CompoundFileReader(){
	close();
	_CLDELETE_CaARRAY(fileName);
	_CLDELETE(entries);
}

Directory* CompoundFileReader::getDirectory(){
   return directory;
}

const char* CompoundFileReader::getName() const{
   return fileName;
}

void CompoundFileReader::close(){
  SCOPED_LOCK_MUTEX(THIS_LOCK)

  if (stream != NULL){
      entries->clear();
      stream->close();
      _CLDELETE(stream);
  }
}

IndexInput* CompoundFileReader::openInput(const char* id){
  SCOPED_LOCK_MUTEX(THIS_LOCK)

  if (stream == NULL)
      _CLTHROWA(CL_ERR_IO,"Stream closed");
	 
  const ReaderFileEntry* entry = entries->get(id);
  if (entry == NULL){
      char buf[CL_MAX_PATH+30];
      strcpy(buf,"No sub-file with id ");
      strncat(buf,id,CL_MAX_PATH);
      strcat(buf," found");
      _CLTHROWA(CL_ERR_IO,buf);
  }
  return _CLNEW CSIndexInput(stream, entry->offset, entry->length);
}

void CompoundFileReader::list(vector<string>* names) const{
  for ( EntriesType::const_iterator i=entries->begin();i!=entries->end();i++ ){
     names->push_back(i->first);
     ++i;
  }
}

bool CompoundFileReader::fileExists(const char* name) const{
   return entries->exists(name);
}

int64_t CompoundFileReader::fileModified(const char* name) const{
  return directory->fileModified(fileName);
}

void CompoundFileReader::touchFile(const char* name){
  directory->touchFile(fileName);
}

bool CompoundFileReader::doDeleteFile(const char* name){
   _CLTHROWA(CL_ERR_UnsupportedOperation,"UnsupportedOperationException: CompoundFileReader::doDeleteFile");
}

void CompoundFileReader::renameFile(const char* from, const char* to){
   _CLTHROWA(CL_ERR_UnsupportedOperation,"UnsupportedOperationException: CompoundFileReader::renameFile");
}

int64_t CompoundFileReader::fileLength(const char* name) const{
  ReaderFileEntry* e = entries->get(name);
  if (e == NULL){
     char buf[CL_MAX_PATH + 30];
     strcpy(buf,"File ");
     strncat(buf,name,CL_MAX_PATH );
     strcat(buf," does not exist");
     _CLTHROWA(CL_ERR_IO,buf);
  }
  return e->length;
}
IndexOutput* CompoundFileReader::createOutput(const char* name){
   _CLTHROWA(CL_ERR_UnsupportedOperation,"UnsupportedOperationException: CompoundFileReader::createOutput");
}
LuceneLock* CompoundFileReader::makeLock(const char* name){
   _CLTHROWA(CL_ERR_UnsupportedOperation,"UnsupportedOperationException: CompoundFileReader::makeLock");
}

TCHAR* CompoundFileReader::toString() const{
	TCHAR* ret = _CL_NEWARRAY(TCHAR,strlen(fileName)+20); //20=strlen("CompoundFileReader@")
	_tcscpy(ret,_T("CompoundFileReader@"));
	STRCPY_AtoT(ret+19,fileName,strlen(fileName));

	return ret;
}

CompoundFileWriter::CompoundFileWriter(Directory* dir, const char* name):
	ids(true),entries(_CLNEW EntriesType(true)){
  if (dir == NULL)
      _CLTHROWA(CL_ERR_NullPointer,"directory cannot be null");
  if (name == NULL)
      _CLTHROWA(CL_ERR_NullPointer,"name cannot be null");
  merged = false;
  directory = dir;
  fileName = STRDUP_AtoA(name);
}

CompoundFileWriter::~CompoundFileWriter(){
	_CLDELETE_CaARRAY(fileName);
	_CLDELETE(entries);
}

Directory* CompoundFileWriter::getDirectory(){
  return directory;
}

/** Returns the name of the compound file. */
const char* CompoundFileWriter::getName() const{
  return fileName;
}

void CompoundFileWriter::addFile(const char* file){
  if (merged)
      _CLTHROWA(CL_ERR_IO,"Can't add extensions after merge has been called");

  if (file == NULL)
      _CLTHROWA(CL_ERR_NullPointer,"file cannot be null");

  if (ids.find(file)!=ids.end()){
     char buf[CL_MAX_PATH + 30];
     strcpy(buf,"File ");
     strncat(buf,file,CL_MAX_PATH);
     strcat(buf," already added");
     _CLTHROWA(CL_ERR_IO,buf);
  }
  ids.insert(STRDUP_AtoA(file));

  WriterFileEntry* entry = _CLNEW WriterFileEntry();
  STRCPY_AtoA(entry->file,file,CL_MAX_PATH);
  entries->push_back(entry);
}

void CompoundFileWriter::close(){
  if (merged)
      _CLTHROWA(CL_ERR_IO,"Merge already performed");

  if (entries->size()==0) //isEmpty()
      _CLTHROWA(CL_ERR_IO,"No entries to merge have been defined");

  merged = true;

  // open the compound stream
  IndexOutput* os = NULL;
  try {
      os = directory->createOutput(fileName);

      // Write the number of entries
      os->writeVInt(entries->size());

      // Write the directory with all offsets at 0.
      // Remember the positions of directory entries so that we can
      // adjust the offsets later
      { //msvc6 for scope fix
		  TCHAR tfile[CL_MAX_PATH];
		  for ( CLLinkedList<WriterFileEntry*>::iterator i=entries->begin();i!=entries->end();i++ ){
			  WriterFileEntry* fe = *i;
			  fe->directoryOffset = os->getFilePointer();
			  os->writeLong(0);    // for now
			  STRCPY_AtoT(tfile,fe->file,CL_MAX_PATH);
			  os->writeString(tfile,_tcslen(tfile));
		  }
	  }

      // Open the files and copy their data into the stream.
      // Remember the locations of each file's data section.
      { //msvc6 for scope fix
		  int32_t bufferLength = 1024;
		  uint8_t buffer[1024];
		  for ( CL_NS(util)::CLLinkedList<WriterFileEntry*>::iterator i=entries->begin();i!=entries->end();i++ ){
			  WriterFileEntry* fe = *i;
			  fe->dataOffset = os->getFilePointer();
			  copyFile(fe, os, buffer, bufferLength);
		  }
	  }

	  { //msvc6 for scope fix
		  // Write the data offsets into the directory of the compound stream
		  for ( CLLinkedList<WriterFileEntry*>::iterator i=entries->begin();i!=entries->end();i++ ){
			  WriterFileEntry* fe = *i;
			  os->seek(fe->directoryOffset);
			  os->writeLong(fe->dataOffset);
		  }
	  }


  } _CLFINALLY (
	  if (os != NULL) try { os->close(); _CLDELETE(os); } catch (...) { }
  );
}


void CompoundFileWriter::copyFile(WriterFileEntry* source, IndexOutput* os, uint8_t* buffer, int32_t bufferLength){
  IndexInput* is = NULL;
  try {
      int64_t startPtr = os->getFilePointer();

      is = directory->openInput(source->file);
      int64_t length = is->length();
      int64_t remainder = length;
      int32_t chunk = bufferLength;

      while(remainder > 0) {
          int32_t len = (int32_t)cl_min((int64_t)chunk, remainder);
          is->readBytes(buffer, len);
          os->writeBytes(buffer, len);
          remainder -= len;
      }

      // Verify that remainder is 0
      if (remainder != 0){
         TCHAR buf[CL_MAX_PATH+100];
         _sntprintf(buf,CL_MAX_PATH+100,_T("Non-zero remainder length after copying")
          _T(": %d (id: %s, length: %d, buffer size: %d)"),
          remainder,source->file,length,chunk );
		 _CLTHROWT(CL_ERR_IO,buf);
      }

      // Verify that the output length diff is equal to original file
      int64_t endPtr = os->getFilePointer();
      int64_t diff = endPtr - startPtr;
      if (diff != length){
         TCHAR buf[100];
         _sntprintf(buf,100,_T("Difference in the output file offsets %d ")
            _T("does not match the original file length %d"),diff,length);
         _CLTHROWT(CL_ERR_IO,buf);
      }
  } _CLFINALLY (
     if (is != NULL){
        is->close();
        _CLDELETE(is);
     }
  );
}

CL_NS_END