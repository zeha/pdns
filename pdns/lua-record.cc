#include <thread>
#include <future>
#include <boost/format.hpp>
#include <boost/uuid/string_generator.hpp>
#include <utility>
#include <algorithm>
#include <random>
#include "qtype.hh"
#include <tuple>
#include "version.hh"
#include "ext/luawrapper/include/LuaContext.hpp"
#include "lock.hh"
#include "lua-auth4.hh"
#include "sstuff.hh"
#include "minicurl.hh"
#include "ueberbackend.hh"
#include "dns_random.hh"
#include "auth-main.hh"
#include "../modules/geoipbackend/geoipinterface.hh" // only for the enum

/* to do:
   block AXFR unless TSIG, or override

   investigate IPv6

   check the wildcard 'no cache' stuff, we may get it wrong

   ponder ECS scopemask setting

   ponder netmask tree from file for huge number of netmasks

   add attribute for certificate check in genericIfUp

   add list of current monitors
      expire them too?

   pool of UeberBackends?

   Pool checks ?
 */

extern int  g_luaRecordExecLimit;

using iplist_t = vector<pair<int, string> >;
using wiplist_t = std::unordered_map<int, string>;
using ipunitlist_t = vector<pair<int, iplist_t> >;
using opts_t = std::unordered_map<string,string>;

class IsUpOracle
{
private:
  struct CheckDesc
  {
    ComboAddress rem;
    string url;
    opts_t opts;
    bool operator<(const CheckDesc& rhs) const
    {
      std::map<string,string> oopts, rhsoopts;
      for(const auto& m : opts)
        oopts[m.first]=m.second;
      for(const auto& m : rhs.opts)
        rhsoopts[m.first]=m.second;

      return std::tuple(rem, url, oopts) <
        std::tuple(rhs.rem, rhs.url, rhsoopts);
    }
  };
  struct CheckState
  {
    CheckState(time_t _lastAccess): lastAccess(_lastAccess) {}
    /* current status */
    std::atomic<bool> status{false};
    /* current weight */
    std::atomic<int> weight{0};
    /* first check? */
    std::atomic<bool> first{true};
    /* number of successive checks returning failure */
    std::atomic<unsigned int> failures{0};
    /* last time the status was accessed */
    std::atomic<time_t> lastAccess{0};
    /* last time the status was modified */
    std::atomic<time_t> lastStatusUpdate{0};
  };

public:
  IsUpOracle()
  {
    d_checkerThreadStarted.clear();
  }
  ~IsUpOracle() = default;
  int isUp(const ComboAddress& remote, const opts_t& opts);
  int isUp(const ComboAddress& remote, const std::string& url, const opts_t& opts);
  //NOLINTNEXTLINE(readability-identifier-length)
  int isUp(const CheckDesc& cd);

private:
  void checkURL(const CheckDesc& cd, const bool status, const bool first) // NOLINT(readability-identifier-length)
  {
    setThreadName("pdns/lua-c-url");

    string remstring;
    try {
      int timeout = 2;
      if (cd.opts.count("timeout")) {
        timeout = std::atoi(cd.opts.at("timeout").c_str());
      }
      string useragent = productName();
      if (cd.opts.count("useragent")) {
        useragent = cd.opts.at("useragent");
      }
      size_t byteslimit = 0;
      if (cd.opts.count("byteslimit")) {
        byteslimit = static_cast<size_t>(std::atoi(cd.opts.at("byteslimit").c_str()));
      }
      MiniCurl mc(useragent);

      string content;
      const ComboAddress* rem = nullptr;
      if(cd.rem.sin4.sin_family != AF_UNSPEC) {
        rem = &cd.rem;
        remstring = rem->toString();
      } else {
        remstring = "[externally checked IP]";
      }

      if (cd.opts.count("source")) {
        ComboAddress src(cd.opts.at("source"));
        content=mc.getURL(cd.url, rem, &src, timeout, false, false, byteslimit);
      }
      else {
        content=mc.getURL(cd.url, rem, nullptr, timeout, false, false, byteslimit);
      }
      if (cd.opts.count("stringmatch") && content.find(cd.opts.at("stringmatch")) == string::npos) {
        throw std::runtime_error(boost::str(boost::format("unable to match content with `%s`") % cd.opts.at("stringmatch")));
      }

      int weight = 0;
      try {
        weight = stoi(content);
        if(!status) {
          g_log<<Logger::Info<<"Lua record monitoring declaring "<<remstring<<" UP for URL "<<cd.url<<"!"<<" with WEIGHT "<<content<<"!"<<endl;
        }
      }
      catch (const std::exception&) {
        if(!status) {
          g_log<<Logger::Info<<"Lua record monitoring declaring "<<remstring<<" UP for URL "<<cd.url<<"!"<<endl;
        }
      }

      setWeight(cd, weight);
      setUp(cd);
    }
    catch(std::exception& ne) {
      if(status || first)
        g_log<<Logger::Info<<"Lua record monitoring declaring "<<remstring<<" DOWN for URL "<<cd.url<<", error: "<<ne.what()<<endl;
      setWeight(cd, 0);
      setDown(cd);
    }
  }
  void checkTCP(const CheckDesc& cd, const bool status, const bool first) { // NOLINT(readability-identifier-length)
    setThreadName("pdns/lua-c-tcp");
    try {
      int timeout = 2;
      if (cd.opts.count("timeout")) {
        timeout = std::atoi(cd.opts.at("timeout").c_str());
      }
      Socket s(cd.rem.sin4.sin_family, SOCK_STREAM);
      ComboAddress src;
      s.setNonBlocking();
      if (cd.opts.count("source")) {
        src = ComboAddress(cd.opts.at("source"));
        s.bind(src);
      }
      s.connect(cd.rem, timeout);
      if (!status) {
        g_log<<Logger::Info<<"Lua record monitoring declaring TCP/IP "<<cd.rem.toStringWithPort()<<" ";
        if(cd.opts.count("source"))
          g_log<<"(source "<<src.toString()<<") ";
        g_log<<"UP!"<<endl;
      }
      setUp(cd);
    }
    catch (const NetworkError& ne) {
      if(status || first) {
        g_log<<Logger::Info<<"Lua record monitoring declaring TCP/IP "<<cd.rem.toStringWithPort()<<" DOWN: "<<ne.what()<<endl;
      }
      setDown(cd);
    }
  }
  void checkThread()
  {
    setThreadName("pdns/luaupcheck");
    while (true)
    {
      std::chrono::system_clock::time_point checkStart = std::chrono::system_clock::now();
      std::vector<std::future<void>> results;
      std::vector<CheckDesc> toDelete;
      time_t interval{g_luaHealthChecksInterval};
      {
        // make sure there's no insertion
        auto statuses = d_statuses.read_lock();
        for (auto& it: *statuses) {
          auto& desc = it.first;
          auto& state = it.second;
          time_t checkInterval{0};
          auto lastAccess = std::chrono::system_clock::from_time_t(state->lastAccess);

          if (desc.opts.count("interval") != 0) {
            checkInterval = std::atoi(desc.opts.at("interval").c_str());
            if (checkInterval != 0) {
              interval = std::gcd(interval, checkInterval);
            }
          }

          if (not state->first) {
            time_t nextCheckSecond = state->lastStatusUpdate;
            if (checkInterval != 0) {
               nextCheckSecond += checkInterval;
            }
            else {
               nextCheckSecond += g_luaHealthChecksInterval;
            }
            if (checkStart < std::chrono::system_clock::from_time_t(nextCheckSecond)) {
              continue; // too early
            }
          }

          if (desc.url.empty()) { // TCP
            results.push_back(std::async(std::launch::async, &IsUpOracle::checkTCP, this, desc, state->status.load(), state->first.load()));
          } else { // URL
            results.push_back(std::async(std::launch::async, &IsUpOracle::checkURL, this, desc, state->status.load(), state->first.load()));
          }
          // Give it a chance to run at least once.
          // If minimumFailures * interval > lua-health-checks-expire-delay, then a down status will never get reported.
          // This is unlikely to be a problem in practice due to the default value of the expire delay being one hour.
          if (not state->first &&
              lastAccess < (checkStart - std::chrono::seconds(g_luaHealthChecksExpireDelay))) {
            toDelete.push_back(desc);
          }
        }
      }
      // we can release the lock as nothing will be deleted
      for (auto& future: results) {
        future.wait();
      }
      if (!toDelete.empty()) {
        auto statuses = d_statuses.write_lock();
        for (auto& it: toDelete) {
          statuses->erase(it);
        }
      }

      // set thread name again, in case std::async surprised us by doing work in this thread
      setThreadName("pdns/luaupcheck");

      std::this_thread::sleep_until(checkStart + std::chrono::seconds(interval));
    }
  }

