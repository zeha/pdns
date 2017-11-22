#include "dnsdist-namedcache.hh"


#ifdef HAVE_NAMEDCACHE

DNSDistNamedCache::DNSDistNamedCache(const std::string& cacheName, const std::string& fileName, const std::string& reqType, const std::string& reqMode, size_t maxEntries, int debug)
{
  iDebug = debug;
  if(iDebug& CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - DNSDistNamedCache() - object %s Created   debug mode: %X - %s ", cacheName.c_str(), iDebug, BaseNamedCache::getDebugText(iDebug).c_str());
  }
  bnc = nullptr;
  sw = new StopWatch();
  init(cacheName, fileName, reqType, reqMode, maxEntries);
}

DNSDistNamedCache::~DNSDistNamedCache()
{
  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - ~DNSDistNamedCache() - object %s DESTROYED ", strCacheName.c_str());
  }
}

// reset object to type 'none'
bool DNSDistNamedCache::reset()
{
bool bStat = true;

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - DNSDistNamedCache()::reset() - start ");
  }

  iCacheType = CACHE_TYPE::TYPE_NONE;
  iCacheMode = CACHE_MODE::MODE_NONE;
  strFileName = "";
  uMaxEntries = 0;
  bOpened = false;
  tCreation = time(NULL);


  resetCounters();

  if(bnc != nullptr) {
    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
      warnlog("DEBUG - DNSDistNamedCache()::reset() - deleting nc object ");
      }
    delete bnc;
  }
  bnc = new NoCache();
  bnc->setDebug(iDebug);
  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - DNSDistNamedCache()::reset() - finished ");
  }
  return(bStat);
}

// initialize object - also destroys old one if it exists
bool DNSDistNamedCache::init(const std::string& cacheName, const std::string& fileName, const std::string& strReqType, const std::string& strReqMode, size_t maxEntries)
{
bool bStat = false;

  reset();             // reset existing object
  strCacheName = cacheName;
  strFileName  = fileName;
  uMaxEntries  = maxEntries;
  bOpened      = false;
  iCacheType   = parseCacheTypeText(strReqType);
  iCacheMode   = parseCacheModeText(strReqMode);
  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - DNSDistNamedCache()::init() - creating object...: %s   Entries: %lu   Type: %s   Mode: %s ", strCacheName.c_str(),
                            maxEntries, BaseNamedCache::getCacheTypeText(iCacheType).c_str(), BaseNamedCache::getCacheModeText(iCacheMode).c_str());
    warnlog("DEBUG - DNSDistNamedCache()::init() - requested cdb file: %s ", fileName.c_str());
  }
  switch(iCacheType) {
    case CACHE_TYPE::TYPE_LRU:              // bindToCDB()
        bnc = new LRUCache(0);
        bnc->setDebug(iDebug);
        break;
    case CACHE_TYPE::TYPE_MAP:              // loadFromCDB()
        bnc = new CdbMapCache();
        bnc->setDebug(iDebug);
        break;
    case CACHE_TYPE::TYPE_CDB:              // read from cdb directly
        bnc = new CdbNoCache();
        bnc->setDebug(iDebug);
        break;
    case CACHE_TYPE::TYPE_NONE:             // no cache
        bnc = new NoCache();
        bnc->setDebug(iDebug);
        break;
    default:
        bnc = NULL;
        break;
    }

  if(bnc->init(uMaxEntries, iCacheMode) == true) {
    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
      warnlog("DEBUG - DNSDistNamedCache()::init() - init nc object with max entries: %lu ", uMaxEntries);
    }
    if(bnc->open(fileName) == true) {
      bOpened = true;
      if(iCacheType == CACHE_TYPE::TYPE_MAP) {
        uMaxEntries = bnc->getEntries();
      }
    } else {
        uMaxEntries = 0;
      }
  }

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - DNSDistNamedCache()::init() - cdb file opened...: %s    %s ", bOpened?"YES":"NO", fileName.c_str());
    if(bOpened == false) {
      warnlog("DEBUG - DNSDistNamedCache(::init() - Error msg: %s ", bnc->getErrMsg().c_str());
    }
  }
  if(bOpened == true) {
    bStat = true;
  }
  return(bStat);
}

