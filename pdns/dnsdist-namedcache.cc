#include "dnsdist-namedcache.hh"


#ifdef HAVE_NAMEDCACHE

DNSDistNamedCache::DNSDistNamedCache(const std::string& cacheName, const std::string& fileName, const std::string& reqType, const std::string& reqMode, size_t maxEntries, int debug)
{

  if(debug > 0) {
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache() - object %s Created XXXXXXXXXXXXXXXXXXXXXXXXXXXX \n", cacheName.c_str());
  }
  nc = nullptr;
  init(cacheName, fileName, reqType, reqMode, maxEntries, debug);
}

DNSDistNamedCache::~DNSDistNamedCache()
{
  if(iDebug > 0) {
    printf("DEBUG DEBUG DEBUG - ~DNSDistNamedCache() - object %s DESTROYED XXXXXXXXXXXXXXXXXXXXXXXXXXXX \n", strCacheName.c_str());
  }
}

// ----------------------------------------------------------------------------
// reset() - reset object to type 'none'
// ----------------------------------------------------------------------------
bool DNSDistNamedCache::reset()
{
bool bStat = true;

  if(iDebug > 0) {
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::reset() - start \n");
  }

  iCacheType = CACHE_TYPE::TYPE_NONE;
  iCacheMode = CACHE_MODE::MODE_NONE;
  strFileName = "";
  uMaxEntries = 0;
  bOpened = false;
  tCreation = time(NULL);

  resetCounters();

  if(nc != nullptr) {
    if(iDebug > 0) {
      printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::reset() - deleting nc object \n");
      }
    delete nc;
  }
  nc = new NoCache();
  if(iDebug > 0) {
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::reset() - finished \n");
  }
  return(bStat);
}

// ----------------------------------------------------------------------------
// init() - initialize object - also destroys old one if it exists
// ----------------------------------------------------------------------------
bool DNSDistNamedCache::init(const std::string& cacheName, const std::string& fileName, const std::string& strReqType, const std::string& strReqMode, size_t maxEntries, int debug)
{
bool bStat = false;

  iDebug = debug;
  reset();             // reset existing object
  strCacheName = cacheName;
  strFileName  = fileName;
  uMaxEntries  = maxEntries;
  bOpened      = false;
  iCacheType   = parseCacheTypeText(strReqType);
  iCacheMode   = parseCacheModeText(strReqMode);
  if(debug > 0) {
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() -------------------- \n");
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() - creating object...: %s \n", strCacheName.c_str());
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() - requested cdb file: %s \n", fileName.c_str());
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() - requested entries.: %lu \n", maxEntries);
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() - requested Type....: %s \n", NamedCache::getCacheTypeText(iCacheType).c_str());
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() - requested Mode....: %s \n", NamedCache::getCacheModeText(iCacheMode).c_str());
  }
  switch(iCacheType) {
    case CACHE_TYPE::TYPE_LRU:
        nc = new LRUCache();
        break;
    case CACHE_TYPE::TYPE_MAP:
        nc = new CdbMapCache();
        break;
    case CACHE_TYPE::TYPE_CDB:
        nc = new CdbNoCache();
        break;
    case CACHE_TYPE::TYPE_NONE:
        nc = new NoCache();
        break;
    default:
        nc = NULL;
        break;
    }

  if(nc->init(uMaxEntries, iCacheMode) == true) {
    if(iDebug > 0) {
      printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() - init nc object with max entries: %lu \n", uMaxEntries);
    }
    if(nc->open(fileName) == true) {
      bOpened = true;
      if(iCacheType == CACHE_TYPE::TYPE_MAP) {
        uMaxEntries = nc->getEntries();
      }
    } else {
        uMaxEntries = 0;
      }
  }

  if(iDebug > 0) {
    printf("DEBUG DEBUG DEBUG - DNSDistNamedCache()::init() - cdb file opened...: %s    %s \n", bOpened?"YES":"NO", fileName.c_str());
    if(bOpened == false) {
      printf("DEBUG DEBUG DEBUG - DNSDistNamedCache(::init() - Error msg: %s \n", nc->getErrMsg().c_str());
    }
  }
  if(bOpened == true) {
    bStat = true;
  }
  return(bStat);
}