  typedef map<CheckDesc, std::unique_ptr<CheckState>> statuses_t;
  SharedLockGuarded<statuses_t> d_statuses;

  std::unique_ptr<std::thread> d_checkerThread;
  std::atomic_flag d_checkerThreadStarted;

  void setStatus(const CheckDesc& cd, bool status)
  {
    auto statuses = d_statuses.write_lock();
    auto& state = (*statuses)[cd];
    state->lastStatusUpdate = time(nullptr);
    state->first = false;
    if (status) {
      state->failures = 0;
      state->status = true;
    } else {
      unsigned int minimumFailures = 1;
      if (cd.opts.count("minimumFailures") != 0) {
        unsigned int value = std::atoi(cd.opts.at("minimumFailures").c_str());
        if (value != 0) {
          minimumFailures = std::max(minimumFailures, value);
        }
      }
      // Since `status' was set to false at constructor time, we need to
      // recompute its value unconditionally to expose "down, but not enough
      // times yet" targets as up.
      state->status = ++state->failures < minimumFailures;
    }
  }

  //NOLINTNEXTLINE(readability-identifier-length)
  void setWeight(const CheckDesc& cd, int weight){
    auto statuses = d_statuses.write_lock();
    auto& state = (*statuses)[cd];
    state->weight = weight;
  }

  void setDown(const ComboAddress& rem, const std::string& url=std::string(), const opts_t& opts = opts_t())
  {
    //NOLINTNEXTLINE(readability-identifier-length)
    CheckDesc cd{rem, url, opts};
    setStatus(cd, false);
  }

  void setUp(const ComboAddress& rem, const std::string& url=std::string(), const opts_t& opts = opts_t())
  {
    CheckDesc cd{rem, url, opts};

    setStatus(cd, true);
  }

  void setDown(const CheckDesc& cd)
  {
    setStatus(cd, false);
  }

  void setUp(const CheckDesc& cd)
  {
    setStatus(cd, true);
  }
};

//NOLINTNEXTLINE(readability-identifier-length)
int IsUpOracle::isUp(const CheckDesc& cd)
{
  if (!d_checkerThreadStarted.test_and_set()) {
    d_checkerThread = std::make_unique<std::thread>([this] { return checkThread(); });
  }
  time_t now = time(nullptr);
  {
    auto statuses = d_statuses.read_lock();
    auto iter = statuses->find(cd);
    if (iter != statuses->end()) {
      iter->second->lastAccess = now;
      if (iter->second->weight > 0) {
        return iter->second->weight;
      }
      return static_cast<int>(iter->second->status);
    }
  }
  // try to parse options so we don't insert any malformed content
  if (cd.opts.count("source")) {
    ComboAddress src(cd.opts.at("source"));
  }
  {
    auto statuses = d_statuses.write_lock();
    // Make sure we don't insert new entry twice now we have the lock
    if (statuses->find(cd) == statuses->end()) {
      (*statuses)[cd] = std::make_unique<CheckState>(now);
    }
  }
  return 0;
}

int IsUpOracle::isUp(const ComboAddress& remote, const opts_t& opts)
{
  CheckDesc cd{remote, "", opts};
  return isUp(cd);
}

int IsUpOracle::isUp(const ComboAddress& remote, const std::string& url, const opts_t& opts)
{
  CheckDesc cd{remote, url, opts};
  return isUp(cd);
}

IsUpOracle g_up;
namespace {
template<typename T, typename C>
bool doCompare(const T& var, const std::string& res, const C& cmp)
{
  if(auto country = boost::get<string>(&var))
    return cmp(*country, res);

  auto countries=boost::get<vector<pair<int,string> > >(&var);
  for(const auto& country : *countries) {
    if(cmp(country.second, res))
      return true;
  }
  return false;
}
}

static std::string getGeo(const std::string& ip, GeoIPInterface::GeoIPQueryAttribute qa)
{
  static bool initialized;
  extern std::function<std::string(const std::string& ip, int)> g_getGeo;
  if(!g_getGeo) {
    if(!initialized) {
      g_log<<Logger::Error<<"Lua record attempted to use GeoIPBackend functionality, but backend not launched"<<endl;
      initialized=true;
    }
    return "unknown";
  }
  else
    return g_getGeo(ip, (int)qa);
}

template <typename T>
static T pickRandom(const vector<T>& items)
{
  if (items.empty()) {
    throw std::invalid_argument("The items list cannot be empty");
  }
  return items[dns_random(items.size())];
}

template <typename T>
static T pickHashed(const ComboAddress& who, const vector<T>& items)
{
  if (items.empty()) {
    throw std::invalid_argument("The items list cannot be empty");
  }
  ComboAddress::addressOnlyHash aoh;
  return items[aoh(who) % items.size()];
}