// 'close' object
bool DNSDistNamedCache::close(void)
{

  return(init(strCacheName,"", "", "", 0));       // keep cache name when closing
}


// lookup query in cache
int DNSDistNamedCache::lookup(const std::string& strQuery, std::string& strData)
{
  int iLoc = bnc->getCache(strQuery, strData);
  switch(iLoc) {
    case CACHE_HIT::HIT_CDB_NO_DATA:
      cdbHitsNoData++;
      break;
    case CACHE_HIT::HIT_CACHE_NO_DATA:
      cacheHitsNoData++;
      break;
    case CACHE_HIT::HIT_CACHE:
      cacheHits++;
      break;
    case CACHE_HIT::HIT_CDB:
      cdbHits++;
      break;
    case CACHE_HIT::HIT_NONE:
      cacheMiss++;
      break;
    default:
      break;
  }
  deltaLookup[deltaCount++] = sw->udiffAndSet();
  if(deltaCount == DELTA_AVG)
    deltaCount = 0;
  return(iLoc);
}

uint64_t DNSDistNamedCache::getMaxEntries()
{
  return(uMaxEntries);
}

std::string DNSDistNamedCache::getCacheName()
{
  return(strCacheName);
}

void DNSDistNamedCache::setCacheName(const std::string cacheName)
{
  strCacheName = cacheName;
}

std::string DNSDistNamedCache::getFileName()
{
  return(strFileName);
}

int DNSDistNamedCache::getErrNum()
{
  return(bnc->getErrNum());
}

std::string DNSDistNamedCache::getErrMsg()
{
  return(bnc->getErrMsg());
}

int DNSDistNamedCache::getDebugFlags()
{
    return(iDebug);
}

// get current number of entries in cache
uint64_t DNSDistNamedCache::getCacheEntries()
{
  return(bnc->getEntries());
}

// get current number of queryies per sec
int DNSDistNamedCache::getQuerySec()
{
double deltaAvg = 0.0;

  if(sw->udiff() > 1000000.0) {
    return 0;                                   // more than 1 sec with nothing
  }
  for(int ii=0; ii < DELTA_AVG; ii++) {
     deltaAvg += deltaLookup[ii];
  }
  deltaAvg /= (double) DELTA_AVG;
  if(deltaAvg != 0.0)
    return (int)(1000000.0 / deltaAvg);         // return average of last 10
  else
    return 0;                                   // nothing stored
}

uint64_t DNSDistNamedCache::getCacheHits()
{
  return(cacheHits);
}

// get current number of cache hits, with no data (ie. not rpz)
uint64_t DNSDistNamedCache::getCacheHitsNoData()
{
  return(cacheHitsNoData);
}

uint64_t DNSDistNamedCache::getCdbHits()
{
  return(cdbHits);
}

// get current number of cdb hits, with no data (ie. not rpz)
uint64_t DNSDistNamedCache::getCdbHitsNoData()
{
  return(cdbHitsNoData);
}

// get current number of cache misses
uint64_t DNSDistNamedCache::getCacheMiss()
{
  return(cacheMiss);
}

// reset the counters
void DNSDistNamedCache::resetCounters()
{
  cacheHits = 0;
  cacheHitsNoData = 0;
  cdbHits = 0;
  cdbHitsNoData = 0;
  cacheMiss = 0;
  tCounterReset = time(NULL);

  deltaCount = 0;
  for(int ii=0; ii < DELTA_AVG; ii++)
     deltaLookup[ii] = 0.0;
  if(sw != NULL)
    sw->start();

}

// return true if file open
bool DNSDistNamedCache::isFileOpen()
{
  return(bOpened);
}

std::string  DNSDistNamedCache::getCacheTypeText(bool bLoadBindMode)
{
  return(BaseNamedCache::getCacheTypeText(iCacheType, bLoadBindMode));
}

