#!/usr/bin/env python
import Queue
import threading
import socket
import struct
import sys
import time
from dnsdisttests import DNSDistTest
from cdblib import CDB, CDBWriter

import dns
import dnsmessage_pb2

import os


def create_cdb():                                               # create test cdb file
    m = CDBWriter('test.cdb')

    m['1fuks551ut9x8d1gs9u801k0v9g7.net'] = 'rpz-test-1'
    m['1funvaz1momy1vruganraqwii7.org'] = 'rpz-test-1'
    m['1fuyfus16emwaj4b253a15gbh9c.org'] = 'rpz-test-1'
    m['azurewebsites.net'] = 'rpz-test-1'
    m['zziemjfgdu.nightmichael.top'] = 'rpz-test-1'
    m['lua.protobuf.tests.powerdns.com'] = 'rpz-test-1'

    m.close()


def delete_cdb():                                               # delete test cdb file
    os.remove('test.cdb')


class TestNamedCache(DNSDistTest):
    create_cdb()                                                # create test cdb file first
    _protobufServerPort = 4342
    _protobufQueue = Queue.Queue()
    _protobufCounter = 0
    _config_params = ['_testServerPort', '_protobufServerPort']
    _config_template = """					-- start of the lua code for dnsdist conf file
    newServer{address="127.0.0.1:%s", useClientSubnet=true}
    rl = newRemoteLogger('127.0.0.1:%s')                      -- protobuf server

    luasmn = newSuffixMatchNode()
    luasmn:add(newDNSName('lua.protobuf.tests.powerdns.com.'))

    function alterProtobufQuery(dq, protobuf)	
      if luasmn:check(dq.qname) then
        requestor = newCA(dq.remoteaddr:toString())		
        if requestor:isIPv4() then
          requestor:truncate(24)
        else
          requestor:truncate(56)
        end
        protobuf:setRequestor(requestor)

        local tableTags = {}
        tableTags = dq:getTagArray()				-- get table from DNSQuery

        local tablePB = {}
        for k, v in pairs( tableTags) do
          table.insert(tablePB, k .. "," .. v)
        end

        protobuf:setTagArray(tablePB)				-- store table in protobuf

        protobuf:setTag("Query,123")				-- add another tag entry in protobuf

        protobuf:setTag("found,yes")			        -- tag as yes response in protobuf	

        protobuf:setResponseCode(dnsdist.NXDOMAIN)        	-- set protobuf response code to be NXDOMAIN

        local strReqName = dq.qname:toString()		  	-- get request dns name

        protobuf:setProtobufResponseType()			-- set protobuf to look like a response and not a query, with 0 default time

        blobData = '\127' .. '\000' .. '\000' .. '\001'		-- 127.0.0.1, note: lua 5.1 can only embed decimal not hex

        protobuf:addResponseRR(strReqName, 1, 1, 123, blobData) -- add a RR to the protobuf

        protobuf:setBytes(65)					-- set the size of the query to confirm in checkProtobufBase
      else

        requestor = newCA(dq.remoteaddr:toString())		
        if requestor:isIPv4() then
          requestor:truncate(24)
        else
          requestor:truncate(56)
        end
        protobuf:setRequestor(requestor)

        local tableTags = {}                                    
        table.insert(tableTags, "TestLabel1,TestData1")
        table.insert(tableTags, "TestLabel2,TestData2")

        protobuf:setTagArray(tableTags)
        protobuf:setTag('TestLabel3,TestData3')
        protobuf:setTag("Query,123")

        protobuf:setTag("found,no")			        -- tag as no response in protobuf


        local strReqName = dq.qname:toString()		  	-- get request dns name

        protobuf:setProtobufResponseType()			-- set protobuf to look like a response and not a query, with 0 default time

        blobData = '\127' .. '\000' .. '\000' .. '\001'		-- 127.0.0.1, note: lua 5.1 can only embed decimal not hex

        protobuf:addResponseRR(strReqName, 1, 1, 123, blobData) -- add a RR to the protobuf

        protobuf:setBytes(65)					-- set the size of the query to confirm in checkProtobufBase

      end
    end


    function alterProtobufResponse(dq, protobuf)                -- after dnsdist lookup alter protobuf message
      if luasmn:check(dq.qname) then
        requestor = newCA(dq.remoteaddr:toString())		
        if requestor:isIPv4() then
          requestor:truncate(24)
        else
          requestor:truncate(56)
        end
        protobuf:setRequestor(requestor)
        local tableTags = {}
        table.insert(tableTags, "TestLabel1,TestData1")
        table.insert(tableTags, "TestLabel2,TestData2")
        protobuf:setTagArray(tableTags)
        protobuf:setTag('TestLabel3,TestData3')
        protobuf:setTag("Response,456")
        protobuf:setTag("found,no")				-- tag as no response in protobuf
      else
        requestor = newCA(dq.remoteaddr:toString())		
        if requestor:isIPv4() then
          requestor:truncate(24)
        else
          requestor:truncate(56)
        end
        protobuf:setRequestor(requestor)
        local tableTags = {} 					
        table.insert(tableTags, "TestLabel1,TestData1")
        table.insert(tableTags, "TestLabel2,TestData2")
        protobuf:setTagArray(tableTags)
        protobuf:setTag('TestLabel3,TestData3')
        protobuf:setTag("Response,456")
        protobuf:setTag("found,no")		                -- tag as no response in protobuf		
      end
    end

    function checkNamedCache(dq)                                -- before dnsdist lookup check for named cache match

      local statTable = xxx:getStats()
      if statTable['cacheName'] ~= "xxx" then
         io.stderr:write(string.format("******* checkNamedCache() - xxx stat table entry cacheName mismatch: %%s \\n", statTable['cacheName']))
         return DNSAction.Nxdomain, ""                          --  this will stop testing....
      end

      local statTable2 = yyy:getStats()
      if statTable2['cacheName'] ~= "yyy" then
         io.stderr:write(string.format("******* checkNamedCache() - yyy stat table entry cacheName mismatch: %%s \\n", statTable2['cacheName']))
         return DNSAction.Nxdomain, ""                          --  this will stop testing....
      end


      local result  = getNamedCache("xxx"):lookupQ(dq)	        -- compare all the various methods of looking up named cache entries
      local result2 = yyy:lookupQ(dq)	
      local strReq  = dq.qname:toString()
      local result3 = getNamedCache("xxx"):lookup(strReq)
      local result4 = yyy:lookup(strReq)

      if result.found ~= result2.found then
        io.stderr:write(string.format("$$$$$$$$$ checkNamedCache() -> BAD mismatch - bindToCDB & loadFromCDB - found: %%s   2: %%s \\n", result.found, result2.found ))	
        return DNSAction.Nxdomain, ""                           --  this will stop testing....
      end
      if result.data ~= result2.data then
        io.stderr:write(string.format("$$$$$$$$$ checkNamedCache() -> BAD mismatch - bindToCDB & loadFromCDB - data: %%s   2: %%s \\n", result.data, result2.data ))	
        return DNSAction.Nxdomain, ""                           --  this will stop testing....
      end

      if result.found ~= result3.found then
        io.stderr:write(string.format("$$$$$$$$$ checkNamedCache() -> BAD mismatch - bindToCDB & loadFromCDB - found: %%s   3: %%s \\n", result.found, result3.found ))	
        return DNSAction.Nxdomain, ""                           --  this will stop testing....
      end
      if result.data ~= result3.data then
        io.stderr:write(string.format("$$$$$$$$$ checkNamedCache() -> BAD mismatch - bindToCDB & loadFromCDB - data: %%s   3: %%s \\n", result.data, result3.data ))	
        return DNSAction.Nxdomain, ""                           --  this will stop testing....
      end

      if result.found ~= result4.found then
        io.stderr:write(string.format("$$$$$$$$$ checkNamedCache() -> BAD mismatch - bindToCDB & loadFromCDB - found: %%s   4: %%s \\n", result.found, result4.found ))	
        return DNSAction.Nxdomain, ""                           --  this will stop testing....
      end
      if result.data ~= result4.data then
        io.stderr:write(string.format("$$$$$$$$$ checkNamedCache() -> BAD mismatch - bindToCDB & loadFromCDB - data: %%s   4: %%s \\n", result.data, result4.data ))	
        return DNSAction.Nxdomain, ""                           --  this will stop testing....
      end

      if result.found then
         m = newDNSDistProtobufMessage(dq)                      -- matching entry was found in cdb
     
         local tableTags = dq:getTagArray()
         local tablePB = {}
         for k, v in pairs( tableTags) do
           table.insert(tablePB, k .. ", " .. v)
         end
     
         local strReqName = dq.qname:toString()
         blobData = '\127' .. '\000' .. '\000' .. '\001'
     
         m:setProtobufResponseType()
         m:addResponseRR(strReqName, 1, 1, 456, blobData)
         m:setResponseCode(dnsdist.NXDOMAIN)

         requestor = newCA(dq.remoteaddr:toString())		
         if requestor:isIPv4() then
          requestor:truncate(24)
         else
          requestor:truncate(56)
         end
         m:setRequestor(requestor)

         local tableTags = {}                                    
         table.insert(tableTags, "TestLabel1,TestData1")
         table.insert(tableTags, "TestLabel2,TestData2")

         m:setTagArray(tableTags)
         m:setTag('TestLabel3,TestData3')
         m:setTag("Query,123")
         m:setTag("found,yes")                                  -- tag as yes response in protobuf
 
         remoteLog(rl, m)                                       -- send out protobuf

         return DNSAction.Spoof, "1.2.3.4"			-- spoof test, only 1 protobuf msg, no dns query

      end
      return DNSAction.None, ""			                -- allow rule processing to continue
    end


    xxx = getNamedCache("xxx")        -- load test cdb into 'loadFrom' named cache 
    xxx:loadFromCDB("test.cdb")

    yyy = getNamedCache("yyy")
    yyy:bindToCDB("test.cdb")          -- load test cdb into 'bindTo' named cache

    addLuaAction(AllRule(), checkNamedCache)		      -- check if named cache hit, if so return nxdomain and send protobuf

    addAction(AllRule(), RemoteLogAction(rl, alterProtobufQuery))                           -- Send protobuf message before lookup

    addResponseAction(AllRule(), RemoteLogResponseAction(rl, alterProtobufResponse, true))  -- Send protobuf message after lookup

    """

    @classmethod
    def ProtobufListener(cls, port):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        try:
            sock.bind(("127.0.0.1", port))
        except socket.error as e:
            print("Error binding in the protbuf listener: %s" % str(e))
            sys.exit(1)

        sock.listen(100)
        while True:
            (conn, _) = sock.accept()
            data = None
            while True:
                data = conn.recv(2)
                if not data:
                    break
                (datalen,) = struct.unpack("!H", data)
                data = conn.recv(datalen)
                if not data:
                    break

                cls._protobufQueue.put(data, True, timeout=2.0)

            conn.close()
        sock.close()

    @classmethod
    def startResponders(cls):
        cls._UDPResponder = threading.Thread(name='UDP Responder', target=cls.UDPResponder, args=[cls._testServerPort, cls._toResponderQueue, cls._fromResponderQueue])
        cls._UDPResponder.setDaemon(True)
        cls._UDPResponder.start()

        cls._TCPResponder = threading.Thread(name='TCP Responder', target=cls.TCPResponder, args=[cls._testServerPort, cls._toResponderQueue, cls._fromResponderQueue])
        cls._TCPResponder.setDaemon(True)
        cls._TCPResponder.start()

        cls._protobufListener = threading.Thread(name='Protobuf Listener', target=cls.ProtobufListener, args=[cls._protobufServerPort])
        cls._protobufListener.setDaemon(True)
        cls._protobufListener.start()

    def getFirstProtobufMessage(self):
        self.assertFalse(self._protobufQueue.empty())
        data = self._protobufQueue.get(False)
        self.assertTrue(data)
        msg = dnsmessage_pb2.PBDNSMessage()
        msg.ParseFromString(data)
        return msg

    def checkProtobufBase(self, msg, protocol, query, initiator, normalQueryResponse=True):
        self.assertTrue(msg)
        self.assertTrue(msg.HasField('timeSec'))
        self.assertTrue(msg.HasField('socketFamily'))
        self.assertEquals(msg.socketFamily, dnsmessage_pb2.PBDNSMessage.INET)
        self.assertTrue(msg.HasField('from'))
        fromvalue = getattr(msg, 'from')
        self.assertEquals(socket.inet_ntop(socket.AF_INET, fromvalue), initiator)
        self.assertTrue(msg.HasField('socketProtocol'))
        self.assertEquals(msg.socketProtocol, protocol)
        self.assertTrue(msg.HasField('messageId'))
        self.assertTrue(msg.HasField('id'))
        self.assertEquals(msg.id, query.id)
        self.assertTrue(msg.HasField('inBytes'))
        if normalQueryResponse:
          # compare inBytes with length of query/response
          self.assertEquals(msg.inBytes, len(query.to_wire()))

    def checkProtobufQuery(self, msg, protocol, query, qclass, qtype, qname, initiator='127.0.0.1'):
        self.assertEquals(msg.type, dnsmessage_pb2.PBDNSMessage.DNSQueryType)
        self.checkProtobufBase(msg, protocol, query, initiator)
        # dnsdist doesn't fill the responder field for responses
        # because it doesn't keep the information around.
        self.assertTrue(msg.HasField('to'))
        self.assertEquals(socket.inet_ntop(socket.AF_INET, msg.to), '127.0.0.1')
        self.assertTrue(msg.HasField('question'))
        self.assertTrue(msg.question.HasField('qClass'))
        self.assertEquals(msg.question.qClass, qclass)
        self.assertTrue(msg.question.HasField('qType'))
        self.assertEquals(msg.question.qClass, qtype)
        self.assertTrue(msg.question.HasField('qName'))
        self.assertEquals(msg.question.qName, qname)

    def checkProtobufTags(self, tags, expectedTags):
        # only differences will be in new list
        listx = set(tags) ^ set(expectedTags)
        # exclusive or of lists should be empty
        strErrMsg = "Protobuf tags don't match: %s ----> %s" % (listx, tags)
        self.assertEqual(len(listx), 0, strErrMsg)

    def checkProtobufQueryConvertedToResponse(self, msg, protocol, response, initiator='127.0.0.0'):
        self.assertEquals(msg.type, dnsmessage_pb2.PBDNSMessage.DNSResponseType)
        self.checkProtobufBase(msg, protocol, response, initiator, False)
        self.assertTrue(msg.HasField('response'))
        self.assertTrue(msg.response.HasField('queryTimeSec'))

    def checkProtobufResponse(self, msg, protocol, response, initiator='127.0.0.1'):
        self.assertEquals(msg.type, dnsmessage_pb2.PBDNSMessage.DNSResponseType)
        self.checkProtobufBase(msg, protocol, response, initiator)
        self.assertTrue(msg.HasField('response'))
        self.assertTrue(msg.response.HasField('queryTimeSec'))

    def checkProtobufResponseRecord(self, record, rclass, rtype, rname, rttl):
        self.assertTrue(record.HasField('class'))
        self.assertEquals(getattr(record, 'class'), rclass)
        self.assertTrue(record.HasField('type'))
        self.assertEquals(record.type, rtype)
        self.assertTrue(record.HasField('name'))
        self.assertEquals(record.name, rname)
        self.assertTrue(record.HasField('ttl'))
        self.assertEquals(record.ttl, rttl)
        self.assertTrue(record.HasField('rdata'))

