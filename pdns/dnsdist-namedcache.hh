#pragma once

#include <atomic>
#include <chrono>
#include <ctime>

#include "dnsdist.hh"
#include "misc.hh"
#include "dolog.hh"
#include "config.h"
#include "gettime.hh"

#include "dnsdist-nc-lrucache.hh"

/*
  GCA - named cache
  Notes:

        DNSDistNamedCache(filename, maxEntries)
                fileName - entire path name to cdb file.
                maxEntries - maximum amount of LRU entries to store in cache

        getCacheHits() - get total number of cache matching hits

        getCacheMiss() - get total number of cache misses

        lookup(strQuery, strData) - return state of lookup
                                 CACHE_HIT::HIT_NONE          - 0 (match not found)
                                 CACHE_HIT::HIT_CDB           - 1 (match found in cdb file, data field not empty)
                                 CACHE_HIT::HIT_CACHE         - 2 (match found in cache, data field not empty)
                                 CACHE_HIT::HIT_CACHE_NO_DATA - 3 (match found in cache, empty data field)
                                 CACHE_HIT::HIT_CDB_NO_DATA   - 4 (match found in cdb file, empty data field)
                strQuery - the dns name to lookup
                strData  - return string with contents of cdb entry, else empty string
*/

#ifdef HAVE_NAMEDCACHE

#define DELTA_AVG 1000

enum CACHE_HIT {HIT_NONE, HIT_CDB, HIT_CACHE, HIT_CDB_NO_DATA, HIT_CACHE_NO_DATA};

class DNSDistNamedCache
{
public:
  DNSDistNamedCache(const std::string& fileName, size_t maxEntries);
  ~DNSDistNamedCache();

  void close();
  void reopen();
  int getErrNum();
  std::string   getErrMsg();
  uint64_t getCacheEntries();
  int      getQuerySec();
  uint64_t getCacheHits();
  uint64_t getCacheHitsNoData();
  uint64_t getCdbHits();
  uint64_t getCdbHitsNoData();
  uint64_t getCacheMiss();
  int      lookup(const std::string& strQuery, std::string& result);
  time_t getCreationTime();
  time_t getCounterResetTime();
  std::string getStatusText();
  void getStatusTable(std::unordered_map<string, string> &tableResult);
  void     resetCounters();
  std::string toString() const;

private:
  std::string d_filename;
  size_t d_maxentries;
  time_t d_tcreated;

  std::unique_ptr<LRUCache> d_cacheholder{nullptr};
  std::atomic<uint64_t> cacheHits{0};
  std::atomic<uint64_t> cacheHitsNoData{0};
  std::atomic<uint64_t> cdbHits{0};
  std::atomic<uint64_t> cdbHitsNoData{0};
  std::atomic<uint64_t> cacheMiss{0};
  std::atomic<double>   deltaLookup[DELTA_AVG];
  int deltaCount;
  time_t d_tcountersreset;
  std::unique_ptr<struct StopWatch> d_sw{nullptr};
};

#endif