std::string  DNSDistNamedCache::getCacheModeText()
{
  return(BaseNamedCache::getCacheModeText(iCacheMode));
}

time_t DNSDistNamedCache::getCreationTime()
{
  return(tCreation);
}

time_t DNSDistNamedCache::getCounterResetTime()
{
  return(tCounterReset);
}

int DNSDistNamedCache::parseCacheTypeText(const std::string& strCacheType)
{
  return(BaseNamedCache::parseCacheTypeText(strCacheType));
}

int DNSDistNamedCache::parseCacheModeText(const std::string& strCacheMode)
{
  return(BaseNamedCache::parseCacheModeText(strCacheMode));
}

std::string DNSDistNamedCache::getNamedCacheStatusText()
{
std::string strText = "";

  strText += "Cache name......: " + getCacheName() + "\n";
  strText += "Cache type......: " + getCacheTypeText(true) + "\n";
  strText += "Cache mode......: " + getCacheModeText() + "\n";

  time_t tTemp = getCreationTime();  // Pretty-print creation time of named cache.
  struct tm* tm = localtime(&tTemp);
  char cTimeBuffer[30];
  strftime(cTimeBuffer, sizeof(cTimeBuffer), "%F %T %z", tm);
  cTimeBuffer[sizeof(cTimeBuffer)-1] = '\0';
  strText += "Creation time...: " + (std::string) cTimeBuffer + "\n";

  strText += "CDB file........: " + getFileName() + "\n";

  std::string fopened = "no";
  if (isFileOpen()) {
    fopened = "yes";
  }
  strText += "File opened.....: " + fopened + "\n";

  strText += "Error number....: " + std::to_string(getErrNum()) + "\n";
  strText += "Error message...: " + getErrMsg() + "\n";
  strText += "Entries in use..: " + std::to_string(getCacheEntries()) + "\n";

	  // Pretty-print the last time the named cache's counters were reset
  tTemp = getCounterResetTime();
  tm = localtime(&tTemp);
  strftime(cTimeBuffer, sizeof(cTimeBuffer), "%F %T %z", tm);
  cTimeBuffer[sizeof(cTimeBuffer)-1] = '\0';
  strText += "Counter reset...: " + (std::string) cTimeBuffer + "\n";

  strText += "Cache hits......: " + std::to_string(getCacheHits()) + "\n";
  strText += "Cache hits !data: " + std::to_string(getCacheHitsNoData()) + "\n";
  strText += "CDB hits........: " + std::to_string(getCdbHits()) + "\n";
  strText += "CDB hits !data..: " + std::to_string(getCdbHitsNoData()) + "\n";
  strText += "Cache misses....: " + std::to_string(getCacheMiss()) + "\n";
  strText += "Query per Sec...: " + std::to_string(getQuerySec()) + "\n";

   return strText;
}

void DNSDistNamedCache::getNamedCacheStatusTable(std::unordered_map<string, string> &tableResult)
{

  tableResult.insert({"cacheName", getCacheName()});
  tableResult.insert({"file", getFileName()});   // file used
  tableResult.insert({"open", isFileOpen() ? "yes" : "no"});             // file open
  tableResult.insert({"maxEntries", std::to_string(getMaxEntries())});
  tableResult.insert({"cacheEntries", std::to_string(getCacheEntries())});
  tableResult.insert({"cacheHits", std::to_string(getCacheHits())});
  tableResult.insert({"cacheHitsNoData", std::to_string(getCacheHitsNoData())});
  tableResult.insert({"cdbHits", std::to_string(getCdbHits())});
  tableResult.insert({"cdbHitsNoData", std::to_string(getCdbHitsNoData())});
  tableResult.insert({"cacheMiss", std::to_string(getCacheMiss())});
  tableResult.insert({"errMsg", getErrMsg()});
  tableResult.insert({"errNum", std::to_string(getErrNum())});
  tableResult.insert({"cacheMode", getCacheModeText()});
  tableResult.insert({"cacheType", getCacheTypeText(true)});      // load / bind
  tableResult.insert({"qps", std::to_string(getQuerySec())});

  time_t tTemp = getCreationTime();
  struct tm* tm = localtime(&tTemp);
  char cTimeBuffer[30];
  strftime(cTimeBuffer, sizeof(cTimeBuffer), "%F %T %z", tm);
  cTimeBuffer[sizeof(cTimeBuffer)-1] = '\0';
  tableResult.insert({"creationTime", cTimeBuffer});

  tTemp = getCounterResetTime();
  tm = localtime(&tTemp);
  strftime(cTimeBuffer, sizeof(cTimeBuffer), "%F %T %z", tm);
  cTimeBuffer[sizeof(cTimeBuffer)-1] = '\0';
  tableResult.insert({"counterResetTime", cTimeBuffer});

}


