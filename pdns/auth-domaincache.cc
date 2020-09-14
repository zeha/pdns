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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "auth-domaincache.hh"
#include "logger.hh"
#include "statbag.hh"
#include "arguments.hh"
extern StatBag S;

AuthDomainCache::AuthDomainCache(size_t mapsCount): d_mapsCount(mapsCount)
{
  S.declare("domain-cache-hit", "Number of hits on the domain cache");
  S.declare("domain-cache-miss", "Number of misses on the domain cache");
  S.declare("domain-cache-size", "Number of entries in the domain cache", StatType::gauge);

  d_statnumhit=S.getPointer("domain-cache-hit");
  d_statnummiss=S.getPointer("domain-cache-miss");
  d_statnumentries=S.getPointer("domain-cache-size");

  d_ttl = 0;
}

AuthDomainCache::~AuthDomainCache()
{
  try {
    WriteLock l(d_mut);
  }
  catch(...) {
  }
}

bool AuthDomainCache::getEntry(const DNSName &domain, int& backendIndex)
{
  ReadLock rl(&d_mut);

  auto& mc = getMap(domain);
  auto iter = mc.d_map.find(domain);

  if (iter == mc.d_map.end()) {
    (*d_statnummiss)++;
    return false;
  } else {
    (*d_statnumhit)++;
    backendIndex = iter->second.backendIndex;
    return true;
  }
}

bool AuthDomainCache::isEnabled() const
{
  return d_ttl > 0;
}

void AuthDomainCache::clear()
{
  WriteLock l(&d_mut);
  vector<MapCombo> new_maps(d_mapsCount);
  d_maps = std::move(new_maps);
}

void AuthDomainCache::replace(const vector<tuple<DNSName, int>> &domain_indices)
{
  if(!d_ttl)
    return;

  size_t count = domain_indices.size();
  vector<MapCombo> new_maps(d_mapsCount);
  time_t now = time(nullptr);

  // TBD: check if we should reserve() maps

  // build new maps
  for(const tuple<DNSName, int>& tup: domain_indices) {
    const DNSName& domain = tup.get<0>();
    CacheValue val;
    val.backendIndex = tup.get<1>();
    auto& mc = new_maps[getMapIndex(domain)];
    mc.d_map.emplace(domain, val);
  }

  // replace all maps in one go
  {
    WriteLock l(&d_mut);
    d_maps = std::move(new_maps);
  }

  d_statnumentries->store(count);
  d_ttd = now + d_ttl;
}
