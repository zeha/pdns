// !! This file has been generated by dnsdist-rules-generator.py, do not edit by hand!!
luaCtx.writeFunction("AllowAction", []() {
  return dnsdist::actions::getAllowAction();
});
luaCtx.writeFunction("DelayAction", [](uint32_t msec) {
  return dnsdist::actions::getDelayAction(msec);
});
luaCtx.writeFunction("DropAction", []() {
  return dnsdist::actions::getDropAction();
});
luaCtx.writeFunction("SetEDNSOptionAction", [](uint32_t code, std::string data) {
  return dnsdist::actions::getSetEDNSOptionAction(code, data);
});
luaCtx.writeFunction("LogAction", [](boost::optional<std::string> file_name, boost::optional<bool> binary, boost::optional<bool> append, boost::optional<bool> buffered, boost::optional<bool> verbose_only, boost::optional<bool> include_timestamp) {
  return dnsdist::actions::getLogAction(file_name ? *file_name : "", binary ? *binary : true, append ? *append : false, buffered ? *buffered : false, verbose_only ? *verbose_only : true, include_timestamp ? *include_timestamp : false);
});
luaCtx.writeFunction("LuaFFIPerThreadAction", [](std::string code) {
  return dnsdist::actions::getLuaFFIPerThreadAction(code);
});
luaCtx.writeFunction("NoneAction", []() {
  return dnsdist::actions::getNoneAction();
});
luaCtx.writeFunction("PoolAction", [](std::string pool_name, boost::optional<bool> stop_processing) {
  return dnsdist::actions::getPoolAction(pool_name, stop_processing ? *stop_processing : true);
});
luaCtx.writeFunction("QPSAction", [](uint32_t limit) {
  return dnsdist::actions::getQPSAction(limit);
});
luaCtx.writeFunction("QPSPoolAction", [](uint32_t limit, std::string pool_name, boost::optional<bool> stop_processing) {
  return dnsdist::actions::getQPSPoolAction(limit, pool_name, stop_processing ? *stop_processing : true);
});
luaCtx.writeFunction("SetAdditionalProxyProtocolValueAction", [](uint8_t proxy_type, std::string value) {
  return dnsdist::actions::getSetAdditionalProxyProtocolValueAction(proxy_type, value);
});
luaCtx.writeFunction("SetDisableECSAction", []() {
  return dnsdist::actions::getSetDisableECSAction();
});
luaCtx.writeFunction("SetDisableValidationAction", []() {
  return dnsdist::actions::getSetDisableValidationAction();
});
luaCtx.writeFunction("SetECSOverrideAction", [](bool override_existing) {
  return dnsdist::actions::getSetECSOverrideAction(override_existing);
});
luaCtx.writeFunction("SetECSPrefixLengthAction", [](uint16_t ipv4, uint16_t ipv6) {
  return dnsdist::actions::getSetECSPrefixLengthAction(ipv4, ipv6);
});
luaCtx.writeFunction("SetExtendedDNSErrorAction", [](uint16_t info_code, boost::optional<std::string> extra_text) {
  return dnsdist::actions::getSetExtendedDNSErrorAction(info_code, extra_text ? *extra_text : "");
});
luaCtx.writeFunction("SetMacAddrAction", [](uint32_t code) {
  return dnsdist::actions::getSetMacAddrAction(code);
});
luaCtx.writeFunction("SetNoRecurseAction", []() {
  return dnsdist::actions::getSetNoRecurseAction();
});
luaCtx.writeFunction("SetSkipCacheAction", []() {
  return dnsdist::actions::getSetSkipCacheAction();
});
luaCtx.writeFunction("SetTagAction", [](std::string tag, std::string value) {
  return dnsdist::actions::getSetTagAction(tag, value);
});
luaCtx.writeFunction("SetTempFailureCacheTTLAction", [](uint32_t ttl) {
  return dnsdist::actions::getSetTempFailureCacheTTLAction(ttl);
});
luaCtx.writeFunction("SNMPTrapAction", [](boost::optional<std::string> reason) {
  return dnsdist::actions::getSNMPTrapAction(reason ? *reason : "");
});
luaCtx.writeFunction("TCAction", []() {
  return dnsdist::actions::getTCAction();
});