template <typename T>
static T pickWeightedRandom(const vector< pair<int, T> >& items)
{
  if (items.empty()) {
    throw std::invalid_argument("The items list cannot be empty");
  }
  int sum=0;
  vector< pair<int, T> > pick;
  pick.reserve(items.size());

  for(auto& i : items) {
    sum += i.first;
    pick.emplace_back(sum, i.second);
  }

  if (sum == 0) {
    throw std::invalid_argument("The sum of items cannot be zero");
  }

  int r = dns_random(sum);
  auto p = upper_bound(pick.begin(), pick.end(), r, [](int rarg, const typename decltype(pick)::value_type& a) { return rarg < a.first; });
  return p->second;
}

template <typename T>
static T pickWeightedHashed(const ComboAddress& bestwho, const vector< pair<int, T> >& items)
{
  if (items.empty()) {
    throw std::invalid_argument("The items list cannot be empty");
  }
  int sum=0;
  vector< pair<int, T> > pick;
  pick.reserve(items.size());

  for(auto& i : items) {
    sum += i.first;
    pick.push_back({sum, i.second});
  }

  if (sum == 0) {
    throw std::invalid_argument("The sum of items cannot be zero");
  }

  ComboAddress::addressOnlyHash aoh;
  int r = aoh(bestwho) % sum;
  auto p = upper_bound(pick.begin(), pick.end(), r, [](int rarg, const typename decltype(pick)::value_type& a) { return rarg < a.first; });
  return p->second;
}

template <typename T>
static T pickWeightedNameHashed(const DNSName& dnsname, vector< pair<int, T> >& items)
{
  if (items.empty()) {
    throw std::invalid_argument("The items list cannot be empty");
  }
  size_t sum=0;
  vector< pair<int, T> > pick;
  pick.reserve(items.size());

  for(auto& i : items) {
    sum += i.first;
    pick.push_back({sum, i.second});
  }

  if (sum == 0) {
    throw std::invalid_argument("The sum of items cannot be zero");
  }

  size_t r = dnsname.hash() % sum;
  auto p = upper_bound(pick.begin(), pick.end(), r, [](int rarg, const typename decltype(pick)::value_type& a) { return rarg < a.first; });
  return p->second;
}

template <typename T>
static vector<T> pickRandomSample(int n, const vector<T>& items)
{
  if (items.empty()) {
    throw std::invalid_argument("The items list cannot be empty");
  }

  vector<T> pick;
  pick.reserve(items.size());

  for(auto& item : items) {
    pick.push_back(item);
  }

  int count = std::min(std::max<size_t>(0, n), items.size());

  if (count == 0) {
    return vector<T>();
  }

  std::shuffle(pick.begin(), pick.end(), pdns::dns_random_engine());

  vector<T> result = {pick.begin(), pick.begin() + count};
  return result;
}

static bool getLatLon(const std::string& ip, double& lat, double& lon)
{
  string inp = getGeo(ip, GeoIPInterface::Location);
  if(inp.empty())
    return false;
  lat=atof(inp.c_str());
  auto pos=inp.find(' ');
  if(pos != string::npos)
    lon=atof(inp.c_str() + pos);
  return true;
}

static bool getLatLon(const std::string& ip, string& loc)
{
  int latdeg, latmin, londeg, lonmin;
  double latsec, lonsec;
  char lathem='X', lonhem='X';

  double lat = 0, lon = 0;
  if(!getLatLon(ip, lat, lon))
    return false;

  if(lat > 0) {
    lathem='N';
  }
  else {
    lat = -lat;
    lathem='S';
  }

  if(lon > 0) {
    lonhem='E';
  }
  else {
    lon = -lon;
    lonhem='W';
  }

  latdeg = lat;
  latmin = (lat - latdeg)*60.0;
  latsec = (((lat - latdeg)*60.0) - latmin)*60.0;

  londeg = lon;
  lonmin = (lon - londeg)*60.0;
  lonsec = (((lon - londeg)*60.0) - lonmin)*60.0;

  // 51 59 00.000 N 5 55 00.000 E 4.00m 1.00m 10000.00m 10.00m

  boost::format fmt("%d %d %d %c %d %d %d %c 0.00m 1.00m 10000.00m 10.00m");

  loc= (fmt % latdeg % latmin % latsec % lathem % londeg % lonmin % lonsec % lonhem ).str();
  return true;
}

static ComboAddress pickclosest(const ComboAddress& bestwho, const vector<ComboAddress>& wips)
{
  if (wips.empty()) {
    throw std::invalid_argument("The IP list cannot be empty");
  }
  map<double, vector<ComboAddress> > ranked;
  double wlat=0, wlon=0;
  getLatLon(bestwho.toString(), wlat, wlon);
  //        cout<<"bestwho "<<wlat<<", "<<wlon<<endl;
  vector<string> ret;
  for(const auto& c : wips) {
    double lat=0, lon=0;
    getLatLon(c.toString(), lat, lon);
    //          cout<<c.toString()<<": "<<lat<<", "<<lon<<endl;
    double latdiff = wlat-lat;
    double londiff = wlon-lon;
    if(londiff > 180)
      londiff = 360 - londiff;
    double dist2=latdiff*latdiff + londiff*londiff;
    //          cout<<"    distance: "<<sqrt(dist2) * 40000.0/360<<" km"<<endl; // length of a degree
    ranked[dist2].push_back(c);
  }
  return ranked.begin()->second[dns_random(ranked.begin()->second.size())];
}

static std::vector<DNSZoneRecord> lookup(const DNSName& name, uint16_t qtype, int zoneid)
{
  static LockGuarded<UeberBackend> s_ub;

  DNSZoneRecord dr;
  vector<DNSZoneRecord> ret;
  {
    auto ub = s_ub.lock();
    ub->lookup(QType(qtype), name, zoneid);
    while (ub->get(dr)) {
      ret.push_back(dr);
    }
  }
  return ret;
}

static bool getAuth(const DNSName& name, uint16_t qtype, SOAData* soaData)
{
  static LockGuarded<UeberBackend> s_ub;

  {
    auto ueback = s_ub.lock();
    return ueback->getAuth(name, qtype, soaData);
  }
}

static std::string getOptionValue(const boost::optional<std::unordered_map<string, string>>& options, const std::string &name, const std::string &defaultValue)
{
  string selector=defaultValue;
  if(options) {
    if(options->count(name))
      selector=options->find(name)->second;
  }
  return selector;
}

static vector<ComboAddress> useSelector(const std::string &selector, const ComboAddress& bestwho, const vector<ComboAddress>& candidates)
{
  vector<ComboAddress> ret;

  if(selector=="all")
    return candidates;
  else if(selector=="random")
    ret.emplace_back(pickRandom<ComboAddress>(candidates));
  else if(selector=="pickclosest")
    ret.emplace_back(pickclosest(bestwho, candidates));
  else if(selector=="hashed")
    ret.emplace_back(pickHashed<ComboAddress>(bestwho, candidates));
  else {
    g_log<<Logger::Warning<<"Lua record called with unknown selector '"<<selector<<"'"<<endl;
    ret.emplace_back(pickRandom<ComboAddress>(candidates));
  }

  return ret;
}

