#ifndef __APPLE__
#include <malloc.h>
#endif

#include "config.h"
#include "dolog.hh"

#include "dnsdist-nc-lrucache.hh"
#include "dnsdist-namedcache.hh"

#ifdef HAVE_NAMEDCACHE

/*
   GCA - least recently used cache for named caches
   Note that even though lruCache is not thread safe,
   it doesn't have to be as all calls are from LUA code.

   Comment from: https://dnsdist.org/advanced/tuning.html
        "Lua processing in dnsdist is serialized by an unique lock for all threads."
   This should be ok as all calls to lruCache are thru lua with is single threaded for dnsdist
*/


// LRCCache object
// see:  http://cr.yp.to/cdb/reading.html
LRUCache::LRUCache(const std::string& fileName, int maxEntries) : d_maxentries(maxEntries)
{
  d_cdb = make_unique<cdbIO>(fileName);
}

bool LRUCache::get(const std::string& key, std::string &val)
{
  auto it = cacheMap.find(key);                         // find entry in map
  if(it == cacheMap.end()) {
    return(false);                                      // didn't find
  } else {                                              // move entry to top of list
    cacheList.splice(cacheList.begin(), cacheList, it->second);
    val = it->second->second;                         // return value
    return(true);
  }
}

void LRUCache::put(const std::string& key, const std::string& value)
{
  auto it = cacheMap.find(key);                         // get old entry in map
  cacheList.push_front(key_value_pair_t(key, value));   // put new entry in front of list
  if(it != cacheMap.end()) {
    cacheList.erase(it->second);                        // remove old entry from list
    cacheMap.erase(it);                                 // remove old entry from map
  }
  cacheMap[key] = cacheList.begin();                    // put new entry (front of list) into map

  if(cacheMap.size() > d_maxentries) {
    auto last = cacheList.end();                        // too big, shrink
    last--;                                             // get end of list entry
    cacheMap.erase(last->first);                        // remove entry from map
    cacheList.pop_back();                               // remove end of list
  }
}

int LRUCache::getMaxEntries()
{
  return d_maxentries;
}

int LRUCache::getEntries()
{
  return cacheMap.size();
}

int LRUCache::getErrNum()
{
  return d_cdb->getErrNum();
}

std::string LRUCache::getErrMsg()
{
  return d_cdb->getErrMsg();
}

LRUCache::~LRUCache()
{
}

//  read from cache / cdb - strValue cleared if not written to
int LRUCache::getCache(const std::string strKey, std::string &strValue)
{
  if(get(strKey, strValue) == true) {           // read from cache
      if(strValue.empty()) {
        return(CACHE_HIT::HIT_CACHE_NO_DATA);   // no data in cache entry
      }
      return(CACHE_HIT::HIT_CACHE);             // data in cache entry
  }

  if(d_cdb->get(strKey, strValue) == true) {     // not in cache, read from CDB
    put(strKey, strValue);                      // store in cache
    if(strValue.empty()) {
      return(CACHE_HIT::HIT_CDB_NO_DATA);       // no data in cdb entry
    }
    return(CACHE_HIT::HIT_CDB);                 // data in cdb entry
  }
  return(CACHE_HIT::HIT_NONE);                  // ret status is 'no hit'
}

#endif
