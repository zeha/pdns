#ifndef CDBIO_H
#define CDBIO_H

#include <errno.h>
#include <string>
#include <string.h>                     // strerror()
#include <fcntl.h>
#include <unistd.h>
#include <cdb.h>

#include "config.h"

#ifdef HAVE_NAMEDCACHE

class cdbIO {

public:

cdbIO(const std::string& strCdbName);
~cdbIO();
bool get(std::string strKey, std::string &strValue);
int  getErrNum();
std::string getErrMsg();

private:
  int fd;                               // cdb file descriptor
  struct cdb cdbX;                      // cdb file info
  std::string strErrMsg;                // cdb error message
  int iErrNum;                          // cdb error number
};

#endif

#endif // CDBIO_H