// ----------------------------------------------------------------------------
// close() - 'close' object
// ----------------------------------------------------------------------------
bool DNSDistNamedCache::close(void)
{

  return(init(strCacheName,"", "", "", 0));       // keep cache name when closing, 10/26/2017
}


// ----------------------------------------------------------------------------
// lookup() - lookup query in cache
// ----------------------------------------------------------------------------
int DNSDistNamedCache::lookup(const std::string& strQuery, std::string& strData)
{
  int iLoc = nc->getCache(strQuery, strData);
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
  return(iLoc);
}

// ----------------------------------------------------------------------------
// getMaxEntries() - get maximum entries in cache
// ----------------------------------------------------------------------------
uint64_t DNSDistNamedCache::getMaxEntries()
{
  return(uMaxEntries);
}

// ----------------------------------------------------------------------------
// getCacheName() - get cache name
// ----------------------------------------------------------------------------
std::string DNSDistNamedCache::getCacheName()
{
  return(strCacheName);
}

// ----------------------------------------------------------------------------
// setCacheName() - set cache name
// ----------------------------------------------------------------------------
void DNSDistNamedCache::setCacheName(const std::string cacheName)
{
  strCacheName = cacheName;
}

// ----------------------------------------------------------------------------
// getFileName() - get cdb filename used to load cache
// ----------------------------------------------------------------------------
std::string DNSDistNamedCache::getFileName()
{
  return(strFileName);
}

// ----------------------------------------------------------------------------
// getErrMsg() - get i/o error number (errno)
// ----------------------------------------------------------------------------
int DNSDistNamedCache::getErrNum()
{
  return(nc->getErrNum());
}
// ----------------------------------------------------------------------------
// getErrMsg() - get error message text
// ----------------------------------------------------------------------------
std::string DNSDistNamedCache::getErrMsg()
{
  return(nc->getErrMsg());
}

// ----------------------------------------------------------------------------
// getCacheEntries() - get current number of entries in cache
// ----------------------------------------------------------------------------
uint64_t DNSDistNamedCache::getCacheEntries()
{
  return(nc->getEntries());
}

// ----------------------------------------------------------------------------
// getCacheHits() - get current number of cache hits
// ----------------------------------------------------------------------------
uint64_t DNSDistNamedCache::getCacheHits()
{
  return(cacheHits);
}

// ----------------------------------------------------------------------------
// getCacheHitsNoData() - get current number of cache hits, with no data (ie. not rpz)
// ----------------------------------------------------------------------------
uint64_t DNSDistNamedCache::getCacheHitsNoData()
{
  return(cacheHitsNoData);
}

// ----------------------------------------------------------------------------
// getCdbHits() - get current number of cdb hits
// ----------------------------------------------------------------------------
uint64_t DNSDistNamedCache::getCdbHits()
{
  return(cdbHits);
}

// ----------------------------------------------------------------------------
// getCdbHitsNoData() - get current number of cdb hits, with no data (ie. not rpz)
// ----------------------------------------------------------------------------
uint64_t DNSDistNamedCache::getCdbHitsNoData()
{
  return(cdbHitsNoData);
}

// ----------------------------------------------------------------------------
// getCacheMiss() - get current number of cache misses
// ----------------------------------------------------------------------------
uint64_t DNSDistNamedCache::getCacheMiss()
{
  return(cacheMiss);
}

// ----------------------------------------------------------------------------
// resetCounts() - reset the counters
// ----------------------------------------------------------------------------
void DNSDistNamedCache::resetCounters()
{
  cacheHits = 0;
  cacheHitsNoData = 0;
  cdbHits = 0;
  cdbHitsNoData = 0;
  cacheMiss = 0;
  tCounterReset = time(NULL);
}

// ----------------------------------------------------------------------------
// isFileOpen() - return true if file open
// ----------------------------------------------------------------------------
bool DNSDistNamedCache::isFileOpen()
{
  return(bOpened);
}

std::string  DNSDistNamedCache::getCacheTypeText(bool bLoadBindMode)
{
  return(NamedCache::getCacheTypeText(iCacheType, bLoadBindMode));
}