static vector<string> convComboAddressListToString(const vector<ComboAddress>& items)
{
  vector<string> result;
  result.reserve(items.size());

  for (const auto& item : items) {
    result.emplace_back(item.toString());
  }

  return result;
}

static vector<ComboAddress> convComboAddressList(const iplist_t& items, uint16_t port=0)
{
  vector<ComboAddress> result;
  result.reserve(items.size());

  for(const auto& item : items) {
    result.emplace_back(ComboAddress(item.second, port));
  }

  return result;
}

/**
 * Reads and unify single or multiple sets of ips :
 * - {'192.0.2.1', '192.0.2.2'}
 * - {{'192.0.2.1', '192.0.2.2'}, {'198.51.100.1'}}
 */

static vector<vector<ComboAddress>> convMultiComboAddressList(const boost::variant<iplist_t, ipunitlist_t>& items, uint16_t port = 0)
{
  vector<vector<ComboAddress>> candidates;

  if(auto simple = boost::get<iplist_t>(&items)) {
    vector<ComboAddress> unit = convComboAddressList(*simple, port);
    candidates.push_back(unit);
  } else {
    auto units = boost::get<ipunitlist_t>(items);
    for(const auto& u : units) {
      vector<ComboAddress> unit = convComboAddressList(u.second, port);
      candidates.push_back(unit);
    }
  }
  return candidates;
}

static vector<string> convStringList(const iplist_t& items)
{
  vector<string> result;
  result.reserve(items.size());

  for(const auto& item : items) {
    result.emplace_back(item.second);
  }

  return result;
}

static vector< pair<int, string> > convIntStringPairList(const std::unordered_map<int, wiplist_t >& items)
{
  vector<pair<int,string> > result;
  result.reserve(items.size());

  for(const auto& item : items) {
    result.emplace_back(atoi(item.second.at(1).c_str()), item.second.at(2));
  }

  return result;
}

bool g_LuaRecordSharedState;

typedef struct AuthLuaRecordContext
{
  ComboAddress          bestwho;
  DNSName               qname;
  DNSZoneRecord         zone_record;
  DNSName               zone;
} lua_record_ctx_t;

static thread_local unique_ptr<lua_record_ctx_t> s_lua_record_ctx;

/*
 *  Holds computed hashes for a given entry
 */
struct EntryHashesHolder
{
  std::atomic<size_t> weight;
  std::string entry;
  SharedLockGuarded<std::vector<unsigned int>> hashes;
  std::atomic<time_t> lastUsed;

  EntryHashesHolder(size_t weight_, std::string entry_, time_t lastUsed_ = time(nullptr)): weight(weight_), entry(std::move(entry_)), lastUsed(lastUsed_) {
  }

  bool hashesComputed() {
    return weight == hashes.read_lock()->size();
  }
  void hash() {
    auto locked = hashes.write_lock();
    locked->clear();
    locked->reserve(weight);
    size_t count = 0;
    while (count < weight) {
      auto value = boost::str(boost::format("%s-%d") % entry % count);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      auto whash = burtle(reinterpret_cast<const unsigned char*>(value.data()), value.size(), 0);
      locked->push_back(whash);
      ++count;
    }
    std::sort(locked->begin(), locked->end());
  }
};

using zone_hashes_key_t = std::tuple<int, std::string, std::string>;

static SharedLockGuarded<std::map<
  zone_hashes_key_t, // zoneid qname entry
  std::shared_ptr<EntryHashesHolder> // entry w/ corresponding hashes
  >>
s_zone_hashes;

static std::atomic<time_t> s_lastConsistentHashesCleanup = 0;

/**
 * every ~g_luaConsistentHashesCleanupInterval, do a cleanup to delete entries that haven't been used in the last g_luaConsistentHashesExpireDelay
 */
static void cleanZoneHashes()
{
  auto now = time(nullptr);
  if (s_lastConsistentHashesCleanup > (now - g_luaConsistentHashesCleanupInterval)) {
    return ;
  }
  s_lastConsistentHashesCleanup = now;
  std::vector<zone_hashes_key_t> toDelete{};
  {
    auto locked = s_zone_hashes.read_lock();
    auto someTimeAgo = now - g_luaConsistentHashesExpireDelay;

    for (const auto& [key, entry]: *locked) {
      if (entry->lastUsed > someTimeAgo) {
        toDelete.push_back(key);
      }
    }
  }
  if (!toDelete.empty()) {
    auto wlocked = s_zone_hashes.write_lock();
    for (const auto& key : toDelete) {
      wlocked->erase(key);
    }
  }
}

static std::vector<std::shared_ptr<EntryHashesHolder>> getCHashedEntries(const int zoneId, const std::string& queryName, const std::vector<std::pair<int, std::string>>& items)
{
  std::vector<std::shared_ptr<EntryHashesHolder>> result{};
  std::map<zone_hashes_key_t, std::shared_ptr<EntryHashesHolder>> newEntries{};

  {
    time_t now = time(nullptr);
    auto locked = s_zone_hashes.read_lock();

    for (const auto& [weight, entry]: items) {
      auto key = std::make_tuple(zoneId, queryName, entry);
      if (locked->count(key) == 0) {
        newEntries[key] = std::make_shared<EntryHashesHolder>(weight, entry, now);
      } else {
        locked->at(key)->weight = weight;
        locked->at(key)->lastUsed = now;
        result.push_back(locked->at(key));
      }
    }
  }
  if (!newEntries.empty()) {
    auto wlocked = s_zone_hashes.write_lock();

    for (auto& [key, entry]: newEntries) {
      result.push_back(entry);
      (*wlocked)[key] = std::move(entry);
    }
  }

  return result;
}

static std::string pickConsistentWeightedHashed(const ComboAddress& bestwho, const std::vector<std::pair<int, std::string>>& items)
{
  const auto& zoneId = s_lua_record_ctx->zone_record.domain_id;
  const auto queryName = s_lua_record_ctx->qname.toString();
  unsigned int sel = std::numeric_limits<unsigned int>::max();
  unsigned int min = std::numeric_limits<unsigned int>::max();

  boost::optional<std::string> ret;
  boost::optional<std::string> first;

  cleanZoneHashes();

  auto entries = getCHashedEntries(zoneId, queryName, items);

  ComboAddress::addressOnlyHash addrOnlyHash;
  auto qhash = addrOnlyHash(bestwho);
  for (const auto& entry : entries) {
    if (!entry->hashesComputed()) {
      entry->hash();
    }
    {
      const auto hashes = entry->hashes.read_lock();
      if (!hashes->empty()) {
        if (min > *(hashes->begin())) {
          min = *(hashes->begin());
          first = entry->entry;
        }

        auto hash_it = std::lower_bound(hashes->begin(), hashes->end(), qhash);
        if (hash_it != hashes->end()) {
          if (*hash_it < sel) {
            sel = *hash_it;
            ret = entry->entry;
          }
        }
      }
    }
  }
  if (ret != boost::none) {
    return *ret;
  }
  if (first != boost::none) {
    return *first;
  }
  return {};
}

