
#include "dnsdist-nc-basecache.hh"

#ifdef HAVE_NAMEDCACHE

// GCA - virtual functions for named caches

BaseNamedCache::BaseNamedCache()
{
  iDebug = 0;
}

void BaseNamedCache::setDebug(int debug)
{
  iDebug = debug;
}


std::string BaseNamedCache::getDebugText(int debug)
{
string strMsg;

  if(debug != 0) {
    strMsg = "DEBUG ";
    for(int ii=0; ii < 32; ii++) {
       switch(debug & (0x01 << ii)) {
       case CACHE_DEBUG::DEBUG_DISP:
         strMsg += "DISPLAY ";
         break;
       case CACHE_DEBUG::DEBUG_SLOW_LOAD:
         strMsg += "SLOW_LOAD ";
         break;
       case CACHE_DEBUG::DEBUG_MALLOC_TRIM:
         strMsg += "MALLOC_TRIM ";
         break;
       case CACHE_DEBUG::DEBUG_DISP_LOAD_DETAIL:
         strMsg += "DISPLAY_LOAD_DETAIL ";
         break;
       default:
         break;
       }
    }
  }
  return(strMsg);
}

std::string BaseNamedCache::getFoundText(int iStat)
{
std::string strFound;

  switch(iStat) {
  case CACHE_HIT::HIT_CACHE_NO_DATA:
    strFound = "Cache_NoData";
    break;
  case CACHE_HIT::HIT_CDB_NO_DATA:
    strFound = "Cdb_NoData";
    break;
  case CACHE_HIT::HIT_CACHE:
    strFound = "Cache";
    break;
 case CACHE_HIT::HIT_CDB:
   strFound = "Cdb";
   break;
 case CACHE_HIT::HIT_NONE:
   strFound = "None";
   break;
 default:
   strFound = "?????";
   break;
 }
 return(strFound);
}

#endif