#endif


#ifdef HAVE_NAMEDCACHE
/*
   operations that are affected by multi-threading below.....
        namedCachesTable.find       // read operation
        namedCachesTable.end()      // read operation
        namedCachesTable.insert     // write operation

        namedCachesTable.erase(it); // write operation

        getNamedCachesStatus        // read operation
*/


std::mutex g_nc_table_mutex;        // mutex to prevent r/w named cache table problems

/*
   createNamedCacheIfNotExists() -
   needs to be locked before with
        auto namedCacheList = g_namedCaches.getCopy();
   after restored after with
        g_namedCaches.setState(namedCacheList);
*/


std::shared_ptr<DNSDistNamedCache> createNamedCacheIfNotExists(namedCaches_t& namedCacheTable, const string& findCacheName, const int iDebug)
{
  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - createNamedCacheEntry() - create: %s ", findCacheName.c_str());
  }

  std::lock_guard<std::mutex> guard(g_nc_table_mutex);         // stop for mutex

  std::shared_ptr<DNSDistNamedCache> selectedCache;
  namedCaches_t::iterator it = namedCacheTable.find(findCacheName);
  if (it != namedCacheTable.end()) {
    selectedCache = it->second;
  } else {
      if(!findCacheName.empty()) {
        vinfolog("Creating named cache %s", findCacheName);
      }
//      selectedCache = std::make_shared<DNSDistNamedCache>();              // WHY IS THIS HERE???
      if(iDebug & CACHE_DEBUG::DEBUG_TEST_ALWAYS_HIT) {
            selectedCache = std::make_shared<DNSDistNamedCache>(findCacheName, "", "NONE", "TEST", 0, iDebug);       // make empty 'test' cache, allways return 'cache hit'
        } else {
            selectedCache = std::make_shared<DNSDistNamedCache>(findCacheName, "", "NONE", "NONE", 0, iDebug);       // make empty named cache
          }
      namedCacheTable.insert(std::pair<std::string,std::shared_ptr<DNSDistNamedCache> >(findCacheName, selectedCache));       // insert into table
      if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
        warnlog("DEBUG - createNamedCacheIfNotExists() - inserted %s ", findCacheName.c_str());
      }
    }

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - createNamedCacheEntry() - create: %s ", findCacheName.c_str());
  }

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - createNamedCacheEntry() - end tablesize: %lu ", namedCacheTable.size());
    for (const auto& entry : namedCacheTable) {
      const string& strCacheName = entry.first;                           // get name
      const std::shared_ptr<DNSDistNamedCache> cacheEntry = entry.second;       // get object - was NamedCacheX
      string strCacheName2 = cacheEntry->getCacheName();
      warnlog("DEBUG - createNamedCacheEntry() - name: %s   name2: %s \n", strCacheName.c_str(), strCacheName2.c_str());
    }

  }


  return selectedCache;
}

/*
   deleteNamedCacheIfNotExists()
   needs to be locked before with
        auto namedCacheList = g_namedCaches.getCopy();
   after restored after with
        g_namedCaches.setState(namedCacheList);
*/


