#include "dnsdist-nc-nocache.hh"

#ifdef HAVE_NAMEDCACHE

// GCA - empty named cache place holder
NoCache::NoCache() {
  iDebug = 0;
}

NoCache::~NoCache() {
}

void NoCache::setDebug(int debug) {
  iDebug = debug;
}

bool NoCache::close() {
  return(true);
}

bool NoCache::open(std::string strFileName)
{
  return(true);
}

int NoCache::getErrNum()
{
  return(0);
}

std::string NoCache::getErrMsg()
{
  return("");
}

bool NoCache::init(int iEntries)
{
  (void) iEntries;
  return(true);
}

int NoCache::getSize()
{
  return(0);
}

int NoCache::getEntries()
{
  return(0);
}

int NoCache::getCache(const std::string strKey, std::string &strValue)
{
  int iStatus = CACHE_HIT::HIT_NONE;
  return(iStatus);
}

#endif
