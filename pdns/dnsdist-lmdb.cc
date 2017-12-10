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
#include <thread>
#include "dolog.hh"
#include "base64.hh"
#include "lock.hh"
#include "gettime.hh"
#include <map>
#include <fstream>
#include <boost/logic/tribool.hpp>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "dnsdist-lmdb.hh"
#include <lmdb.h>

// XXX XXX XXX XXX XXX
// XXX compile with 'CXXFLAGS=-llmdb'
// XXX
// XXX caveats:
// XXX   * does not enforce LMDB "one open() per process" rule, WILL CRASH if you do not respect this.
// XXX   * lookup + action are intertwined. needs to become an LMDBRule instead.
// XXX   * cannot have wildcard and non-wildcard match of the same qname.
// XXX   * makes one LMDB TX per lookup; should we cache a TX per thread?
// XXX   * leaf is not fixed up for existing parents. maybe it should just die in a fire.
// XXX   * read data size is not checked.
// XXX XXX XXX XXX XXX
// Usage:
//     addAction(".", LMDBPolicyAction("db"))
// To build the db files, run (inside dnsdist -c /dev/null -l 127.0.0.1:5555):
//     buildLMDBPolicyActionDB("db", "dbinput.txt")
// cat dbinput.txt
// # comment
// Xexample.org.
// D*.namespace.at.
// Tdeduktiva.com.
// T*.zeha.at.


struct LMDBActionStorage {
  DNSAction::Action action;
  bool wildcard;
  bool leaf;
};

static string mdbstringerror(int rc) {
  return mdb_strerror(rc);
}

struct SimpleLMDBTX {
  MDB_txn *txn;
  MDB_dbi dbi;
  SimpleLMDBTX() : txn(nullptr), dbi(0) {};
};