bool deleteNamedCacheEntry(namedCaches_t& namedCachesTable, const string& findCacheName, const int iDebug)
{
int iNoDeleteForDebug = 0;
  if(iNoDeleteForDebug) {
    return(true);
  }


  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - deleteNamedCacheEntry() - start size: %lu ",  namedCachesTable.size());
  }

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - deleteNamedCacheEntry() - delete: %s ", findCacheName.c_str());
  }

  std::lock_guard<std::mutex> guard(g_nc_table_mutex);         // stop for mutex

  std::shared_ptr<DNSDistNamedCache> selectedCache;
  namedCaches_t::iterator it = namedCachesTable.find(findCacheName);
  if (it != namedCachesTable.end()) {

    selectedCache = it->second;

    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
      warnlog("DEBUG - deleteNamedCacheEntry() - found: %s   foundx: %s ",findCacheName.c_str(), selectedCache->getCacheName().c_str());
    }

    selectedCache->init(findCacheName, "", "", "", 0);     // minimize resources
    namedCachesTable.erase(it);                           // erase it
    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
      warnlog("DEBUG - deleteNamedCacheEntry() - deleted: %s ",findCacheName.c_str());
    }
    return true;
  }
  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - deleteNamedCacheEntry() - COULD NOT DELETE: %s ", findCacheName.c_str());
  }

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - deleteNamedCacheEntry() - END: %s ", findCacheName.c_str());
  }

  return false;
}



// leave cache name alone while swapping other data
void swapSelectedElements(std::shared_ptr<DNSDistNamedCache> dataA, std::shared_ptr<DNSDistNamedCache> dataB)
{

  int iDebug               = dataA->iDebug;
  int iCacheType           = dataA->iCacheType;
  int iCacheMode           = dataA->iCacheMode;
  std::string strFileName  = dataA->strFileName;
  size_t uMaxEntries       = dataA->uMaxEntries;
  BaseNamedCache *bnc      = dataA->bnc;
  bool bOpened             = dataA->bOpened;
  time_t tCreation         = dataA->tCreation;

  dataA->iDebug             = dataB->iDebug;
  dataA->iCacheType         = dataB->iCacheType;
  dataA->iCacheMode         = dataB->iCacheMode;
  dataA->strFileName        = dataB->strFileName;
  dataA->uMaxEntries        = dataB->uMaxEntries;
  dataA->bnc                = dataB->bnc;
  dataA->bOpened            = dataB->bOpened;
  dataA->resetCounters();
  dataA->tCreation          = dataB->tCreation;

  dataB->iDebug             = iDebug;
  dataB->iCacheType         = iCacheType;
  dataB->iCacheMode         = iCacheMode;
  dataB->strFileName        = strFileName;
  dataB->uMaxEntries        = uMaxEntries;
  dataB->bnc                = bnc;
  dataB->bOpened            = bOpened;
  dataB->resetCounters();
  dataB->tCreation          = tCreation;

}


/*
   swapNamedCache()
   return:
        0 - ok
        -1 couldn't find 1st name
        -2 couldn't find 2nd name
*/
int swapNamedCacheEntries(namedCaches_t& namedCacheTable, const string& findCacheNameA, const string& findCacheNameB, int iDebug)
{
  std::lock_guard<std::mutex> guard(g_nc_table_mutex);         // stop for mutex


  std::shared_ptr<DNSDistNamedCache> selectedCacheA;
  std::shared_ptr<DNSDistNamedCache> selectedCacheB;
  namedCaches_t::iterator itA = namedCacheTable.find(findCacheNameA);
  if (itA != namedCacheTable.end()) {
    selectedCacheA = itA->second;
  } else {
    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
      warnlog("DEBUG - swapNamedCacheEntries() - No match for named cache A: %s ", findCacheNameA.c_str());
    }
    return -1;
  }

  namedCaches_t::iterator itB = namedCacheTable.find(findCacheNameB);
  if (itB != namedCacheTable.end()) {
    selectedCacheB = itB->second;
  } else {
    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
      warnlog("DEBUG - swapNamedCacheEntries() - No match for named cache B: %s ", findCacheNameB.c_str());
    }
    return -2;
  }

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - swapNamedCacheEntries() - before swap: cacheA: %s   cacheB: %s", itA->second->getCacheName(), itB->second->getCacheName());
  }

  swapSelectedElements(selectedCacheA, selectedCacheB);            // swap data of 'A' & 'B'

  if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
    warnlog("DEBUG - swapNamedCacheEntries() - after swap: cacheA: %s  cacheB: %s ", itA->second->getCacheName(), itB->second->getCacheName());
  }
  return 0;
}



