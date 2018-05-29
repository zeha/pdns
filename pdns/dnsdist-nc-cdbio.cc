#include <stdexcept>

#include "dnsdist-nc-cdbio.hh"

#ifdef HAVE_NAMEDCACHE

// GCA - low level cdbio reading for named cache
// see:  http://cr.yp.to/cdb/reading.html

cdbIO::cdbIO(const std::string& fileName)
{
  iErrNum = 0;
  strErrMsg = "";

  fd = -1;
  fd = ::open(fileName.c_str(), O_RDONLY);             // open the CDB file
  if (fd == -1) {
    int err = errno;
    throw std::runtime_error("Opening CDB failed: " + fileName + " Error: " + strerror(err));
  }

  cdb_init(&cdbX, fd);                                 // initialize internal structure
}

cdbIO::~cdbIO()
{
  if (fd == -1)
    return;

  cdb_free(&cdbX);                                     // free internal structure
  if(::close(fd) == -1) {
    int err = errno;
    throw std::runtime_error("Closing CDB failed, Error: " + std::string(strerror(err)));
  }
}

// getCDB() - lookup key in cdb and set value if found
//            assume that there is only one entry for a key
//            return true if found
bool cdbIO::get(std::string strKey, std::string &strValue)
{
bool bStatus = false;
void *ptrVal;
uint32_t vpos;
unsigned int vlen;

  strValue.clear();
  iErrNum = 0;
  strErrMsg = "";

    int iFindRet = cdb_find(&cdbX, strKey.c_str(), strKey.length());
    if(iFindRet > 0) {                                   // search successeful
      vpos = cdb_datapos(&cdbX);                         // position of data in a file
      vlen = cdb_datalen(&cdbX);                         // length of data
      ptrVal = malloc(vlen);                             // allocate memory
      if(cdb_read(&cdbX, ptrVal, vlen, vpos) == -1) {    // read the value into buffer
        iErrNum = errno;
        strErrMsg = strerror(iErrNum);
      }
  	  strValue.append((const char *) ptrVal, vlen);      // handle the value here

      free(ptrVal);                                      // free up memory
      bStatus = true;
    } else {
        if(iFindRet == -1) {                             // search error
          iErrNum = errno;
          strErrMsg = strerror(iErrNum);
        }
      }

  return(bStatus);
}

int cdbIO::getErrNum()
{
  return(iErrNum);
}

std::string cdbIO::getErrMsg()
{
  return(strErrMsg);
}

#endif