std::string  DNSDistNamedCache::getCacheModeText()
{
  return(NamedCache::getCacheModeText(iCacheMode));
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
  return(NamedCache::parseCacheTypeText(strCacheType));
}

int DNSDistNamedCache::parseCacheModeText(const std::string& strCacheMode)
{
  return(NamedCache::parseCacheModeText(strCacheMode));
}

#endif


#ifdef HAVE_NAMEDCACHE

// ----------------------------------------------------------------------------
// operations that are affected by multi-threading below.....
//      namedCachesTable.find       // read operation
//      namedCachesTable.end()      // read operation
//      namedCachesTable.insert     // write operation
//
//      namedCachesTable.erase(it); // write operation
//
//      getNamedCachesStatus        // read operation

// ----------------------------------------------------------------------------

std::mutex g_nc_table_mutex;        // mutex to prevent r/w named cache table problems

// ----------------------------------------------------------------------------
// createNamedCacheIfNotExists() -
// needs to be locked before with
//      auto namedCacheList = g_namedCaches.getCopy();
// after restored after with
//      g_namedCaches.setState(namedCacheList);
// ----------------------------------------------------------------------------
std::shared_ptr<NamedCacheX> createNamedCacheIfNotExists(namedCaches_t& namedCacheTable, const string& findCacheName, const int iDebug)
{
  if(iDebug > 0) {
    printf("xxxxxx \n");
    printf("createNamedCacheEntry() - DEBUG - DEBUG - DEBUG - START - create: %s \n", findCacheName.c_str());
    printf("%s", getAllNamedCacheStatus(namedCacheTable).c_str());
    printf("****** \n");
  }

  std::lock_guard<std::mutex> guard(g_nc_table_mutex);         // stop for mutex

  std::shared_ptr<NamedCacheX> selectedCache;
  namedCaches_t::iterator it = namedCacheTable.find(findCacheName);
  if (it != namedCacheTable.end()) {
    selectedCache = it->second;
  } else {
      if(!findCacheName.empty()) {
        vinfolog("Creating named cache %s", findCacheName);
      }
      selectedCache = std::make_shared<NamedCacheX>();
      selectedCache->namedCache = std::make_shared<DNSDistNamedCache>(findCacheName, "", "NONE", "NONE", 0, iDebug);       // make empty named cache
      namedCacheTable.insert(std::pair<std::string,std::shared_ptr<NamedCacheX> >(findCacheName, selectedCache));       // insert into table
      if(iDebug > 0) {
        printf("createNamedCacheIfNotExists() - DEBUG -----> inserted %s\n", findCacheName.c_str());
      }
    }

  if(iDebug > 0) {
    printf("xxxxxx \n");
    printf("createNamedCacheEntry() - DEBUG - DEBUG - DEBUG - END - create: %s \n", findCacheName.c_str());
    printf("%s", getAllNamedCacheStatus(namedCacheTable).c_str());
    printf("****** \n");
  }

  if(iDebug > 0) {
    printf("createNamedCacheEntry() ----------> end tablesize: %lu \n", namedCacheTable.size());
    for (const auto& entry : namedCacheTable) {
      const string& strCacheName = entry.first;                           // get name
      const std::shared_ptr<NamedCacheX> cacheEntry = entry.second;       // get object - was NamedCacheX
      string strCacheName2 = cacheEntry->namedCache->getCacheName();
      printf("createNamedCacheEntry() ----------> name: %s   name2: %s \n", strCacheName.c_str(), strCacheName2.c_str());
      printf("--------------------------------------------------------------------- \n");
    }

  }


  return selectedCache;
}