static vector<string> genericIfUp(const boost::variant<iplist_t, ipunitlist_t>& ips, boost::optional<opts_t> options, const std::function<bool(const ComboAddress&, const opts_t&)>& upcheckf, uint16_t port = 0)
{
  vector<vector<ComboAddress> > candidates;
  opts_t opts;
  if(options)
    opts = *options;

  candidates = convMultiComboAddressList(ips, port);

  for(const auto& unit : candidates) {
    vector<ComboAddress> available;
    for(const auto& c : unit) {
      if (upcheckf(c, opts)) {
        available.push_back(c);
      }
    }
    if(!available.empty()) {
      vector<ComboAddress> res = useSelector(getOptionValue(options, "selector", "random"), s_lua_record_ctx->bestwho, available);
      return convComboAddressListToString(res);
    }
  }

  // All units down, apply backupSelector on all candidates
  vector<ComboAddress> ret{};
  for(const auto& unit : candidates) {
    ret.insert(ret.end(), unit.begin(), unit.end());
  }

  vector<ComboAddress> res = useSelector(getOptionValue(options, "backupSelector", "random"), s_lua_record_ctx->bestwho, ret);
  return convComboAddressListToString(res);
}

static void setupLuaRecords(LuaContext& lua) // NOLINT(readability-function-cognitive-complexity)
{
  lua.writeFunction("latlon", []() {
      double lat = 0, lon = 0;
      getLatLon(s_lua_record_ctx->bestwho.toString(), lat, lon);
      return std::to_string(lat)+" "+std::to_string(lon);
    });
  lua.writeFunction("latlonloc", []() {
      string loc;
      getLatLon(s_lua_record_ctx->bestwho.toString(), loc);
      return loc;
  });
  lua.writeFunction("closestMagic", []() {
      vector<ComboAddress> candidates;
      // Getting something like 192-0-2-1.192-0-2-2.198-51-100-1.example.org
      for(auto l : s_lua_record_ctx->qname.getRawLabels()) {
        std::replace(l.begin(), l.end(), '-', '.');
        try {
          candidates.emplace_back(l);
        } catch (const PDNSException& e) {
          // no need to continue as we most likely reached the end of the ip list
          break ;
        }
      }
      return pickclosest(s_lua_record_ctx->bestwho, candidates).toString();
    });
  lua.writeFunction("latlonMagic", [](){
      auto labels= s_lua_record_ctx->qname.getRawLabels();
      if(labels.size()<4)
        return std::string("unknown");
      double lat = 0, lon = 0;
      getLatLon(labels[3]+"."+labels[2]+"."+labels[1]+"."+labels[0], lat, lon);
      return std::to_string(lat)+" "+std::to_string(lon);
    });


  lua.writeFunction("createReverse", [](string format, boost::optional<std::unordered_map<string,string>> e){
      try {
        auto labels = s_lua_record_ctx->qname.getRawLabels();
        if(labels.size()<4)
          return std::string("unknown");

        vector<ComboAddress> candidates;

        // so, query comes in for 4.3.2.1.in-addr.arpa, zone is called 2.1.in-addr.arpa
        // e["1.2.3.4"]="bert.powerdns.com" then provides an exception
        if(e) {
          ComboAddress req(labels[3]+"."+labels[2]+"."+labels[1]+"."+labels[0], 0);
          const auto& uom = *e;
          for(const auto& c : uom)
            if(ComboAddress(c.first, 0) == req)
              return c.second;
        }
        boost::format fmt(format);
        fmt.exceptions( boost::io::all_error_bits ^ ( boost::io::too_many_args_bit | boost::io::too_few_args_bit )  );
        fmt % labels[3] % labels[2] % labels[1] % labels[0];

        fmt % (labels[3]+"-"+labels[2]+"-"+labels[1]+"-"+labels[0]);

        boost::format fmt2("%02x%02x%02x%02x");
        for(int i=3; i>=0; --i)
          fmt2 % atoi(labels[i].c_str());

        fmt % (fmt2.str());

        return fmt.str();
      }
      catch(std::exception& ex) {
        g_log<<Logger::Error<<"error: "<<ex.what()<<endl;
      }
      return std::string("error");
    });
  lua.writeFunction("createForward", []() {
      static string allZerosIP{"0.0.0.0"};
      try {
        DNSName record_name{s_lua_record_ctx->zone_record.dr.d_name};
        if (!record_name.isWildcard()) {
          return allZerosIP;
        }
        record_name.chopOff();
        DNSName rel{s_lua_record_ctx->qname.makeRelative(record_name)};

        // parts is something like ["1", "2", "3", "4", "static"] or
        // ["1", "2", "3", "4"] or ["ip40414243", "ip-addresses", ...]
        auto parts = rel.getRawLabels();
        // Yes, this still breaks if an 1-2-3-4.XXXX is nested too deeply...
        if (parts.size() >= 4) {
          ComboAddress address(parts[0]+"."+parts[1]+"."+parts[2]+"."+parts[3]);
          return address.toString();
        }
	if (!parts.empty()) {
          auto& input = parts.at(0);

          // allow a word without - in front, as long as it does not contain anything that could be a number
          size_t nonhexprefix = strcspn(input.c_str(), "0123456789abcdefABCDEF");
          if (nonhexprefix > 0) {
            input = input.substr(nonhexprefix);
          }

          // either hex string, or 12-13-14-15
          vector<string> ip_parts;

          stringtok(ip_parts, input, "-");
          if (ip_parts.size() >= 4) {
            // 1-2-3-4 with any prefix (e.g. ip-foo-bar-1-2-3-4)
            string ret;
            for (size_t index=4; index > 0; index--) {
              auto octet = ip_parts.at(ip_parts.size() - index);
              auto octetVal = std::stol(octet); // may throw
              if (octetVal >= 0 && octetVal <= 255) {
                ret += octet + ".";
              } else {
                return allZerosIP;
              }
            }
            ret.resize(ret.size() - 1); // remove trailing dot after last octet
            return ret;
          }
          if (input.length() >= 8) {
            auto last8 = input.substr(input.length()-8);
            unsigned int part1{0};
            unsigned int part2{0};
            unsigned int part3{0};
            unsigned int part4{0};
            if (sscanf(last8.c_str(), "%02x%02x%02x%02x", &part1, &part2, &part3, &part4) == 4) {
              ComboAddress address(std::to_string(part1) + "." + std::to_string(part2) + "." + std::to_string(part3) + "." + std::to_string(part4));
              return address.toString();
            }
          }
        }
        return allZerosIP;
      } catch (const PDNSException &e) {
        return allZerosIP;
      }
    });

  lua.writeFunction("createForward6", []() {
      static string allZerosIP{"::"};
      try {
        DNSName record_name{s_lua_record_ctx->zone_record.dr.d_name};
        if (!record_name.isWildcard()) {
          return allZerosIP;
        }
        record_name.chopOff();
        DNSName rel{s_lua_record_ctx->qname.makeRelative(record_name)};

        auto parts = rel.getRawLabels();
        if (parts.size() == 8) {
          string tot;
          for (int chunk = 0; chunk < 8; ++chunk) {
            if (chunk != 0) {
              tot.append(1, ':');
	      }
            tot += parts.at(chunk);
          }
          ComboAddress address(tot);
          return address.toString();
        }
        if (parts.size() == 1) {
          if (parts[0].find('-') != std::string::npos) {
            std::replace(parts[0].begin(), parts[0].end(), '-', ':');
            ComboAddress address(parts[0]);
            return address.toString();
          }
          if (parts[0].size() >= 32) {
            auto ippart = parts[0].substr(parts[0].size()-32);
            auto fulladdress =
              ippart.substr(0, 4) + ":" +
              ippart.substr(4, 4) + ":" +
              ippart.substr(8, 4) + ":" +
              ippart.substr(12, 4) + ":" +
              ippart.substr(16, 4) + ":" +
              ippart.substr(20, 4) + ":" +
              ippart.substr(24, 4) + ":" +
              ippart.substr(28, 4);

            ComboAddress address(fulladdress);
            return address.toString();
          }
        }
        return allZerosIP;
      } catch (const PDNSException &e) {
        return allZerosIP;
      }
    });
  lua.writeFunction("createReverse6", [](const string &format, boost::optional<std::unordered_map<string,string>> excp){
      vector<ComboAddress> candidates;

      try {
        auto labels= s_lua_record_ctx->qname.getRawLabels();
        if (labels.size()<32) {
          return std::string("unknown");
	}
        boost::format fmt(format);
        fmt.exceptions( boost::io::all_error_bits ^ ( boost::io::too_many_args_bit | boost::io::too_few_args_bit )  );


        string together;
        vector<string> quads;
        for (int chunk = 0; chunk < 8; ++chunk) {
          if (chunk != 0) {
            together += ":";
	  }
          string lquad;
          for (int quartet = 0; quartet < 4; ++quartet) {
            lquad.append(1, labels[31 - chunk * 4 - quartet][0]);
            together += labels[31 - chunk * 4 - quartet][0];
          }
          quads.push_back(lquad);
        }
	ComboAddress ip6(together,0);

	if (excp) {
          auto& addrs=*excp;
          for(const auto& addr: addrs) {
            // this makes sure we catch all forms of the address
            if (ComboAddress(addr.first, 0) == ip6) {
              return addr.second;
	    }
          }
        }

        string dashed=ip6.toString();
        std::replace(dashed.begin(), dashed.end(), ':', '-');

        // https://github.com/PowerDNS/pdns/issues/7524
        if (boost::ends_with(dashed, "-")) {
          // "a--a-" -> "a--a-0"
          dashed.push_back('0');
        }
        if (boost::starts_with(dashed, "-") || dashed.compare(2, 2, "--") == 0) {
          // "-a--a" -> "0-a--a"               "aa--a" -> "0aa--a"
          dashed.insert(0, "0");
        }

        for (int byte = 31; byte >= 0; --byte) {
          fmt % labels[byte];
	}
        fmt % dashed;

        for(const auto& lquad : quads) {
          fmt % lquad;
	}

        return fmt.str();
      }
      catch(std::exception& ex) {
        g_log<<Logger::Error<<"Lua record exception: "<<ex.what()<<endl;
      }
      catch(PDNSException& ex) {
        g_log<<Logger::Error<<"Lua record exception: "<<ex.reason<<endl;
      }
      return std::string("unknown");
    });

  lua.writeFunction("filterForward", [](const string& address, NetmaskGroup& nmg, boost::optional<string> fallback) -> vector<string> {
      ComboAddress ca(address);

      if (nmg.match(ComboAddress(address))) {
        return {address};
      } else {
        if (fallback) {
          if (fallback->empty()) {
            // if fallback is an empty string, return an empty array
            return {};
          }
          return {*fallback};
        }

        if (ca.isIPv4()) {
          return {string("0.0.0.0")};
        } else {
          return {string("::")};
        }
      }
    });

  /*
   * Simplistic test to see if an IP address listens on a certain port
   * Will return a single IP address from the set of available IP addresses. If
   * no IP address is available, will return a random element of the set of
   * addresses supplied for testing.
   *
   * @example ifportup(443, { '1.2.3.4', '5.4.3.2' })"
   */
  lua.writeFunction("ifportup", [](int port, const boost::variant<iplist_t, ipunitlist_t>& ips, const boost::optional<std::unordered_map<string,string>> options) {
    port = std::max(port, 0);
    port = std::min(port, static_cast<int>(std::numeric_limits<uint16_t>::max()));

    auto checker = [](const ComboAddress& addr, const opts_t& opts) {
      return g_up.isUp(addr, opts);
    };
    return genericIfUp(ips, options, checker, port);
  });

  lua.writeFunction("ifurlextup", [](const vector<pair<int, opts_t> >& ipurls, boost::optional<opts_t> options) {
      vector<ComboAddress> candidates;
      opts_t opts;
      if(options)
        opts = *options;

      ComboAddress ca_unspec;
      ca_unspec.sin4.sin_family=AF_UNSPEC;

      // ipurls: { { ["192.0.2.1"] = "https://example.com", ["192.0.2.2"] = "https://example.com/404" } }
      for (const auto& [count, unitmap] : ipurls) {
        // unitmap: 1 = { ["192.0.2.1"] = "https://example.com", ["192.0.2.2"] = "https://example.com/404" }
        vector<ComboAddress> available;

        for (const auto& [ipStr, url] : unitmap) {
          // unit: ["192.0.2.1"] = "https://example.com"
          ComboAddress ip(ipStr);
          candidates.push_back(ip);
          if (g_up.isUp(ca_unspec, url, opts)) {
            available.push_back(ip);
          }
        }
        if(!available.empty()) {
          vector<ComboAddress> res = useSelector(getOptionValue(options, "selector", "random"), s_lua_record_ctx->bestwho, available);
          return convComboAddressListToString(res);
        }
      }

      // All units down, apply backupSelector on all candidates
      vector<ComboAddress> res = useSelector(getOptionValue(options, "backupSelector", "random"), s_lua_record_ctx->bestwho, candidates);
      return convComboAddressListToString(res);
    });

  lua.writeFunction("ifurlup", [](const std::string& url,
                                          const boost::variant<iplist_t, ipunitlist_t>& ips,
                                          boost::optional<opts_t> options) {

    auto checker = [&url](const ComboAddress& addr, const opts_t& opts) {
        return g_up.isUp(addr, url, opts);
      };
      return genericIfUp(ips, options, checker);
    });
  /*
   * Returns a random IP address from the supplied list
   * @example pickrandom({ '1.2.3.4', '5.4.3.2' })"
   */
  lua.writeFunction("pickrandom", [](const iplist_t& ips) {
      vector<string> items = convStringList(ips);
      return pickRandom<string>(items);
    });

  /*
   * Based on the hash of `bestwho`, returns an IP address from the list
   * supplied, weighted according to the results of isUp calls.
   * @example pickselfweighted('http://example.com/weight', { "192.0.2.20", "203.0.113.4", "203.0.113.2" })
   */
  lua.writeFunction("pickselfweighted", [](const std::string& url,
                                             const iplist_t& ips,
                                             boost::optional<opts_t> options) {
      vector< pair<int, ComboAddress> > items;
      opts_t opts;
      if(options) {
        opts = *options;
      }

      items.reserve(ips.capacity());
      bool available = false;

      vector<ComboAddress> conv = convComboAddressList(ips);
      for (auto& entry : conv) {
        int weight = 0;
        weight = g_up.isUp(entry, url, opts);
        if(weight>0) {
          available = true;
        }
        items.emplace_back(weight, entry);
      }
      if(available) {
        return pickWeightedHashed<ComboAddress>(s_lua_record_ctx->bestwho, items).toString();
      }

      // All units down, apply backupSelector on all candidates
      return pickWeightedRandom<ComboAddress>(items).toString();
    });

  lua.writeFunction("pickrandomsample", [](int n, const iplist_t& ips) {
      vector<string> items = convStringList(ips);
	  return pickRandomSample<string>(n, items);
    });

  lua.writeFunction("pickhashed", [](const iplist_t& ips) {
      vector<string> items = convStringList(ips);
      return pickHashed<string>(s_lua_record_ctx->bestwho, items);
    });
  /*
   * Returns a random IP address from the supplied list, as weighted by the
   * various ``weight`` parameters
   * @example pickwrandom({ {100, '1.2.3.4'}, {50, '5.4.3.2'}, {1, '192.168.1.0'} })
   */
  lua.writeFunction("pickwrandom", [](std::unordered_map<int, wiplist_t> ips) {
      vector< pair<int, string> > items = convIntStringPairList(ips);
      return pickWeightedRandom<string>(items);
    });

  /*
   * Based on the hash of `bestwho`, returns an IP address from the list
   * supplied, as weighted by the various `weight` parameters
   * @example pickwhashed({ {15, '1.2.3.4'}, {50, '5.4.3.2'} })
   */
  lua.writeFunction("pickwhashed", [](std::unordered_map<int, wiplist_t > ips) {
      vector< pair<int, string> > items;

      items.reserve(ips.size());
      for (auto& entry : ips) {
        items.emplace_back(atoi(entry.second[1].c_str()), entry.second[2]);
      }

      return pickWeightedHashed<string>(s_lua_record_ctx->bestwho, items);
    });

  /*
   * Based on the hash of the record name, return an IP address from the list
   * supplied, as weighted by the various `weight` parameters
   * @example picknamehashed({ {15, '1.2.3.4'}, {50, '5.4.3.2'} })
   */
  lua.writeFunction("picknamehashed", [](std::unordered_map<int, wiplist_t > ips) {
      vector< pair<int, string> > items;

      items.reserve(ips.size());
      for(auto& i : ips)
      {
        items.emplace_back(atoi(i.second[1].c_str()), i.second[2]);
      }

      return pickWeightedNameHashed<string>(s_lua_record_ctx->qname, items);
    });
  /*
   * Based on the hash of `bestwho`, returns an IP address from the list
   * supplied, as weighted by the various `weight` parameters and distributed consistently
   * @example pickchashed({ {15, '1.2.3.4'}, {50, '5.4.3.2'} })
   */
  lua.writeFunction("pickchashed", [](const std::unordered_map<int, wiplist_t>& ips) {
    std::vector<std::pair<int, std::string>> items;

    items.reserve(ips.size());
    for (const auto& entry : ips) {
      items.emplace_back(atoi(entry.second.at(1).c_str()), entry.second.at(2));
    }

    return pickConsistentWeightedHashed(s_lua_record_ctx->bestwho, items);
  });

  lua.writeFunction("pickclosest", [](const iplist_t& ips) {
      vector<ComboAddress> conv = convComboAddressList(ips);

      return pickclosest(s_lua_record_ctx->bestwho, conv).toString();

    });

  if (g_luaRecordExecLimit > 0) {
      lua.executeCode(boost::str(boost::format("debug.sethook(report, '', %d)") % g_luaRecordExecLimit));
  }

  lua.writeFunction("report", [](string /* event */, boost::optional<string> /* line */){
      throw std::runtime_error("Script took too long");
    });

  lua.writeFunction("geoiplookup", [](const string &ip, const GeoIPInterface::GeoIPQueryAttribute attr) {
    return getGeo(ip, attr);
  });
  lua.writeVariable("GeoIPQueryAttribute", std::unordered_map<std::string, int>{{"ASn", GeoIPInterface::GeoIPQueryAttribute::ASn}, {"City", GeoIPInterface::GeoIPQueryAttribute::City}, {"Continent", GeoIPInterface::GeoIPQueryAttribute::Continent}, {"Country", GeoIPInterface::GeoIPQueryAttribute::Country}, {"Country2", GeoIPInterface::GeoIPQueryAttribute::Country2}, {"Name", GeoIPInterface::GeoIPQueryAttribute::Name}, {"Region", GeoIPInterface::GeoIPQueryAttribute::Region}, {"Location", GeoIPInterface::GeoIPQueryAttribute::Location}});

  typedef const boost::variant<string,vector<pair<int,string> > > combovar_t;

  lua.writeFunction("asnum", [](const combovar_t& asns) {
      string res=getGeo(s_lua_record_ctx->bestwho.toString(), GeoIPInterface::ASn);
      return doCompare(asns, res, [](const std::string& a, const std::string& b) {
          return !strcasecmp(a.c_str(), b.c_str());
        });
    });
  lua.writeFunction("continent", [](const combovar_t& continent) {
     string res=getGeo(s_lua_record_ctx->bestwho.toString(), GeoIPInterface::Continent);
      return doCompare(continent, res, [](const std::string& a, const std::string& b) {
          return !strcasecmp(a.c_str(), b.c_str());
        });
    });
  lua.writeFunction("continentCode", []() {
      string unknown("unknown");
      string res = getGeo(s_lua_record_ctx->bestwho.toString(), GeoIPInterface::Continent);
      if ( res == unknown ) {
       return std::string("--");
      }
      return res;
    });
  lua.writeFunction("country", [](const combovar_t& var) {
      string res = getGeo(s_lua_record_ctx->bestwho.toString(), GeoIPInterface::Country2);
      return doCompare(var, res, [](const std::string& a, const std::string& b) {
          return !strcasecmp(a.c_str(), b.c_str());
        });

    });
  lua.writeFunction("countryCode", []() {
      string unknown("unknown");
      string res = getGeo(s_lua_record_ctx->bestwho.toString(), GeoIPInterface::Country2);
      if ( res == unknown ) {
       return std::string("--");
      }
      return res;
    });
  lua.writeFunction("region", [](const combovar_t& var) {
      string res = getGeo(s_lua_record_ctx->bestwho.toString(), GeoIPInterface::Region);
      return doCompare(var, res, [](const std::string& a, const std::string& b) {
          return !strcasecmp(a.c_str(), b.c_str());
        });

    });
  lua.writeFunction("regionCode", []() {
      string unknown("unknown");
      string res = getGeo(s_lua_record_ctx->bestwho.toString(), GeoIPInterface::Region);
      if ( res == unknown ) {
       return std::string("--");
      }
      return res;
    });
  lua.writeFunction("netmask", [](const iplist_t& ips) {
      for(const auto& i :ips) {
        Netmask nm(i.second);
        if(nm.match(s_lua_record_ctx->bestwho))
          return true;
      }
      return false;
    });
  /* {
       {
        {'192.168.0.0/16', '10.0.0.0/8'},
        {'192.168.20.20', '192.168.20.21'}
       },
       {
        {'0.0.0.0/0'}, {'192.0.2.1'}
       }
     }
  */
  lua.writeFunction("view", [](const vector<pair<int, vector<pair<int, iplist_t> > > >& in) {
      for(const auto& rule : in) {
        const auto& netmasks=rule.second[0].second;
        const auto& destinations=rule.second[1].second;
        for(const auto& nmpair : netmasks) {
          Netmask nm(nmpair.second);
          if(nm.match(s_lua_record_ctx->bestwho)) {
            if (destinations.empty()) {
              throw std::invalid_argument("The IP list cannot be empty (for netmask " + nm.toString() + ")");
            }
            return destinations[dns_random(destinations.size())].second;
          }
        }
      }
      return std::string();
    });

  lua.writeFunction("all", [](const vector< pair<int,string> >& ips) {
      vector<string> result;
	  result.reserve(ips.size());

      for(const auto& ip : ips) {
          result.emplace_back(ip.second);
      }
      if(result.empty()) {
        throw std::invalid_argument("The IP list cannot be empty");
      }
      return result;
    });

  lua.writeFunction("dblookup", [](const string& record, uint16_t qtype) {
    DNSName rec;
    vector<string> ret;
    try {
      rec = DNSName(record);
    }
    catch (const std::exception& e) {
      g_log << Logger::Error << "DB lookup cannot be performed, the name (" << record << ") is malformed: " << e.what() << endl;
      return ret;
    }
    try {
      SOAData soaData;

      if (!getAuth(rec, qtype, &soaData)) {
        return ret;
      }

      vector<DNSZoneRecord> drs = lookup(rec, qtype, soaData.domain_id);
      for (const auto& drec : drs) {
        ret.push_back(drec.dr.getContent()->getZoneRepresentation());
      }
    }
    catch (std::exception& e) {
      g_log << Logger::Error << "Failed to do DB lookup for " << rec << "/" << qtype << ": " << e.what() << endl;
    }
    return ret;
  });

  lua.writeFunction("include", [&lua](string record) {
      DNSName rec;
      try {
        rec = DNSName(record) + s_lua_record_ctx->zone;
      } catch (const std::exception &e){
        g_log<<Logger::Error<<"Included record cannot be loaded, the name ("<<record<<") is malformed: "<<e.what()<<endl;
        return;
      }
      try {
        vector<DNSZoneRecord> drs = lookup(rec, QType::LUA, s_lua_record_ctx->zone_record.domain_id);
        for(const auto& dr : drs) {
          auto lr = getRR<LUARecordContent>(dr.dr);
          lua.executeCode(lr->getCode());
        }
      }
      catch(std::exception& e) {
        g_log<<Logger::Error<<"Failed to load include record for Lua record "<<rec<<": "<<e.what()<<endl;
      }
    });
}

