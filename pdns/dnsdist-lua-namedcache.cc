/*
 * This file is part of PowerDNS or dnsdist.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "dnsdist.hh"
#include "dnsdist-ecs.hh"
#include "dnsdist-lua.hh"
#include "dnsdist-protobuf.hh"

#include <map>
#include <fstream>
#include <boost/logic/tribool.hpp>
#include <boost/variant.hpp>
#include <sstream>

#include "dolog.hh"
#include "ednsoptions.hh"
#include "remote_logger.hh"


// GCA - allow protobuf generation & execute next action, alternate method
class RemoteLogActionX : public DNSAction, public boost::noncopyable
{
public:

  RemoteLogActionX(std::shared_ptr<RemoteLoggerInterface> logger, boost::optional<std::function<std::tuple<int, string>(DNSQuestion*, DNSDistProtoBufMessage*)> > alterFuncX): d_logger(logger), d_alterFuncX(alterFuncX)
  {
  }
  DNSAction::Action operator()(DNSQuestion* dq, string* ruleresult) const override
  {
   int iRetAction = (int) DNSAction::Action::None;       // default action

#ifdef HAVE_PROTOBUF
    DNSDistProtoBufMessage message(*dq);
      if (d_alterFuncX) {
        std::lock_guard<std::mutex> lock(g_luamutex);
        auto ret  = (*d_alterFuncX)(dq, &message);
        std::string strTemp = std::get<1>(ret);
        iRetAction = (int) std::get<0>(ret);
        std::string data;
       switch(iRetAction) {
         case (int)DNSAction::Action::Nxdomain:             // Nxdomain - send out protobuf
            message.serialize(data);
            d_logger->queueData(data);
            break;
         case (int)DNSAction::Action::Spoof:                // Spoof - send out protobuf
            std::vector<std::string> addrs;
            stringtok(addrs, strTemp, " ,");
            if (addrs.size() > 0) {
              ComboAddress spoofAddr(strTemp);
              message.setResponder(spoofAddr);              // need to set spoof address out thru protobuf msg
            }
            message.serialize(data);
            d_logger->queueData(data);
            break;
       }
        if(ruleresult)
           *ruleresult=std::get<1>(ret);
        return (Action)std::get<0>(ret);
      }

#endif /* HAVE_PROTOBUF */

    return (DNSAction::Action) iRetAction;
  }
  string toString() const override
  {
    return "remote logX to " + d_logger->toString();
  }
private:
  std::shared_ptr<RemoteLoggerInterface> d_logger;
  boost::optional<std::function<std::tuple<int, string>(DNSQuestion*, DNSDistProtoBufMessage*)> > d_alterFuncX;       // returns int/string now!

};

