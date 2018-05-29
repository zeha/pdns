#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <cdb.h>
#include <map>
#include <unordered_map>

#include "config.h"
#include "misc.hh"

#include "dnsdist-nc-cdbio.hh"

#ifdef HAVE_NAMEDCACHE

class LRUCache  {
  typedef typename std::string keyType;
  typedef typename std::string valueType;
  typedef typename std::pair<keyType, valueType> key_value_pair_t;
  typedef typename std::list<key_value_pair_t>::iterator list_iterator_t;

public:
  LRUCache(const std::string& fileName, size_t maxEntries);
  ~LRUCache();
  size_t  getMaxEntries();
  int  getErrNum(void);
  std::string getErrMsg(void);
  int  lookup(const std::string strKey, std::string &result);
  size_t  getEntries();

private:
  std::list<key_value_pair_t> cacheList;                  // front is most recent use
  std::unordered_map<keyType, list_iterator_t> cacheMap;  // key with ptr to list entry
  size_t d_maxentries;                                    // max entries to allow
  std::unique_ptr<cdbIO> d_cdb;
  void put(const std::string& key, const std::string& value);
  bool get(const std::string& key, std::string &val);
};

#endif