std::string getAllNamedCacheStatus(namedCaches_t& namedCacheTable, int iDebug)
{


   ostringstream ret;
   boost::format fmt("%1$8.8s %|5t|%2$4s %|5t|%3$4s %|5t|%4$4s %|5t|%5$8s %|5t|%6$8s %|5t|%7$8s %|5t|%8$12s %|5t|%9$12s %|5t|%10$12s %|5t|%11$12s %|5t|%12$12s %|5t|%13%");
   boost::format fmtDebug( "%1$8.8s %2$8.8s %|5t|%3$4s %|5t|%4$4s %|5t|%5$4s %|5t|%6$8s %|5t|%7$8s %|5t|%8$8s %|5t|%9$12s %|5t|%10$12s %|5t|%11$12s %|5t|%12$12s %|5t|%13$12s %|5t|%14%");
  if(iDebug > 0) {
    ret << (fmtDebug % "Name" % "NameX" % "Type" % "Mode" % "Open" % "MaxCache" % "InCache" % "QuerySec" % "HitsCache" % "NoDataCache" % "CdbHits" % "CdbNoData"% " MissCache" % "FileName" ) << endl;
    ret << (fmtDebug % "--------" % "--------" %"----" % "----" % "----" % "--------" % "--------" % "--------" % "------------" % "------------" % "------------" % "------------" % "------------" % "--------" ) << endl;
  } else {
      ret << (fmt % "Name" % "Type" % "Mode" % "Open" % "MaxCache" % "InCache" % "QuerySec" % "HitsCache" % "NoDataCache" % "CdbHits" % "CdbNoData"% " MissCache" % "FileName" ) << endl;
      ret << (fmt % "--------" %"----" % "----" % "----" % "--------" % "--------" % "--------" % "------------" % "------------" % "------------" % "------------" % "------------" % "--------" ) << endl;
    }


   for (const auto& entry : namedCacheTable) {
      const string& strCacheName = entry.first;                           // get name
      const std::shared_ptr<DNSDistNamedCache> cacheEntry = entry.second;       // get object - was NamedCacheX
      string strCacheName2 = cacheEntry->getCacheName();
      string strFileName = cacheEntry->getFileName();
      string strType = cacheEntry->getCacheTypeText(true);    // load / bind name
      string strMode = cacheEntry->getCacheModeText();
      string strFileOpen = cacheEntry->isFileOpen()?"Yes":"No";
      string strMaxEntries = std::to_string(cacheEntry->getMaxEntries());
      string strInCache = std::to_string(cacheEntry->getCacheEntries());

      string strQueryPerSec = std::to_string(cacheEntry->getQuerySec());

      string strHitsCache = std::to_string(cacheEntry->getCacheHits());
      string strNoDataCache = std::to_string(cacheEntry->getCacheHitsNoData());
      string strCdbHits = std::to_string(cacheEntry->getCdbHits());
      string strCdbHitsNoData = std::to_string(cacheEntry->getCdbHitsNoData());
      string strMissCache = std::to_string(cacheEntry->getCacheMiss());

      if(iDebug > 0) {
        ret << (fmtDebug % strCacheName % strCacheName2 % strType % strMode % strFileOpen % strMaxEntries % strInCache % strQueryPerSec % strHitsCache % strNoDataCache % strCdbHits % strCdbHitsNoData % strMissCache % strFileName) << endl;
      } else {
          ret << (fmt % strCacheName % strType % strMode % strFileOpen % strMaxEntries % strInCache % strQueryPerSec % strHitsCache % strNoDataCache % strCdbHits % strCdbHitsNoData % strMissCache % strFileName) << endl;
        }
   }
   return ret.str();
};



