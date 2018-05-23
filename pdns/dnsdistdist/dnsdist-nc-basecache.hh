#ifndef NAMEDCACHE_H
#define NAMEDCACHE_H

#include <string>

#include "config.h"

#include "misc.hh"

#ifdef HAVE_NAMEDCACHE

enum CACHE_HIT {HIT_NONE, HIT_CDB, HIT_CACHE, HIT_CDB_NO_DATA, HIT_CACHE_NO_DATA};
enum CACHE_DEBUG {DEBUG_NONE = 0x00, DEBUG_DISP = 0X01, DEBUG_SLOW_LOAD = 0x02, DEBUG_MALLOC_TRIM = 0x04, DEBUG_DISP_LOAD_DETAIL = 0X08};

class BaseNamedCache {

private:
    int iDebug;
public:

  BaseNamedCache();
  virtual ~BaseNamedCache(){}
  virtual void setDebug(int iDebug=0);
  virtual bool init(int iEntries)=0;
  virtual bool open(std::string strFileName)=0;
  virtual bool close()=0;
  virtual int  getCache(std::string strKey, std::string &strValue)=0;
  virtual int  getSize(void)=0;
  virtual int  getEntries(void)=0;
  virtual int  getErrNum(void)=0;
  virtual std::string getErrMsg(void)=0;
  static  std::string getDebugText(int iDebug);
  static  std::string getFoundText(int iStat);
};

#endif

#endif // NAMEDCACHE_H