class SimpleLMDBDatabase {
public:
  SimpleLMDBDatabase(string dbPath, int flags) {
    MDB_env *renv = nullptr;
    throwIf(mdb_env_create(&renv), "mdb_env_create");
    d_env = shared_ptr<MDB_env>(renv, mdb_env_close);

    // 4GB
    throwIf(mdb_env_set_mapsize(d_env.get(), 4294967296L), "mdb_env_set_mapsize");

    int rc = mdb_env_open(d_env.get(), dbPath.c_str(), MDB_NOSUBDIR|flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (rc)
      throw std::runtime_error("mdb_env_open for DB directory '" + dbPath + "' failed: " + mdbstringerror(rc));
  };

  void txn_begin(SimpleLMDBTX &tx, int flags) {
    throwIf(mdb_txn_begin(d_env.get(), nullptr, flags, &tx.txn), "mdb_txn_begin");

    int rc = mdb_dbi_open(tx.txn, NULL, MDB_CREATE, &tx.dbi);
    if (rc) {
      mdb_txn_abort(tx.txn);
      tx.txn = nullptr;
      throw std::runtime_error("mdb_dbi_open returned " + mdbstringerror(rc));
    }
  }

  void txn_commit(SimpleLMDBTX &tx) {
    throwIf(mdb_txn_commit(tx.txn), "mdb_txn_commit");
  }

  void txn_abort(SimpleLMDBTX &tx) {
    if (tx.txn != nullptr) {
      mdb_txn_abort(tx.txn);
    }
  }

  // clears the DB contents.
  void clear(SimpleLMDBTX &tx) {
    throwIf(mdb_drop(tx.txn, tx.dbi, 0), "mdb_drop");
  }

  bool get(SimpleLMDBTX &tx, string key, MDB_val* data) {
    MDB_val db_key { .mv_size = key.size(), .mv_data = (void*)key.c_str() };
    int rc = mdb_get(tx.txn, tx.dbi, &db_key, data);
    if (rc == 0) {
      // found
      return true;
    } else if (rc == MDB_NOTFOUND) {
      return false;
    }
    throw std::runtime_error("mdb_get returned " + mdbstringerror(rc));
  }

  void put(SimpleLMDBTX &tx, string key, MDB_val* data) {
    MDB_val db_key { .mv_size = key.size(), .mv_data = (void*)key.c_str() };
    throwIf(mdb_put(tx.txn, tx.dbi, &db_key, data, 0), "mdb_put");
  }

private:
  shared_ptr<MDB_env> d_env;

  void throwIf(int rc, const string& funcname) {
    if (rc != 0)
      throw std::runtime_error(funcname + " returned " + mdbstringerror(rc));
  }
};

void buildLMDBPolicyActionDB(string dbPath, string inputFile) {
  FILE *rfp = fopen(inputFile.c_str(), "r");
  if(!rfp)
    throw std::runtime_error("Error opening input file '"+inputFile+"': "+stringerror());

  shared_ptr<FILE> fp=shared_ptr<FILE>(rfp, fclose);

  SimpleLMDBDatabase db(dbPath, 0);
  SimpleLMDBTX tx;

  LMDBActionStorage parent;
  parent.wildcard = false;
  parent.leaf = false;
  parent.action = DNSAction::Action::None;
  MDB_val parent_val { .mv_size = sizeof(parent), .mv_data = &parent };

  try {
    db.txn_begin(tx, 0);
    db.clear(tx);

    string line;
    int linenum=0;

    while(linenum++, stringfgets(fp.get(), line)) {
      trim(line);
      if (line[0] == '#') // Comment line, skip to the next line
        continue;

      LMDBActionStorage stor;

      switch (line[0]) {
        case 'X':
          stor.action = DNSAction::Action::Nxdomain;
          break;
        case 'T':
          stor.action = DNSAction::Action::Truncate;
          break;
        case 'D':
          stor.action = DNSAction::Action::Drop;
          break;
        default:
          throw std::runtime_error("Unknown action '" + std::to_string(line[0]) + "' on line " + std::to_string(linenum));
      }

      DNSName qname = DNSName(line.substr(1));
      if(qname.empty())
        throw std::runtime_error("Domain cannot be empty, on line " + std::to_string(linenum));

      stor.wildcard = qname.isWildcard();
      if (stor.wildcard) {
        if (!qname.chopOff()) {
          throw std::runtime_error("Cannot use wildcard match on root domain, on line " + std::to_string(linenum));
        }
      }

      stor.leaf = true;

      DNSName storedname = qname.labelReverse().makeLowerCase();

      DNSName parentkey;
      MDB_val db_val;
      string skey;
      for (const auto& label : storedname.getRawLabels()) {
        parentkey.appendRawLabel(label);
        skey = parentkey.toDNSString();
        if (!db.get(tx, skey, &db_val)) {
          db.put(tx, skey, &parent_val);
        } else {
          // XXX compare db_data to be a valid parent data or kill leaf.
        }
      }

      db_val.mv_size = sizeof(stor);
      db_val.mv_data = &stor;
      db.put(tx, storedname.toDNSString(), &db_val);
    }

    db.txn_commit(tx);
  } catch (...) {
    db.txn_abort(tx);
    throw;
  }
}

template <class T>
void depthFind(SimpleLMDBDatabase* db, const DNSName& qname, DNSName& bestMatch, T& result) {
  SimpleLMDBTX tx;
  try {
    db->txn_begin(tx, MDB_RDONLY);

    DNSName storedname = qname.labelReverse().makeLowerCase();
    DNSName key;
    MDB_val db_data;

    for (const auto& label : storedname.getRawLabels()) {
      key.appendRawLabel(label);
      // cerr << "looking for " << key.toLogString() << endl;
      if (db->get(tx, key.toDNSString(), &db_data)) {
        bestMatch = key;
        // cerr << "GOT data with size " << std::to_string(db_data.mv_size) << endl;
        result = *reinterpret_cast<T*>(db_data.mv_data);
        // cerr << "INTERMEDIATE bestMatch=" << bestMatch.toLogString() << " leaf= " << (result.leaf ? "y" : "n") << endl;
        if (result.leaf) {
          break;
        }
      } else {
        break;
      }
    }
  } catch(...) {
    db->txn_abort(tx);
    throw;
  }
  db->txn_abort(tx);
}

string lookupLMDBPolicyAction(string dbPath, const DNSName& qname) {
  SimpleLMDBDatabase db(dbPath, MDB_RDONLY);
  string out;
  try {
    DNSName bestMatch;
    LMDBActionStorage result;
    depthFind(&db, qname, bestMatch, result);

    out = "qname=" + qname.toLogString() + " bestMatch=" + bestMatch.toLogString() + " wildcard=" + std::to_string(result.wildcard);
    // cerr << "qname= " << dq->qname->toLogString() << " found=" << found.toLogString() << " action= " << std::to_string((int)result.action) << " wildcard= " << (result.wildcard ? "y" : "n") << endl;
    if (!bestMatch.empty()) {
      if (result.wildcard || bestMatch == qname) {
        out += ": GOOD, taking action " + std::to_string((int)result.action);
      } else {
        out += ": not a wildcard";
      }
    } else {
      out += ": not found";
    }
  } catch(const std::exception& e) {
    errlog(e.what());
  }
  return out;
}

LMDBPolicyAction::LMDBPolicyAction(const std::string& dbpath): d_dbpath(dbpath), d_db(new SimpleLMDBDatabase(dbpath, MDB_RDONLY)) {
}

LMDBPolicyAction::~LMDBPolicyAction() {
  delete d_db;
}

DNSAction::Action LMDBPolicyAction::operator()(DNSQuestion* dq, string* ruleresult) const
{
  DNSAction::Action action = DNSAction::Action::None;
  try {
    DNSName bestMatch;
    LMDBActionStorage result;
    depthFind(d_db, *dq->qname, bestMatch, result);

    // cerr << "qname= " << dq->qname->toLogString() << " found=" << found.toLogString() << " action= " << std::to_string((int)result.action) << " wildcard= " << (result.wildcard ? "y" : "n") << endl;
    if (!bestMatch.empty() && (result.wildcard || bestMatch == *dq->qname)) {
      action = result.action;
    }
  } catch(const std::exception& e) {
    errlog(e.what());
  }
  return action;
}

string LMDBPolicyAction::toString() const
{
  return "lmdb policy action from " + d_dbpath;
}