// Background thread to load cache.......
void namedCacheLoadThread(std::shared_ptr<DNSDistNamedCache> entryCacheA, const std::string strFileName, const std::string strCacheType, const std::string strCacheMode, int iMaxEntries, int iDebug)
{

  auto start = std::chrono::system_clock::now();
  std::time_t startTime = std::chrono::system_clock::to_time_t(start);
  std::string strStartTime;
  strStartTime.append(std::ctime(&startTime), 19);



  int iCacheType = BaseNamedCache::parseCacheTypeText(strCacheType);
  std::string strMaxEntries = std::to_string(iMaxEntries);
  if (iCacheType == CACHE_TYPE::TYPE_MAP) {
    strMaxEntries = "all";
  }
  std::string strType = BaseNamedCache::getCacheTypeText(iCacheType, true);       // make it pretty

  std::string strCacheName = entryCacheA->getCacheName();

  iDebug = entryCacheA->getDebugFlags();      // set debug level  - FIX THIS

  warnlog("Loading Cache: %s   Type: %s   Mode: %s   Max Entries: %s   Time: %s   File: %s ",
                                        strCacheName.c_str(), strType.c_str(), strCacheMode.c_str(), strMaxEntries.c_str(), strStartTime.c_str(), strFileName.c_str());
  if(iDebug & CACHE_DEBUG::DEBUG_SLOW_LOAD) {
    warnlog("WARNING! - SLOW DISK LOADING ENABLED FOR DEBUGGING for named cache: %s ", strCacheName.c_str());
  }

  std::string strCacheNameB = g_namedCacheTempPrefix;
  std::stringstream stream;
  stream << std::hex << g_namedCacheTempFileCount++;
  strCacheNameB += stream.str();                                // unique temporary named cache name for loading in bkg
  std::shared_ptr<DNSDistNamedCache> entryCacheB = createNamedCacheIfNotExists(g_namedCacheTable, strCacheNameB, iDebug);

  bool bStat = entryCacheB->init(strCacheNameB, strFileName, strCacheType, strCacheMode , iMaxEntries);
  if(bStat == true) {
    std::lock_guard<std::mutex> lock(g_luamutex);               // lua lock till end of 'if'

    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
     warnlog("DEBUG - namedCacheLoadThread() -Before swap cacheA: %s - max: %lu   cacheB: %s - max: %lu ",
                        entryCacheA->getCacheName().c_str(), entryCacheA->getMaxEntries(),
                        entryCacheB->getCacheName().c_str(), entryCacheB->getMaxEntries());
    }
    int iSwapStat =  swapNamedCacheEntries(g_namedCacheTable, strCacheName, strCacheNameB, iDebug);
    if(iSwapStat != 0) {
      errlog("namedCacheLoadThread - swap failed! iSwapStat: %d   nameA: %s   nameB: %s \n", iSwapStat, strCacheName, strCacheNameB);
    }


    if(iDebug & CACHE_DEBUG::DEBUG_DISP) {
      errlog("DEBUG - namedCacheLoadThread() - After swap cacheA: %s - max: %lu   cacheB: %s - max: %lu ",
                    entryCacheA->getCacheName().c_str(), entryCacheA->getMaxEntries(), entryCacheB->getCacheName().c_str(),entryCacheB->getMaxEntries());
    }
    deleteNamedCacheEntry(g_namedCacheTable,strCacheNameB, iDebug);  // delete temp
  } else {
      std::stringstream err;
      err << "Failed to reload named cache " << strFileName << "  -> " << entryCacheB->getErrMsg() << "(" << entryCacheB->getErrNum() << ")" << endl;
      string outstr = err.str();
	  errlog(outstr.c_str());
    }



  auto end = std::chrono::system_clock::now();
  std::time_t endTime = std::chrono::system_clock::to_time_t(end);
  std::string strEndTime;
  strEndTime.append(std::ctime(&endTime), 19);

  auto millSec = std::chrono::duration_cast<std::chrono::milliseconds> (end-start);
  warnlog("Finished loading Cache: %s   Max Entries: %d   Elapsed ms: %d ",  strCacheName.c_str(), entryCacheA->getMaxEntries(), millSec.count());


}