std::vector<shared_ptr<DNSRecordContent>> luaSynth(const std::string& code, const DNSName& query, const DNSZoneRecord& zone_record, const DNSName& zone, const DNSPacket& dnsp, uint16_t qtype, unique_ptr<AuthLua4>& LUA)
{
  if(!LUA ||                  // we don't have a Lua state yet
     !g_LuaRecordSharedState) { // or we want a new one even if we had one
    LUA = make_unique<AuthLua4>(::arg()["lua-global-include-dir"]);
    setupLuaRecords(*LUA->getLua());
  }

  std::vector<shared_ptr<DNSRecordContent>> ret;

  LuaContext& lua = *LUA->getLua();

  s_lua_record_ctx = std::make_unique<lua_record_ctx_t>();
  s_lua_record_ctx->qname = query;
  s_lua_record_ctx->zone_record = zone_record;
  s_lua_record_ctx->zone = zone;

  lua.writeVariable("qname", query);
  lua.writeVariable("zone", zone);
  lua.writeVariable("zoneid", zone_record.domain_id);
  lua.writeVariable("who", dnsp.getInnerRemote());
  lua.writeVariable("localwho", dnsp.getLocal());
  lua.writeVariable("dh", (dnsheader*)&dnsp.d);
  lua.writeVariable("dnssecOK", dnsp.d_dnssecOk);
  lua.writeVariable("tcp", dnsp.d_tcp);
  lua.writeVariable("ednsPKTSize", dnsp.d_ednsRawPacketSizeLimit);
  if(dnsp.hasEDNSSubnet()) {
    lua.writeVariable("ecswho", dnsp.getRealRemote());
    s_lua_record_ctx->bestwho = dnsp.getRealRemote().getNetwork();
  }
  else {
    lua.writeVariable("ecswho", nullptr);
    s_lua_record_ctx->bestwho = dnsp.getInnerRemote();
  }
  lua.writeVariable("bestwho", s_lua_record_ctx->bestwho);

  try {
    string actual;
    if(!code.empty() && code[0]!=';')
      actual = "return " + code;
    else
      actual = code.substr(1);

    auto content=lua.executeCode<boost::variant<string, vector<pair<int, string> > > >(actual);

    vector<string> contents;
    if(auto str = boost::get<string>(&content))
      contents.push_back(*str);
    else
      for(const auto& c : boost::get<vector<pair<int,string>>>(content))
        contents.push_back(c.second);

    for(const auto& content_it: contents) {
      if(qtype==QType::TXT)
        ret.push_back(DNSRecordContent::make(qtype, QClass::IN, '"' + content_it + '"'));
      else
        ret.push_back(DNSRecordContent::make(qtype, QClass::IN, content_it));
    }
  } catch(std::exception &e) {
    g_log << Logger::Info << "Lua record ("<<query<<"|"<<QType(qtype).toString()<<") reported: " << e.what();
    try {
      std::rethrow_if_nested(e);
      g_log<<endl;
    } catch(const std::exception& ne) {
      g_log << ": " << ne.what() << std::endl;
    }
    catch(const PDNSException& ne) {
      g_log << ": " << ne.reason << std::endl;
    }
    throw ;
  }

  return ret;
}
