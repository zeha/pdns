
#include "dnsdist-nc-namedcache.hh"

#ifdef HAVE_NAMEDCACHE

// GCA - virtual functions for named caches

NamedCache::NamedCache()
{
  iDebug = 0;
}

void NamedCache::setDebug(int debug)
{
  iDebug = debug;
}


std::string NamedCache::getDebugText(int debug)
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
       case CACHE_DEBUG::DEBUG_TEST_ALWAYS_HIT:
         strMsg += "TEST_ALWAYS_HIT ";
         break;
       default:
         break;
       }
    }
  }
  return(strMsg);
}

std::string NamedCache::getFoundText(int iStat)
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

std::string NamedCache::getCacheModeText(int iMode)
{
std::string strFound;

  switch(iMode) {
  case CACHE_MODE::MODE_NONE:
    strFound = "None";
    break;
  case CACHE_MODE::MODE_CDB:
    strFound = "CDB";
    break;
  case CACHE_MODE::MODE_ALL:
    strFound = "ALL";
    break;
  case CACHE_MODE::MODE_TEST:
    strFound = "TEST";
    break;
  default:
    strFound = "????";
    break;
  }
 return(strFound);
}

int NamedCache::parseCacheModeText(const std::string& strCacheMode)
{
std::string strTemp;

  strTemp = toLower(strCacheMode);
  if(strTemp == "none") {
    return(CACHE_MODE::MODE_NONE);
  }
  if(strTemp == "cdb") {
    return(CACHE_MODE::MODE_CDB);
  }
  if(strTemp == "all") {
    return(CACHE_MODE::MODE_ALL);
  }
  if(strTemp == "test") {
    return(CACHE_MODE::MODE_TEST);
  }

  return( CACHE_MODE::MODE_NONE);
}

std::string NamedCache::getCacheTypeText(int iType, bool bLoadBindMode)
{
std::string strFound;

  switch(iType) {
    case CACHE_TYPE::TYPE_NONE:
      strFound = "None";
      break;
    case CACHE_TYPE::TYPE_CDB:
      strFound = "CDB";
      break;
    case CACHE_TYPE::TYPE_LRU:
      strFound = "LRU";
      if(bLoadBindMode == true) {           // for user display with bindToCDB()
        strFound = "BIND";
        }
      break;
    case CACHE_TYPE::TYPE_MAP:
      strFound = "MAP";
      if(bLoadBindMode == true) {           // for user display with loadFromCDB()
        strFound = "LOAD";
        }
      break;
    default:
      strFound = "????";
      break;
    }
    return(strFound);
}

int NamedCache::parseCacheTypeText(const std::string& strCacheType)
{
std::string strTemp;

  strTemp = toLower(strCacheType);
  if(strTemp == "none") {
    return(CACHE_TYPE::TYPE_NONE);
  }
  if(strTemp == "cdb") {
    return(CACHE_TYPE::TYPE_CDB);
  }
  if(strTemp == "lru") {
    return(CACHE_TYPE::TYPE_LRU);
  }
  if(strTemp == "map") {
    return(CACHE_TYPE::TYPE_MAP);
  }

  return(CACHE_TYPE::TYPE_NONE);
}

#endif
