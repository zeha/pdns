#ifndef NOCACHE_H
#define NOCACHE_H

#include "config.h"

#include "dnsdist-nc-cdbio.hh"
#include "dnsdist-nc-basecache.hh"

#ifdef HAVE_NAMEDCACHE

class NoCache : public BaseNamedCache {

public:
   NoCache();
   ~NoCache();
   void  setDebug(int debug=0);
   bool  init(int capacity);
   bool  open(std::string strFileName);
   bool  close(void);
   int   getErrNum(void);
   std::string getErrMsg(void);
   int   getSize();
   int   getEntries();
   int   getCache(const std::string strKey, std::string &strValue);
   bool  setSize(int iEntries);
private:
   int iDebug;

};

class CdbNoCache : public BaseNamedCache {

public:
   CdbNoCache();
   ~CdbNoCache();
   void  setDebug(int debug=0);
   bool  init(int capacity);
   bool  open(std::string strFileName);
   bool  close(void);
   int   getErrNum(void);
   std::string getErrMsg(void);
   int   getSize();
   int   getEntries();
   int   getCache(const std::string strKey, std::string &strValue);
   bool  setSize(int iEntries);
private:
   int iDebug;
   cdbIO cdbFH;

};

#endif

#endif // NOCACHE_H