// Background thread to reload cache.......
void namedCacheReloadThread(const std::string& strCacheNameA,  boost::optional<int> maxEntries)
{
int iDebug = 0;

  auto start = std::chrono::system_clock::now();
  std::time_t startTime = std::chrono::system_clock::to_time_t(start);
  std::string strStartTime;
  strStartTime.append(std::ctime(&startTime), 19);

  std::shared_ptr<DNSDistNamedCache> entryCacheA = createNamedCacheIfNotExists(g_namedCacheTable, strCacheNameA, iDebug);


  iDebug = entryCacheA->getDebugFlags();
  std::string strFileNameA = entryCacheA->getFileName();
  std::string strCacheTypeA = entryCacheA->getCacheTypeText();
  std::string strCacheModeA = entryCacheA->getCacheModeText();
  int iMaxEntriesA = entryCacheA->getMaxEntries();
  if(maxEntries) {
    iMaxEntriesA = *maxEntries;
  }

  warnlog("Reloading Named Cache: %s   Max Entries: %s   Time: %s ", strCacheNameA.c_str(), maxEntries?std::to_string((int) *maxEntries):"all", strStartTime.c_str());
  if(iDebug & CACHE_DEBUG::DEBUG_SLOW_LOAD) {
    warnlog("WARNING! - SLOW DISK LOADING ENABLED FOR DEBUGGING for named cache: %s ", strCacheNameA.c_str());
  }

  std::string strCacheNameB = g_namedCacheTempPrefix;
  std::stringstream stream;
  stream << std::hex << g_namedCacheTempFileCount++;
  strCacheNameB += stream.str();                            // unique temp name

  std::shared_ptr<DNSDistNamedCache> entryCacheB = createNamedCacheIfNotExists(g_namedCacheTable, strCacheNameB, iDebug);        // create temp
                                                            // load data into temp
  bool bStat = entryCacheB->init(strCacheNameB, strFileNameA, strCacheTypeA, strCacheModeA , iMaxEntriesA);
  if(bStat == true) {
    std::lock_guard<std::mutex> lock(g_luamutex);           // lua lock till end of 'if'
    int iSwapStat =  swapNamedCacheEntries(g_namedCacheTable, strCacheNameA, strCacheNameB, iDebug);   // swap
    if(iSwapStat != 0) {
      warnlog("namedCacheReloadThread - swap failed! iSwapStat: %d   nameA: %s   nameB: %s \n", iSwapStat, strCacheNameA, strCacheNameB);
    }
    deleteNamedCacheEntry(g_namedCacheTable,strCacheNameB, iDebug); // delete temp
  } else {
      std::stringstream err;
      err << "Failed to reload named cache " << strFileNameA << "  -> " << entryCacheB->getErrMsg() << "(" << entryCacheB->getErrNum() << ")" << endl;
      string outstr = err.str();
	  errlog(outstr.c_str());
    }



  auto end = std::chrono::system_clock::now();
  std::time_t endTime = std::chrono::system_clock::to_time_t(end);
  std::string strEndTime;
  strEndTime.append(std::ctime(&endTime), 19);

  auto millSec = std::chrono::duration_cast<std::chrono::milliseconds> (end-start);
  warnlog("Finished reloading Named Cache: %s   Max Entries: %d   Elapsed ms: %d ", strCacheNameA.c_str(), iMaxEntriesA, millSec.count());


}

#endif