// ----------------------------------------------------------------------------
// deleteNamedCacheIfNotExists()
// needs to be locked before with
//      auto namedCacheList = g_namedCaches.getCopy();
// after restored after with
//      g_namedCaches.setState(namedCacheList);
// ----------------------------------------------------------------------------
bool deleteNamedCacheEntry(namedCaches_t& namedCachesTable, const string& findCacheName, const int iDebug)
{
int iNoDeleteForDebug = 0;
  if(iNoDeleteForDebug) {
    return(true);
  }

  if(iDebug > 0) {
    printf("deleteNamedCacheEntry() ----------> start size: %lu \n",  namedCachesTable.size());
  }

  if(iDebug > 0) {
    printf("xxxxxx \n");
    printf("deleteNamedCacheEntry() - DEBUG - DEBUG - DEBUG - START - delete: %s \n", findCacheName.c_str());
    printf("%s", getAllNamedCacheStatus(namedCachesTable).c_str());
    printf("xxxxxx \n");
  }

  std::lock_guard<std::mutex> guard(g_nc_table_mutex);         // stop for mutex

  std::shared_ptr<NamedCacheX> selectedCache;
  namedCaches_t::iterator it = namedCachesTable.find(findCacheName);
  if (it != namedCachesTable.end()) {

    selectedCache = it->second;

    if(iDebug > 0) {
      printf("deleteNamedCacheEntry() - DEBUG - DEBUG - DEBUG - found: %s   foundx: %s \n",findCacheName.c_str(), selectedCache->namedCache->getCacheName().c_str());
    }

    selectedCache->namedCache->init(findCacheName, "", "", "", 0);     // minimize resources
    namedCachesTable.erase(it);                         // erase it
    if(iDebug > 0) {
      printf("deleteNamedCacheEntry() - DEBUG - DEBUG - DEBUG - deleted: %s \n",findCacheName.c_str());
    }
    return true;
  }
  if(iDebug > 0) {
    printf("deleteNamedCacheEntry() - DEBUG - DEBUG - DEBUG - COULD NOT DELETE: %s \n", findCacheName.c_str());
  }

  if(iDebug > 0) {
    printf("xxxxxx \n");
    printf("deleteNamedCacheEntry() - DEBUG - DEBUG - DEBUG - start - END: %s \n", findCacheName.c_str());
    printf("%s", getAllNamedCacheStatus(namedCachesTable).c_str());
    printf("xxxxxx \n");
  }

  return false;
}

// ----------------------------------------------------------------------------
// swapNamedCache()
// needs to be locked before with
//      auto namedCacheList = g_namedCaches.getCopy();
// after restored after with
//      g_namedCaches.setState(namedCacheList);
// return:
//      0 - ok
//      -1 couldn't find 1st name
//      -2 couldn't find 2nd name
// ----------------------------------------------------------------------------
int swapNamedCacheEntries(namedCaches_t& namedCacheTable, const string& findCacheNameA, const string& findCacheNameB, int iDebug)
{
int iNoSwapForDebug = 0;        // don't swap.... for debugging
  if(iNoSwapForDebug)
    return(0);

  std::lock_guard<std::mutex> guard(g_nc_table_mutex);         // stop for mutex


  std::shared_ptr<NamedCacheX> selectedCacheA;
  std::shared_ptr<NamedCacheX> selectedCacheB;
  std::shared_ptr<NamedCacheX> selectedCacheTemp;
  namedCaches_t::iterator itA = namedCacheTable.find(findCacheNameA);
  if (itA != namedCacheTable.end()) {
    selectedCacheA = itA->second;
  } else {
    if(iDebug > 0) {
      printf("swapNamedCacheEntries() - No match for named cache A: %s \n", findCacheNameA.c_str());
    }
    return -1;
  }

  namedCaches_t::iterator itB = namedCacheTable.find(findCacheNameB);
  if (itB != namedCacheTable.end()) {
    selectedCacheB = itB->second;
  } else {
    if(iDebug > 0) {
      printf("swapNamedCacheEntries() - No match for named cache B: %s \n", findCacheNameB.c_str());
    }
    return -2;
  }

  std::string strNameA = selectedCacheA->namedCache->getCacheName();
  std::string strNameB = selectedCacheB->namedCache->getCacheName();

  selectedCacheB->namedCache->setCacheName(strNameA);                                   // put the internal cache name for 'A' into 'B'
  selectedCacheA->namedCache->setCacheName(strNameB);                                   // put the internal cache name for 'B' into 'A'

  std::shared_ptr<DNSDistNamedCache> ncTemp = selectedCacheA->namedCache;               // swap 'A' & 'B'
  selectedCacheA->namedCache    = selectedCacheB->namedCache;
  selectedCacheB->namedCache    = ncTemp;

//  selectedCacheA->namedCache.swap(selectedCacheB->namedCache);                          // swap 'A' & 'B'

  if(iDebug > 0) {
    printf("swapNamedCacheEntries() - org  -  A: %s   B: %s \n", findCacheNameA.c_str(), findCacheNameB.c_str());
    printf("swapNamedCacheEntries() - swap -  A: %s   B: %s \n", selectedCacheA->namedCache->getCacheName().c_str(), selectedCacheB->namedCache->getCacheName().c_str());
    printf("swapNamedCacheEntries() - end  -  MaxA: %lu   MaxB: %lu \n", selectedCacheA->namedCache->getMaxEntries(), selectedCacheB->namedCache->getMaxEntries());
    for (const auto& entry : namedCacheTable) {
       const string& strCacheName = entry.first;                           // get name
       const std::shared_ptr<NamedCacheX> cacheEntry = entry.second;       // get object - was NamedCacheX
       string strCacheName2 = cacheEntry->namedCache->getCacheName();
       printf("swapNamedCacheEntries() ----------> name: %s   name2: %s   max: %lu \n", strCacheName.c_str(), strCacheName2.c_str(), cacheEntry->namedCache->getMaxEntries());
    }
    printf("--------------------------------------------------------------------- \n");
  }
  return 0;
}

