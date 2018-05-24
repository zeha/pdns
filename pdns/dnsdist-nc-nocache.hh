#pragma once

#include "config.h"

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

#endif
