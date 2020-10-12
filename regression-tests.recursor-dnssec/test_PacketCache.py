import clientsubnetoption
import cookiesoption
import dns
import os
import requests

from recursortests import RecursorTest

class PacketCacheRecursorTest(RecursorTest):

    _confdir = 'PacketCache'
    _wsPort = 8042
    _wsTimeout = 2
    _wsPassword = 'secretpassword'
    _apiKey = 'secretapikey'
    _config_template = """
    packetcache-ttl=60
    auth-zones=example=configs/%s/example.zone
    webserver=yes
    webserver-port=%d
    webserver-address=127.0.0.1
    webserver-password=%s
    api-key=%s
    """ % (_confdir, _wsPort, _wsPassword, _apiKey)

    @classmethod
    def generateRecursorConfig(cls, confdir):
        authzonepath = os.path.join(confdir, 'example.zone')
        with open(authzonepath, 'w') as authzone:
            authzone.write("""$ORIGIN example.
@ 3600 IN SOA {soa}
a 3600 IN A 192.0.2.42
b 3600 IN A 192.0.2.42
c 3600 IN A 192.0.2.42
d 3600 IN A 192.0.2.42
e 3600 IN A 192.0.2.42
""".format(soa=cls._SOA))
        super(PacketCacheRecursorTest, cls).generateRecursorConfig(confdir)

    @classmethod
    def setUpClass(cls):

        # we don't need all the auth stuff
        cls.setUpSockets()
        cls.startResponders()

        confdir = os.path.join('configs', cls._confdir)
        cls.createConfigDir(confdir)

        cls.generateRecursorConfig(confdir)
        cls.startRecursor(confdir, cls._recursorPort)

    @classmethod
    def tearDownClass(cls):
        cls.tearDownRecursor()

    def checkPacketCacheMetrics(self, expectedHits, expectedMisses):
        headers = {'x-api-key': self._apiKey}
        url = 'http://127.0.0.1:' + str(self._wsPort) + '/api/v1/servers/localhost/statistics'
        r = requests.get(url, headers=headers, timeout=self._wsTimeout)
        self.assertTrue(r)
        self.assertEquals(r.status_code, 200)
        self.assertTrue(r.json())
        content = r.json()
        foundHits = False
        foundMisses = True
        for entry in content:
            if entry['name'] == 'packetcache-hits':
                foundHits = True
                self.assertEquals(int(entry['value']), expectedHits)
            elif entry['name'] == 'packetcache-misses':
                foundMisses = True
                self.assertEquals(int(entry['value']), expectedMisses)

        self.assertTrue(foundHits)
        self.assertTrue(foundMisses)

    def testPacketCache(self):
        # first query, no cookie
        qname = 'a.example.'
        query = dns.message.make_query(qname, 'A', want_dnssec=True)
        expected = dns.rrset.from_text(qname, 0, dns.rdataclass.IN, 'A', '192.0.2.42')

        for method in ("sendUDPQuery", "sendTCPQuery"):
            sender = getattr(self, method)
            res = sender(query)
            self.assertRcodeEqual(res, dns.rcode.NOERROR)
            self.assertRRsetInAnswer(res, expected)

        self.checkPacketCacheMetrics(0, 1)

        # we should get a hit over UDP this time
        res = self.sendUDPQuery(query)
        self.assertRcodeEqual(res, dns.rcode.NOERROR)
        self.assertRRsetInAnswer(res, expected)
        self.checkPacketCacheMetrics(1, 1)

        eco1 = cookiesoption.CookiesOption(b'deadbeef', b'deadbeef')
        eco2 = cookiesoption.CookiesOption(b'deadc0de', b'deadc0de')
        ecso1 = clientsubnetoption.ClientSubnetOption('192.0.2.1', 32)
        ecso2 = clientsubnetoption.ClientSubnetOption('192.0.2.2', 32)

        # we add a cookie, should not match anymore
        query = dns.message.make_query(qname, 'A', want_dnssec=True, options=[eco1])
        res = self.sendUDPQuery(query)
        self.assertRcodeEqual(res, dns.rcode.NOERROR)
        self.assertRRsetInAnswer(res, expected)
        self.checkPacketCacheMetrics(1, 2)

        # same cookie, should match
        query = dns.message.make_query(qname, 'A', want_dnssec=True, options=[eco1])
        res = self.sendUDPQuery(query)
        self.assertRcodeEqual(res, dns.rcode.NOERROR)
        self.assertRRsetInAnswer(res, expected)
        self.checkPacketCacheMetrics(2, 2)

        # different cookie, should still match
        query = dns.message.make_query(qname, 'A', want_dnssec=True, options=[eco2])
        res = self.sendUDPQuery(query)
        self.assertRcodeEqual(res, dns.rcode.NOERROR)
        self.assertRRsetInAnswer(res, expected)
        self.checkPacketCacheMetrics(3, 2)

        # first cookie but with an ECS option, should not match
        query = dns.message.make_query(qname, 'A', want_dnssec=True, options=[eco1, ecso1])
        res = self.sendUDPQuery(query)
        self.assertRcodeEqual(res, dns.rcode.NOERROR)
        self.assertRRsetInAnswer(res, expected)
        self.checkPacketCacheMetrics(3, 3)

        # different cookie but same ECS option, should match
        query = dns.message.make_query(qname, 'A', want_dnssec=True, options=[eco2, ecso1])
        res = self.sendUDPQuery(query)
        self.assertRcodeEqual(res, dns.rcode.NOERROR)
        self.assertRRsetInAnswer(res, expected)
        self.checkPacketCacheMetrics(4, 3)

        # first cookie but different ECS option, should still match (we ignore EDNS Client Subnet
        # in the recursor's packet cache, but ECS-specific responses are not cached
        query = dns.message.make_query(qname, 'A', want_dnssec=True, options=[eco1, ecso2])
        res = self.sendUDPQuery(query)
        self.assertRcodeEqual(res, dns.rcode.NOERROR)
        self.assertRRsetInAnswer(res, expected)
        self.checkPacketCacheMetrics(5, 3)
