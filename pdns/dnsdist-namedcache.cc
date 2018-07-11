#include "dnsdist-namedcache.hh"

#ifdef HAVE_NAMEDCACHE

using namedCaches_t = std::unordered_map<std::string, std::shared_ptr<DNSDistNamedCache>>;

std::mutex g_nc_table_mutex;        // mutex to prevent r/w named cache table problems
namedCaches_t g_namedCacheTable;
std::atomic<std::uint16_t> g_namedCacheTempFileCount;
std::string g_namedCacheTempPrefix = "-4rld";

DNSDistNamedCache::DNSDistNamedCache(const std::string& fileName, size_t maxEntries) :
 d_filename(fileName), d_maxentries(maxEntries), d_sw(make_unique<StopWatch>())
{
  d_tcreated = time(NULL);
  resetCounters();
  reopen();
}

DNSDistNamedCache::~DNSDistNamedCache() {
}

// 'close' object
void DNSDistNamedCache::close(void) {
  d_cacheholder = nullptr;
}

void DNSDistNamedCache::reopen(void) {
  d_cacheholder = make_unique<LRUCache>(d_filename, d_maxentries);
}

// lookup query in cache
int DNSDistNamedCache::lookup(const std::string& strQuery, std::string& result) {
  if (!d_cacheholder) {
    return CACHE_HIT::HIT_NONE;
  }

  int iLoc = d_cacheholder->lookup(strQuery, result);
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
  deltaLookup[deltaCount++] = d_sw->udiffAndSet();
  if(deltaCount == DELTA_AVG)
    deltaCount = 0;
  return iLoc;
}

int DNSDistNamedCache::getErrNum()
{
  if (!d_cacheholder) {
    return -EINVAL;
  }
  return d_cacheholder->getErrNum();
}

std::string DNSDistNamedCache::getErrMsg()
{
  if (!d_cacheholder) {
    return "";
  }
  return d_cacheholder->getErrMsg();
}

// get current number of entries in cache
uint64_t DNSDistNamedCache::getCacheEntries()
{
  if (!d_cacheholder) {
    return 0;
  }
  return(d_cacheholder->getEntries());
}

// get current number of queryies per sec
int DNSDistNamedCache::getQuerySec()
{
double deltaAvg = 0.0;

  if(d_sw->udiff() > 1000000.0) {
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
  d_tcountersreset = time(NULL);

  deltaCount = 0;
  for(int ii=0; ii < DELTA_AVG; ii++)
     deltaLookup[ii] = 0.0;
  d_sw->start();
}

time_t DNSDistNamedCache::getCreationTime()
{
  return d_tcreated;
}

time_t DNSDistNamedCache::getCounterResetTime()
{
  return d_tcountersreset;
}

std::string DNSDistNamedCache::toString() const
{
  return string("NamedCache(\"") + d_filename + "\", " + std::to_string(d_maxentries) + ")";
}

std::string DNSDistNamedCache::getStatusText()
{
  std::string strText = "";

  time_t tTemp = getCreationTime();  // Pretty-print creation time of named cache.
  struct tm* tm = localtime(&tTemp);
  char cTimeBuffer[30];
  strftime(cTimeBuffer, sizeof(cTimeBuffer), "%F %T %z", tm);
  cTimeBuffer[sizeof(cTimeBuffer)-1] = '\0';
  strText += "Creation time...: " + string(cTimeBuffer) + "\n";

  strText += "CDB file........: " + d_filename + "\n";
  strText += "Open............: " + std::string(d_cacheholder ? "yes" : "no") + "\n";

  strText += "Error number....: " + std::to_string(getErrNum()) + "\n";
  strText += "Error message...: " + getErrMsg() + "\n";
  strText += "Max. Entries....: " + std::to_string(d_maxentries) + "\n";
  strText += "Entries in use..: " + std::to_string(getCacheEntries()) + "\n";

  // Pretty-print the last time the named cache's counters were reset
  tTemp = getCounterResetTime();
  tm = localtime(&tTemp);
  strftime(cTimeBuffer, sizeof(cTimeBuffer), "%F %T %z", tm);
  cTimeBuffer[sizeof(cTimeBuffer)-1] = '\0';
  strText += "Counter reset...: " + string(cTimeBuffer) + "\n";

  strText += "Cache hits......: " + std::to_string(getCacheHits()) + "\n";
  strText += "Cache hits !data: " + std::to_string(getCacheHitsNoData()) + "\n";
  strText += "CDB hits........: " + std::to_string(getCdbHits()) + "\n";
  strText += "CDB hits !data..: " + std::to_string(getCdbHitsNoData()) + "\n";
  strText += "Cache misses....: " + std::to_string(getCacheMiss()) + "\n";
  strText += "Query per Sec...: " + std::to_string(getQuerySec()) + "\n";

  return strText;
}

void DNSDistNamedCache::getStatusTable(std::unordered_map<string, string> &tableResult)
{
  tableResult.insert({"file", d_filename});   // file used
  tableResult.insert({"maxEntries", std::to_string(d_maxentries)});
  tableResult.insert({"open", std::to_string(!!d_cacheholder)});
  tableResult.insert({"cacheEntries", std::to_string(getCacheEntries())});
  tableResult.insert({"cacheHits", std::to_string(getCacheHits())});
  tableResult.insert({"cacheHitsNoData", std::to_string(getCacheHitsNoData())});
  tableResult.insert({"cdbHits", std::to_string(getCdbHits())});
  tableResult.insert({"cdbHitsNoData", std::to_string(getCdbHitsNoData())});
  tableResult.insert({"cacheMiss", std::to_string(getCacheMiss())});
  tableResult.insert({"errMsg", getErrMsg()});
  tableResult.insert({"errNum", std::to_string(getErrNum())});
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