void setupLuaNamedCache(bool client)
{
#ifdef HAVE_NAMEDCACHE

/*
   GCA - new lua functions

   NamedCacheX general methods:
       newNamedCache([strCacheName]) - create a named cache if it doesn't exist
                          - parameters:
                                 strCacheName - cache name, defaults to "default"
                          - example: addNamedCache("yyy")
       showNamedCaches()  - display list of named caches on terminal
                          - example: showNamedCaches()

       closeNamedCache() - close a named cache, creates it if doesn't exist.
                            Used to free up resources used by a no longer needed cache
                            Unlike deleteNamedCache() this function will leave a
                            working empty named cache
                          - example: closeNamedCache("yyy")
       deleteNamedCache() - delete a named cache
                          - example: deleteNamedCache("yyy")
       reloadNamedCache([strCacheNameA], [maxEntries])
                          - parameters:
                            [strCacheName] - named cache to reload - else "default"
                            [maxEntries] - maxEntries if it is going to be changed, only works if "bindToCDB" type named cache.
                          - example:reloadNamedCache("xxx")

   NamedCacheX methods:
        getNamedCache([strCacheName]) - get named cache ptr, create if not exist
                           - parameters:
                                 strCacheName - cache name, defaults to "default"
                           - example:
                                 ncx = getNamedCache("xxx")
        getStats()         - get statistics in a table
                           - example:
                                 tableStat2 = ncx:getStats()
                                 getNamedCache("xxx"):getStats()
        showStats()        - directly print out statistics to terminal
                           - example:
                                 getNamedCache("xxx"):showStats()
        resetCounters()    - reset statistic counters for the named cache
                           - example:
                                 getNamedCache("xxx"):resetCounters()
        wasFileOpenedOK()  - return true if cdb file was opened OK
                           - example:
                                 print(getNamedCache("xxx"):isFileOpen())
        loadFromCDB()      - (re)initialize a new cache entirely into memory
                           - parameters:
                                 strCacheName - cdb file path name to use
                           - examples:
                                 loadFromCDB("xxx.cdb")
        bindToCDB()        - (re)initialize a new lru cache using a cdb file
                           - parameters:
                                 strCacheName - cdb file path name to use
                                 maxEntries - number of max entries to store in memory  -- optional parameter
                                           The default is 100000
                                 strMode - "none", "cdb", "all"  -- optional parameter
                                           none - store nothing in memory
                                           cdb  - store cdb hits in memory
                                           all  - store all requests (even missed) in memory
                                           The default is "cdb"
                           - examples:
                                loadFromCDB("xxx.cdb", 200000, "cdb")
                                loadFromCDB("xxx.cdb")  -- mode defaults to cdb, maxEntries to 100000

        newDNSDistProtobufMessage() - Ari's function to create a protobuf message 'on the fly'
                                      used with remoteLog()
                           - parameters:
                                 DNSQuestion
                           - example:
                                 m = newDNSDistProtobufMessage(dq)
                           - return lua object

        remoteLog()        - Ari's function to generate a protobuf message 'on the fly'
                             used with newDNSDistProtobufMessage()
                           - parameters:
                                 rlBlkLst - object created with newRemoteLogger
                                 m - object create with newDNSDistProtobufMessage
                           - example:
                                 remoteLog(rlBlkLst, m)

        RemoteLogActionX() - Seth's function to modify a protobuf message and allow other actions -  going with Ari's
                           - parameters:
                                 rlBlkLst - object create with newRemoteLogger
                                 luaCheckLogBLK - lua subroutine to be called
                           - example:
                                 addAction(AllRule(), RemoteLogActionX(rlBlkLst, luaCheckLogBLX))
                           - notes:
                                 function setup to be called has two parameters and must return one
                                    parameters:
                                        dq - DNSQuestion
                                        pbMsg - DNSDistProtoBufMessage
                                    return:
                                       DNSAction type like DNSAction.Nxdomain or DNSAction.None
                                 example - luaCheckLogBLX(dq, pbMsg)

    Debugging functions:
        getErrNum()        - return error message number (errno, from last i/o operation)
                           - example:
                                 print(getNamedCache("xxx"):getErrNum())
        getErrMsg()        - return error message text
                           - example:
                                 print(getNamedCache("xxx"):getErrMsg())
*/


    g_lua.writeFunction("showNamedCaches", []() {
      setLuaNoSideEffect();
      try {
        g_outputBuffer = getAllNamedCacheStatus(g_namedCacheTable);
      }catch(std::exception& e) { g_outputBuffer=e.what(); throw; }
    });

  /* newNamedCache(cacheName)
   *
   * Create a named cache if it doesn't exist, does not return anything.
   *
   * Example:
   *
   * 	newNamedCache("foo")
   *
   *    optional debug settings:
   *            bit flags:
   *             0 = no debugging (default)
   *             1 = display debugging on console
   *             2 = slow loading of cdb file for loadFromCDB and reloadNamedCache()
   *             4 = call linux trim function when closing named cache
   *             8 = detailed display of cdb data when loading for loadFromCDB
   *            16 = make empty 'test' cache, allways return 'cache hit'
   */

  g_lua.writeFunction("newNamedCache", [](const boost::optional<std::string> cacheName, const boost::optional<int> debug) {
      setLuaSideEffect();
      std::string strCacheName ="";
      if(cacheName) {
        strCacheName = *cacheName;
      }
      if(strCacheName.length() == 0) {
          const char *strMsg = "Error creating new named cache, no name supplied.";
          g_outputBuffer=strMsg;
	      errlog(strMsg);
	      return;
        }
      if(boost::starts_with(strCacheName, g_namedCacheTempPrefix)) {
          const char *strMsg = "Error creating new named cache, don't use named cache temp prefix.";
          g_outputBuffer= strMsg;
	      errlog(strMsg);
	      return;
        }

      int iDebug = 0;
      if(debug) {
        iDebug = *debug;
        warnlog("DEBUG - newNamedCache() - debug flag set to: %d - %s ", iDebug, BaseNamedCache::getDebugText(iDebug).c_str());
      }

      createNamedCacheIfNotExists(g_namedCacheTable, strCacheName, iDebug);
    });

  g_lua.writeFunction("closeNamedCache", [](const boost::optional<std::string> cacheName) {
      setLuaSideEffect();
      std::string strCacheName ="";
      if(cacheName) {
        strCacheName = *cacheName;
      }
      if(strCacheName.length() == 0) {
          const char *strMsg = "Error closing named cache, no name supplied.";
          g_outputBuffer=strMsg;
	      errlog(strMsg);
	      return;
        }
      if(boost::starts_with(strCacheName, g_namedCacheTempPrefix)) {
          const char *strMsg = "Error closing named cache, don't use named cache temp prefix.";
          g_outputBuffer=strMsg;
	      errlog(strMsg);
	      return;
        }

      std::shared_ptr<DNSDistNamedCache> selectedEntry = createNamedCacheIfNotExists(g_namedCacheTable, strCacheName);
      selectedEntry->close();                   // remove resources from the 'temp' named cache

    });

  g_lua.writeFunction("reloadNamedCache", [](const boost::optional<std::string> cacheName, boost::optional<int> maxEntries) {
      setLuaSideEffect();
      std::string strCacheNameA = "";
      if(cacheName) {
        strCacheNameA = *cacheName;
      }
      if(strCacheNameA.length() == 0) {
          const char *strMsg = "Error reloading named cache, no name supplied.";
          g_outputBuffer=strMsg;
	      errlog(strMsg);
	      return;
        }
      if(boost::starts_with(strCacheNameA, g_namedCacheTempPrefix)) {
          const char *strMsg = "Error reloading named cache, don't use named cache temp prefix.";
          g_outputBuffer=strMsg;
	      errlog(strMsg);
	      return;
        }

      std::thread t(namedCacheReloadThread, strCacheNameA, maxEntries);
	  t.detach();

    });

  g_lua.writeFunction("deleteNamedCache", [](const boost::optional<std::string> cacheName) {
      setLuaSideEffect();
      std::string strCacheName ="";
      if(cacheName) {
        strCacheName = *cacheName;
      }
      if(strCacheName.length() == 0) {
          const char *strMsg = "Error deleting named cache, no name supplied.";
          g_outputBuffer=strMsg;
	      errlog(strMsg);
	      return;
        }
      if(boost::starts_with(strCacheName, g_namedCacheTempPrefix)) {
          const char *strMsg = "Error deleting new named cache, don't use named cache temp prefix.";
          g_outputBuffer=strMsg;
	      errlog(strMsg);
	      return;
        }
      deleteNamedCacheEntry(g_namedCacheTable, strCacheName);
    });


// NamedCacheX general methods

  /* getNamedCache(cacheName)
   *
   * Retrieve the named cache with the given cacheName.
   * If the named cache does not exist, or the caller provides an empty string,
   * then this function will raise an error.
   *
   * Example:
   * 	
   * 	local nc = getNamedCache("foo")
   *
   *    optional debug settings:
   *            bit flags:
   *             0 = no debugging (default)
   *             1 = display debugging on console
   *             2 = slow loading of cdb file for loadFromCDB and reloadNamedCache()
   *             4 = call linux trim function when closing named cache
   *             8 = detailed display of cdb data when loading for loadFromCDB
   *            16 = make empty 'test' cache, allways return 'cache hit'
   */
    g_lua.writeFunction("getNamedCache", [client](const boost::optional<std::string> cacheName, const boost::optional<int> debug) {
      setLuaSideEffect();

      std::string name = "";
      if (cacheName) {
        name = *cacheName;
      }
      if (name.length() == 0) {
        throw std::runtime_error("You must specify a cache name");
      }
      if(boost::starts_with(name, g_namedCacheTempPrefix)) {
        throw std::runtime_error("Error getting named cache, don't use named cache temp prefix.");
      }

      int iDebug = 0;
      if(debug) {
        iDebug = *debug;
        warnlog("DEBUG - getNamedCache() - name: %s   debug flag set to: %d - %s ",name.c_str(), iDebug, BaseNamedCache::getDebugText(iDebug).c_str());
      }
      std::shared_ptr<DNSDistNamedCache> nc = createNamedCacheIfNotExists(g_namedCacheTable, name, iDebug);

      return nc;
    });

    g_lua.registerFunction<std::unordered_map<string, string>(std::shared_ptr<DNSDistNamedCache>::*)()>("getStats", [](const std::shared_ptr<DNSDistNamedCache>pool) {
        std::unordered_map<string, string> tableResult;
        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
          if (nc) {
            nc->getNamedCacheStatusTable(tableResult);
          }
        }
        return(tableResult);
    });

    g_lua.registerFunction<void(std::shared_ptr<DNSDistNamedCache>::*)()>("showStats", [](const std::shared_ptr<DNSDistNamedCache> pool) {

        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
	  if (!nc) {
	    const char *strMsg = "Cannot show statistics for a nil named cache";
	    g_outputBuffer = strMsg;
	    errlog(strMsg);
	    return;
	  }
	  g_outputBuffer = nc->getNamedCacheStatusText();
        } else {
          throw std::runtime_error("Cannot show statistics for a nil named cache");
	}
      });

    g_lua.registerFunction<void(std::shared_ptr<DNSDistNamedCache>::*)()>("resetCounters", [](const std::shared_ptr<DNSDistNamedCache> pool) {
        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
          if (nc) {
            nc->resetCounters();
            }
          }
      });
    g_lua.registerFunction<bool(std::shared_ptr<DNSDistNamedCache>::*)()>("wasFileOpenedOK", [](const std::shared_ptr<DNSDistNamedCache>pool) {
        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
          if (nc) {
            return(nc->isFileOpen());
          }
        }
        return false;
      });


    g_lua.registerFunction<int(std::shared_ptr<DNSDistNamedCache>::*)()>("getErrNum", [](const std::shared_ptr<DNSDistNamedCache>pool) {
        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
          if (nc) {
            return(nc->getErrNum());
          }
        }
        return 0;
      });


    g_lua.registerFunction<std::string(std::shared_ptr<DNSDistNamedCache>::*)()>("getErrMsg", [](const std::shared_ptr<DNSDistNamedCache>pool) {
        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
          if (nc) {
            return nc->getErrMsg();
          }
        }
	std::string empty = "";
	return empty;
      });

    /* NamedCacheX::loadFromCDB(fileName)
     *
     * Populate a named cache with the contents of the CDB file at fileName.
     *
     * loadFromCDB will return a boolean value to indicate whether or not the
     * loading operation was successful.
     */
     g_lua.registerFunction<bool(std::shared_ptr<DNSDistNamedCache>::*)(const string&)>("loadFromCDB", [client](const std::shared_ptr<DNSDistNamedCache>pool, const string& fileName) {
        if (client)
          return false;
        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
          if (nc) {         
            std::thread t(namedCacheLoadThread, pool, fileName, "MAP", "CDB", 1, CACHE_DEBUG::DEBUG_NONE);
	        t.detach();
            return true;
	  }
	}
	return false;
    });

    /* NamedCacheX::bindToCDB(fileName[, cacheSize[, cacheType]])
     *
     * Bind a named cache to a CDB at path fileName.
     *
     * Optionally, the caller may also specify the maximum number of entries to
     * cache in memory. If the caller does not specify this value, the default
     * value of 100000 will be used.
     *
     * The caller may also specify the type of entries to cache, in memory:
     *
     * 		"all"	Store both CDB hits, and misses in memory.
     * 		"cdb"	Store only CDB hits (default).
     *
     * This method will return a boolean value to indicate whether or not the
     * named cache was successfully able to bind to a named cache.
     *
     * Examples:
     *
     * 	The most simple invocation, which will cache up to 100 000, successful
     * 	CDB lookups in memory:
     *
     * 		let ok = nc:bindToCDB("path/to/my.cdb")
     *
     *  Cache up to 1 million CDB lookup hits and misses:
     *
     *  	let ok = nc:bindToCDB("path/to/my.cdb", 1000000, "all")
     */

    g_lua.registerFunction<bool(std::shared_ptr<DNSDistNamedCache>::*)(const string&, const boost::optional<int>, const boost::optional<std::string>)>("bindToCDB", [client](const std::shared_ptr<DNSDistNamedCache>pool,
                    const string& fileName, const boost::optional<int> maxEntries, const boost::optional<std::string> cacheMode) {
        if (client)
          return false;
        if (pool) {
          std::shared_ptr<DNSDistNamedCache> nc = pool;
          if (nc) {
	    // Determine whether to cache only CDB hits in memory, or  CDB hits and misses.
	    std::string cmode = "cdb";
	    if (cacheMode) {
	      cmode = *cacheMode;
	    }

	    // Determine the maximum number of entries to store in memory.
	    int csize = 100000;
	    if (maxEntries) {
	      csize = *maxEntries;
	    }

        std::thread t(namedCacheLoadThread, pool, fileName, "LRU", cmode, csize, CACHE_DEBUG::DEBUG_NONE);
	    t.detach();
	    return true;
	  }
        }
	return false;
    });

  /* NamedCacheX::lookupQWild(DNSQuestion, minLabels)
   * A depth lookup is done starting from minLabels labels. If QName has less than minLabels labels, the entire QName is looked up once.
   *
   * Example of lookups done with minLabels=2 and QName foo.bar.foo.example.com.:
   *  example.com.
   *  foo.example.com.
   *  bar.foo.example.com.
   *  foo.bar.foo.example.com.
   */
  g_lua.registerFunction<std::unordered_map<string, boost::variant<string, bool> >(std::shared_ptr<DNSDistNamedCache>::*)(DNSQuestion *dq, int minLabels)>("lookupQWild", [](const std::shared_ptr<DNSDistNamedCache> pool, DNSQuestion *dq, int minLabels) {
    std::unordered_map<string, boost::variant<string, bool>> tableResult;
    if (! (pool)) {
      return tableResult;
    }
    std::shared_ptr<DNSDistNamedCache> nc = pool;

    DNSName reverse = dq->qname->makeLowerCase().labelReverse();
    int totalLabelCount = reverse.countLabels();
    DNSName key;

    bool found = false;
    std::string strRet;
    for (const auto& label : reverse.getRawLabels()) {
      key.appendRawLabel(label);
      if (totalLabelCount >= minLabels && key.countLabels() < minLabels) {
        // skip actual lookup
        continue;
      }

      // Normalize the query, by converting it to lower-case, and remove the
      // trailing period, if there is one.
      std::string strQuery = key.labelReverse().toString();
      if(strQuery.back() == '.') {
        strQuery.pop_back();
      }
      if (strQuery.length() == 0) {
        throw std::runtime_error("The DNS question's QNAME is a zero-length string");
      }

      int hitType = nc->lookup(strQuery, strRet);
      found = !(hitType == CACHE_HIT::HIT_NONE);
      if (found) {
        break;
      }
    }

    tableResult.insert({"found", found});
    tableResult.insert({"data", strRet});

    // Make sure the DNSQuestion.QTag field is initialized, and add the
    // qtags to the DNSQuestion.
    if(dq->qTag == nullptr) {
      dq->qTag = std::make_shared<QTag>();
    }

    dq->qTag->insert({"found", std::string(found ? "yes": "no")});
    if (found) {
      dq->qTag->insert({"data", strRet});
    }
    return tableResult;
  });