// ----------------------------------------------------------------------------
std::string getAllNamedCacheStatus(namedCaches_t& namedCacheTable)
{

   ostringstream ret;
   boost::format fmt("%1$8.8s %2$8.8s %|5t|%3$4s %|5t|%4$4s %|5t|%5$4s %|5t|%6$8s %|5t|%7$8s %|5t|%8$12s %|5t|%9$12s %|5t|%10$12s %|5t|%11$12s %|5t|%12$12s %|5t|%13%");
   ret << (fmt % "Name" % "NameX" % "Type" % "Mode" % "Open" % "MaxCache" % "InCache" % "HitsCache" % "NoDataCache" % "CdbHits" % "CdbNoData"% " MissCache" % "FileName" ) << endl;
   ret << (fmt % "--------" % "--------" %"----" % "----" % "----" % "--------" % "--------" % "------------" % "------------" % "------------" % "------------" % "------------" % "--------" ) << endl;

//   const auto localNamedCaches = g_namedCaches.getCopy();                // get snapshot
//   for (const auto& entry : localNamedCaches) {

   for (const auto& entry : namedCacheTable) {
      const string& strCacheName = entry.first;                           // get name
      const std::shared_ptr<NamedCacheX> cacheEntry = entry.second;       // get object - was NamedCacheX
      string strCacheName2 = cacheEntry->namedCache->getCacheName();
      string strFileName = cacheEntry->namedCache->getFileName();
      string strType = cacheEntry->namedCache->getCacheTypeText(true);    // load / bind name
      string strMode = cacheEntry->namedCache->getCacheModeText();
      string strFileOpen = cacheEntry->namedCache->isFileOpen()?"Yes":"No";
      string strMaxEntries = std::to_string(cacheEntry->namedCache->getMaxEntries());
      string strInCache = std::to_string(cacheEntry->namedCache->getCacheEntries());
      string strHitsCache = std::to_string(cacheEntry->namedCache->getCacheHits());
      string strNoDataCache = std::to_string(cacheEntry->namedCache->getCacheHitsNoData());
      string strCdbHits = std::to_string(cacheEntry->namedCache->getCdbHits());
      string strCdbHitsNoData = std::to_string(cacheEntry->namedCache->getCdbHitsNoData());
      string strMissCache = std::to_string(cacheEntry->namedCache->getCacheMiss());
      ret << (fmt % strCacheName % strCacheName2 % strType % strMode % strFileOpen % strMaxEntries % strInCache % strHitsCache % strNoDataCache % strCdbHits % strCdbHitsNoData % strMissCache % strFileName) << endl;
   }
   return ret.str();
};

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// GCA - Seth - 10/20/2017 Thread to load cache.......
// ----------------------------------------------------------------------------
void namedCacheLoadThread(std::shared_ptr<NamedCacheX> entryCacheA, const std::string strFileName, const std::string strCacheType, const std::string strCacheMode, int iMaxEntries)
{
int iDebug = 0;
  auto start = std::chrono::system_clock::now();
  std::time_t startTime = std::chrono::system_clock::to_time_t(start);
  std::string strStartTime;
  strStartTime.append(std::ctime(&startTime), 19);


  int iCacheType = NamedCache::parseCacheTypeText(strCacheType);
  std::string strMaxEntries = std::to_string(iMaxEntries);
  if (iCacheType == CACHE_TYPE::TYPE_MAP) {
    strMaxEntries = "all";
  }
  std::string strType = NamedCache::getCacheTypeText(iCacheType, true);       // make it pretty

  std::string strCacheName = entryCacheA->namedCache->getCacheName();

  warnlog("Loading Cache: %s   Type: %s   Mode: %s   Max Entries: %s   Time: %s   File: %s ",
                                        strCacheName.c_str(), strType.c_str(), strCacheMode.c_str(), strMaxEntries.c_str(), strStartTime.c_str(), strFileName.c_str());

//  auto namedCacheList = g_namedCaches.getCopy();                // get snapshot
//  auto namedCacheList = g_namedCacheTable;
  std::string strCacheNameB = g_namedCacheTempPrefix;
  std::stringstream stream;
  stream << std::hex << g_namedCacheTempFileCount++;
  strCacheNameB += stream.str();                                // unique temporary named cache name for loading in bkg
  std::shared_ptr<NamedCacheX> entryCacheB = createNamedCacheIfNotExists(g_namedCacheTable, strCacheNameB);
//  g_namedCaches.setState(namedCacheList);

  bool bStat = entryCacheB->namedCache->init(strCacheNameB, strFileName, strCacheType, strCacheMode , iMaxEntries, iDebug);
  if(bStat == true) {
    std::lock_guard<std::mutex> lock(g_luamutex);               // lua lock till end of 'if'

    if(iDebug > 0) {
      printf("namedCacheLoadThread() - DEBUG - Before swap cacheA: %s - max: %lu   cacheB: %s - max: %lu \n",
                        entryCacheA->namedCache->getCacheName().c_str(), entryCacheA->namedCache->getMaxEntries(),
                        entryCacheB->namedCache->getCacheName().c_str(), entryCacheB->namedCache->getMaxEntries());
    }

//    auto namedCacheList1 = g_namedCaches.getCopy();             // get snapshot
    int iSwapStat =  swapNamedCacheEntries(g_namedCacheTable, strCacheName, strCacheNameB, iDebug);
    if(iSwapStat == 0) {
//       g_namedCaches.setState(namedCacheList1);                 // save snapshot
      } else {
          errlog("namedCacheLoadThread - swap failed! iSwapStat: %d   nameA: %s   nameB: %s \n", iSwapStat, strCacheName, strCacheNameB);
        }


    if(iDebug > 0) {
      printf("namedCacheLoadThread() - DEBUG - After swap cacheA: %s - max: %lu   cacheB: %s - max: %lu \n",
                    entryCacheA->namedCache->getCacheName().c_str(), entryCacheA->namedCache->getMaxEntries(), entryCacheB->namedCache->getCacheName().c_str(),entryCacheB->namedCache->getMaxEntries());
    }

//    auto namedCacheList2 = g_namedCaches.getCopy();             // get snapshot
    deleteNamedCacheEntry(g_namedCacheTable,strCacheNameB, iDebug);  // delete temp
//    g_namedCaches.setState(namedCacheList);                       // save snapshot

  } else {
      std::stringstream err;
      err << "Failed to reload named cache " << strFileName << "  -> " << entryCacheB->namedCache->getErrMsg() << "(" << entryCacheB->namedCache->getErrNum() << ")" << endl;
      string outstr = err.str();
	  errlog(outstr.c_str());
    }



  auto end = std::chrono::system_clock::now();
  std::time_t endTime = std::chrono::system_clock::to_time_t(end);
  std::string strEndTime;
  strEndTime.append(std::ctime(&endTime), 19);

  auto millSec = std::chrono::duration_cast<std::chrono::milliseconds> (end-start);
  warnlog("Finished loading Cache: %s   Max Entries: %d   Elapsed ms: %d ",  strCacheName.c_str(), entryCacheA->namedCache->getMaxEntries(), millSec.count());


}