# start of python execution 


    
    def testLuaNamedCacheMatch(self):                                   # test named cache lookup for 'match'
        """
        Protobuf: Check that the Lua callback rewrote the initiator
        """

        name = 'lua.protobuf.tests.powerdns.com.'			# dns name to lookup and match.....
        query = dns.message.make_query(name, 'A', 'IN')
        response = dns.message.make_response(query)
        rrset = dns.rrset.from_text(name,
                                    3600,
                                    dns.rdataclass.IN,
                                    dns.rdatatype.A,
                                    '127.0.0.1')
        response.answer.append(rrset)


        (receivedQuery, receivedResponse) = self.sendUDPQuery(query, response) # send out the UDP query

        self.assertTrue(receivedResponse)

        
        time.sleep(1)								# let the protobuf messages the time to get there

        									# check the protobuf message corresponding to the UDP query
        msg = self.getFirstProtobufMessage()

        self.checkProtobufQueryConvertedToResponse(msg, dnsmessage_pb2.PBDNSMessage.UDP, response, '127.0.0.0')

        self.checkProtobufTags(msg.response.tags, [ u"TestLabel1,TestData1", u"TestLabel2,TestData2", u"TestLabel3,TestData3", u"Query,123", u"found,yes"])  


        (receivedQuery, receivedResponse) = self.sendTCPQuery(query, response)
        self.assertTrue(receivedResponse)
        									# let the protobuf messages the time to get there
        time.sleep(1)

        									# check the protobuf message corresponding to the TCP query
        msg = self.getFirstProtobufMessage()

        self.checkProtobufQueryConvertedToResponse(msg, dnsmessage_pb2.PBDNSMessage.TCP, response, '127.0.0.0')

        self.checkProtobufTags(msg.response.tags, [ u"TestLabel1,TestData1", u"TestLabel2,TestData2", u"TestLabel3,TestData3", u"Query,123", u"found,yes"])



     
    def testLuaNamedCacheNoMatch(self):                                 # test named cache lookup for 'no match'
        """
        Protobuf: Check that the Lua callback rewrote the initiator
        """

        name = 'xxxxlua.protobuf.tests.powerdns.com.'			# dns name to lookup and match.....
        query = dns.message.make_query(name, 'A', 'IN')
        response = dns.message.make_response(query)
        rrset = dns.rrset.from_text(name,
                                    3600,
                                    dns.rdataclass.IN,
                                    dns.rdatatype.A,
                                    '127.0.0.1')
        response.answer.append(rrset)


        (receivedQuery, receivedResponse) = self.sendUDPQuery(query, response)  # send out the UDP query

        self.assertTrue(receivedResponse)

        time.sleep(1)								# let the protobuf messages the time to get there

        msg = self.getFirstProtobufMessage()                                    # protobuf message before dnsdist lookup

        self.checkProtobufQueryConvertedToResponse(msg, dnsmessage_pb2.PBDNSMessage.UDP, response, '127.0.0.0')	             

        self.checkProtobufTags(msg.response.tags, [ u"TestLabel1,TestData1", u"TestLabel2,TestData2", u"TestLabel3,TestData3", u"Query,123", u"found,no"])   # query


        msg = self.getFirstProtobufMessage()                                    # protobuf message after dnsdist lookup

 
        self.checkProtobufResponse(msg, dnsmessage_pb2.PBDNSMessage.UDP, response, '127.0.0.0')

        self.checkProtobufTags(msg.response.tags, [ u"TestLabel1,TestData1", u"TestLabel2,TestData2", u"TestLabel3,TestData3", u"Response,456", u"found,no"])   # response

        self.assertEquals(len(msg.response.rrs), 1)
        for rr in msg.response.rrs:
            self.checkProtobufResponseRecord(rr, dns.rdataclass.IN, dns.rdatatype.A, name, 3600)
            self.assertEquals(socket.inet_ntop(socket.AF_INET, rr.rdata), '127.0.0.1')



        (receivedQuery, receivedResponse) = self.sendTCPQuery(query, response)  # send out the TCP query

        self.assertTrue(receivedResponse)
        									
        time.sleep(1)                                                           # let the protobuf messages the time to get there

        msg = self.getFirstProtobufMessage()                                    # protobuf message before dnsdist lookup

        self.checkProtobufQueryConvertedToResponse(msg, dnsmessage_pb2.PBDNSMessage.TCP, response, '127.0.0.0')               

        self.checkProtobufTags(msg.response.tags, [ u"TestLabel1,TestData1", u"TestLabel2,TestData2", u"TestLabel3,TestData3", u"Query,123", u"found,no"])


        msg = self.getFirstProtobufMessage()                                    # protobuf message after dnsdist lookup

 
        self.checkProtobufResponse(msg, dnsmessage_pb2.PBDNSMessage.TCP, response, '127.0.0.0')

        self.checkProtobufTags(msg.response.tags, [ u"TestLabel1,TestData1", u"TestLabel2,TestData2", u"TestLabel3,TestData3", u"Response,456", u"found,no"])   # response

        self.assertEquals(len(msg.response.rrs), 1)
        for rr in msg.response.rrs:
            self.checkProtobufResponseRecord(rr, dns.rdataclass.IN, dns.rdatatype.A, name, 3600)
            self.assertEquals(socket.inet_ntop(socket.AF_INET, rr.rdata), '127.0.0.1')


    @classmethod
    def tearDownClass(cls):                                               # delete the test cdb file when finished
        super(TestNamedCache, cls).tearDownClass()
        delete_cdb()
