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
#include "dnsdist-namedcache.hh"

#include <map>
#include <fstream>
#include <boost/logic/tribool.hpp>
#include <boost/variant.hpp>
#include <sstream>

#include "dolog.hh"
#include "ednsoptions.hh"
#include "remote_logger.hh"


void setupLuaNamedCache(bool client)
{
#ifdef HAVE_NAMEDCACHE

/*

   NamedCache methods:
        getStats()         - get statistics in a table
                           - example:
                                 tableStat2 = ncx:getStats()
                                 tableStat2:getStats()
        showStats()        - directly print out statistics to terminal
                           - example:
                                 nc_rpz:showStats()
        resetCounters()    - reset statistic counters for the named cache
                           - example:
                                 nc_rpz:resetCounters()

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

    Debugging functions:
        getErrNum()        - return error message number (errno, from last i/o operation)
                           - example:
                                 print(nc_rpz:getErrNum())
        getErrMsg()        - return error message text
                           - example:
                                 print(nc_rpz:getErrMsg())
*/


  /* newNamedCache(cdbpath, maxEntries)
   *
   * Create a named cache and attach a CDB file.
   *
   * maxEntries specifies the maximum number of entries to cache in memory.
   * Only CDB hits are stored in memory cache.
   *
   * This method will return the named cache object.
   *
   * Examples:
   *
   * 	The most simple invocation, which will cache up to 100 000, successful
   * 	CDB lookups in memory:
   *
   * 		let nc = newNamedCache("path/to/my.cdb", 100000)
   *
   *  Cache up to 1 million CDB lookup hits:
   *
   *  	let nc = newNamedCache("path/to/my.cdb", 1000000)
   */

  g_lua.writeFunction("newNamedCache", [client](const std::string& filename, const int maxEntries) {
    setLuaSideEffect();
    if (client) {
      return std::shared_ptr<DNSDistNamedCache>(nullptr);
    }
    return std::make_shared<DNSDistNamedCache>(filename, maxEntries);
  });

  g_lua.registerFunction<void(std::shared_ptr<DNSDistNamedCache>::*)()>("close", [](const std::shared_ptr<DNSDistNamedCache> nc) {
    if (!nc) {
      g_outputBuffer = "nc:close: nc was nil";
      return;
    }

    nc->close();
  });

  g_lua.registerFunction<void(std::shared_ptr<DNSDistNamedCache>::*)()>("reopen", [](const std::shared_ptr<DNSDistNamedCache> nc) {
    if (!nc) {
      g_outputBuffer = "nc:reopen: nc was nil";
      return;
    }
    nc->reopen();
  });

  g_lua.registerFunction<std::unordered_map<string, string>(std::shared_ptr<DNSDistNamedCache>::*)()>("getStats", [](const std::shared_ptr<DNSDistNamedCache> nc) {
    std::unordered_map<string, string> tableResult;
    if (nc) {
      nc->getStatusTable(tableResult);
    }
    return(tableResult);
  });

  g_lua.registerFunction<void(std::shared_ptr<DNSDistNamedCache>::*)()>("showStats", [](const std::shared_ptr<DNSDistNamedCache> nc) {
    if (!nc) {
      g_outputBuffer = "Cannot show statistics for a nil named cache";
      return;
    }
    g_outputBuffer = nc->getStatusText();
  });

  g_lua.registerFunction("toString", &DNSDistNamedCache::toString);

  g_lua.registerFunction<void(std::shared_ptr<DNSDistNamedCache>::*)()>("resetCounters", [](const std::shared_ptr<DNSDistNamedCache> nc) {
    if (!nc) {
      return;
    }
    nc->resetCounters();
  });

  g_lua.registerFunction<int(std::shared_ptr<DNSDistNamedCache>::*)()>("getErrNum", [](const std::shared_ptr<DNSDistNamedCache> nc) {
    if (!nc) {
      return 0;
    }
    return(nc->getErrNum());
  });

  g_lua.registerFunction<std::string(std::shared_ptr<DNSDistNamedCache>::*)()>("getErrMsg", [](const std::shared_ptr<DNSDistNamedCache> nc) {
    if (!nc) {
      return std::string();
    }
    return nc->getErrMsg();
  });

  /* NamedCache::lookupQWild(DNSQuestion, minLabels)
   * A depth lookup is done starting from minLabels labels. If QName has less than minLabels labels, the entire QName is looked up once.
   *
   * Example of lookups done with minLabels=2 and QName foo.bar.foo.example.com.:
   *  example.com.
   *  foo.example.com.
   *  bar.foo.example.com.
   *  foo.bar.foo.example.com.
   */
  g_lua.registerFunction<std::unordered_map<string, boost::variant<string, bool> >(std::shared_ptr<DNSDistNamedCache>::*)(DNSQuestion *dq, int minLabels)>("lookupQWild", [](const std::shared_ptr<DNSDistNamedCache> nc, DNSQuestion *dq, int minLabels) {
    std::unordered_map<string, boost::variant<string, bool>> tableResult;
    if (!nc) {
      return tableResult;
    }

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