#endif // HAVE_NAMEDCACHE

// GCA - Protobuf generation and allowing next 'action' to occur
    g_lua.writeFunction("RemoteLogActionX", [](std::shared_ptr<RemoteLoggerInterface> logger, boost::optional<std::function<std::tuple<int, string>(DNSQuestion*, DNSDistProtoBufMessage*)> > alterFuncX) {
      // avoids potentially-evaluated-expression warning with clang.
      RemoteLoggerInterface& rl = *logger.get();
      if (typeid(rl) != typeid(RemoteLogger)) {
        // We could let the user do what he wants, but wrapping PowerDNS Protobuf inside a FrameStream tagged as dnstap is logically wrong.
        throw std::runtime_error("RemoteLogActionX only takes RemoteLogger.");
      }
#ifdef HAVE_PROTOBUF
        return std::shared_ptr<DNSAction>(new RemoteLogActionX(logger, alterFuncX));
#else
        throw std::runtime_error("Protobuf support is required to use RemoteLogActionX");
#endif
      });

// GCA - Allow Protobuf generation 'on the fly'
    g_lua.writeFunction("newDNSDistProtobufMessage", [](DNSQuestion *dq) {
#ifdef HAVE_PROTOBUF
            return std::shared_ptr<DNSDistProtoBufMessage>(new DNSDistProtoBufMessage(*dq));
#else
            throw std::runtime_error("Protobuf support is required to use newDNSDistProtobufMessage");
#endif /* HAVE_PROTOBUF */
      });

    g_lua.writeFunction("remoteLog", [](std::shared_ptr<RemoteLoggerInterface> logger, std::shared_ptr<DNSDistProtoBufMessage> message) {
      // avoids potentially-evaluated-expression warning with clang.
      RemoteLoggerInterface& rl = *logger.get();
      if (typeid(rl) != typeid(RemoteLogger)) {
        // We could let the user do what he wants, but wrapping PowerDNS Protobuf inside a FrameStream tagged as dnstap is logically wrong.
        throw std::runtime_error("remoteLog only takes RemoteLogger.");
      }
            setLuaNoSideEffect();

#ifdef HAVE_PROTOBUF
            std::string data;
            message->serialize(data);
            logger->queueData(data);
#else
            throw std::runtime_error("Protobuf support is required to use remoteLog");
#endif /* HAVE_PROTOBUF */

      });
}