// ----------------------------------------------------------------------------
// GCA - Seth - 10/20/2017 Thread to reload cache.......
// ----------------------------------------------------------------------------
void namedCacheReloadThread(const std::string& strCacheNameA,  boost::optional<int> maxEntries)
{
int iDebug = 0;
//  warnlog("namedCacheReload start - Cache Name: %s   Max Entries: %s", strCacheNameA.c_str(), maxEntries?std::to_string((int) *maxEntries):"all");

  auto start = std::chrono::system_clock::now();
  std::time_t startTime = std::chrono::system_clock::to_time_t(start);
  std::string strStartTime;
  strStartTime.append(std::ctime(&startTime), 19);
//  warnlog("namedCacheReloadThread start - %s", strStartTime.c_str());

///  auto namedCacheList = g_namedCaches.getCopy();            // get snapshot
//  auto namedCacheList = g_namedCacheTable;

  std::shared_ptr<NamedCacheX> entryCacheA = createNamedCacheIfNotExists(g_namedCacheTable, strCacheNameA);

  std::string strFileNameA = entryCacheA->namedCache->getFileName();
  std::string strCacheTypeA = entryCacheA->namedCache->getCacheTypeText();
  std::string strCacheModeA = entryCacheA->namedCache->getCacheModeText();
  int iMaxEntriesA = entryCacheA->namedCache->getMaxEntries();
  if(maxEntries) {
    iMaxEntriesA = *maxEntries;
  }

  warnlog("Reloading Cache: %s   Max Entries: %s   Time: %s ", strCacheNameA.c_str(), maxEntries?std::to_string((int) *maxEntries):"all", strStartTime.c_str());

  std::string strCacheNameB = g_namedCacheTempPrefix;
  std::stringstream stream;
  stream << std::hex << g_namedCacheTempFileCount++;
  strCacheNameB += stream.str();                            // unique temp name

  std::shared_ptr<NamedCacheX> entryCacheB = createNamedCacheIfNotExists(g_namedCacheTable, strCacheNameB);        // create temp

//  g_namedCaches.setState(namedCacheList);                   // save snapshot


                                                            // load data into temp
  bool bStat = entryCacheB->namedCache->init(strCacheNameB, strFileNameA, strCacheTypeA, strCacheModeA , iMaxEntriesA, iDebug);
  if(bStat == true) {
    std::lock_guard<std::mutex> lock(g_luamutex);           // lua lock till end of 'if'

//    auto namedCacheList1 = g_namedCaches.getCopy();         // get snapshot
    int iSwapStat =  swapNamedCacheEntries(g_namedCacheTable, strCacheNameA, strCacheNameB, iDebug);   // swap
    if(iSwapStat == 0) {
//       g_namedCaches.setState(namedCacheList1);                 // save snapshot
      } else {
          warnlog("namedCacheReloadThread - swap failed! iSwapStat: %d   nameA: %s   nameB: %s \n", iSwapStat, strCacheNameA, strCacheNameB);
        }
//    g_namedCaches.setState(namedCacheList1);                 // save snapshot

//    auto namedCacheList2 = g_namedCaches.getCopy();          // get snapshot
    deleteNamedCacheEntry(g_namedCacheTable,strCacheNameB, 0); // delete temp
//    g_namedCaches.setState(namedCacheList);                    // save snapshot
  } else {
      std::stringstream err;
      err << "Failed to reload named cache " << strFileNameA << "  -> " << entryCacheB->namedCache->getErrMsg() << "(" << entryCacheB->namedCache->getErrNum() << ")" << endl;
      string outstr = err.str();
	  errlog(outstr.c_str());
    }



  auto end = std::chrono::system_clock::now();
  std::time_t endTime = std::chrono::system_clock::to_time_t(end);
  std::string strEndTime;
  strEndTime.append(std::ctime(&endTime), 19);

//  warnlog("namedCacheReloadThread iMaxEntriesA: %d ", iMaxEntriesA);
  auto millSec = std::chrono::duration_cast<std::chrono::milliseconds> (end-start);
  warnlog("Finished reloading Cache: %s   Max Entries: %d   Elapsed ms: %d ", strCacheNameA.c_str(), iMaxEntriesA, millSec.count());


}

#endif
