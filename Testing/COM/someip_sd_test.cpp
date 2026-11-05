// SOME/IP-SD conformance tests. Expected bytes are derived by hand from
// AUTOSAR_FO_PRS_SOMEIPServiceDiscoveryProtocol (R25-11).
#include "ara/com/SOMEIP/SomeipSDMessage.h"
#include "ara/com/SOMEIP/entry/eventgroup_entry.h"
#include "ara/com/SOMEIP/entry/service_entry.h"
#include "ara/com/SOMEIP/option/ipv4_endpoint_option.h"
#include <gtest/gtest.h>
#include <memory>

using namespace ara::com;
using SOMEIP_MESSAGE::sd::SomeIpSDMessage;
using Bytes = std::vector<uint8_t>;

namespace {
Bytes Cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (auto const &p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

void Be32(Bytes &b, uint32_t v) {
  b.push_back(v >> 24); b.push_back(v >> 16); b.push_back(v >> 8); b.push_back(v);
}

// Assemble a complete SD message around raw entry/option arrays.
Bytes BuildSd(uint8_t flags, Bytes const &entries, Bytes const &options) {
  Bytes tail;
  tail.insert(tail.end(), {flags, 0, 0, 0});
  Be32(tail, static_cast<uint32_t>(entries.size()));
  tail.insert(tail.end(), entries.begin(), entries.end());
  Be32(tail, static_cast<uint32_t>(options.size()));
  tail.insert(tail.end(), options.begin(), options.end());
  Bytes msg = {0xFF, 0xFF, 0x81, 0x00};
  Be32(msg, static_cast<uint32_t>(tail.size() + 8));
  msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x00});
  msg.insert(msg.end(), tail.begin(), tail.end());
  return msg;
}

void FreeEntries(SomeIpSDMessage const &m) {
  for (auto *e : m.Entries()) delete e;
}

std::shared_ptr<option::Option> Endpoint(uint8_t lastOctet, uint16_t port,
                                         bool discardable = false) {
  return std::make_shared<option::Ipv4EndpointOption>(
      option::Ipv4EndpointOption::CreateUnitcastEndpoint(
          discardable, helper::Ipv4Address(192, 168, 0, lastOctet),
          option::Layer4ProtocolType::Udp, port));
}

const Bytes kOfferNoOption = {0x01, 0x00, 0x00, 0x00, 0x12, 0x34, 0x00, 0x01,
                              0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05};
} // namespace

TEST(SomeIpSd, HeaderAndOfferEntryWithoutOptions) {
  auto offer = entry::ServiceEntry::CreateOfferServiceEntry(0x1234, 0x0001, 1, 5, 3);
  SomeIpSDMessage sd;
  sd.AddEntry(&offer);
  // Message ID 0xFFFF8100, Length = 8 + 4 + 4 + 16 + 4, Request ID 0/1,
  // versions 1/1, NOTIFICATION, E_OK [PRS_SOMEIPSD_00151-00164, 00153];
  // Reboot + Unicast flags set, reserved bits zero [PRS_SOMEIPSD_00254-00261].
  Bytes expected = Cat({{0xFF, 0xFF, 0x81, 0x00, 0x00, 0x00, 0x00, 0x24,
                         0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x00,
                         0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10},
                        kOfferNoOption,
                        {0x00, 0x00, 0x00, 0x00}});
  EXPECT_EQ(sd.Serializer(), expected);
  EXPECT_EQ(sd.Length(), 0x24u);
}

TEST(SomeIpSd, RebootFlagClearsAfterSessionWraps) {
  SomeIpSDMessage sd;
  EXPECT_EQ(sd.Serializer()[16], 0xC0);
  sd.SetSessionId(0xFFFF);
  EXPECT_TRUE(sd.IncrementSessionId());
  EXPECT_EQ(sd.SessionId(), 1);
  EXPECT_EQ(sd.Serializer()[16], 0x40);  // Unicast only [PRS_SOMEIPSD_00255]
  EXPECT_THROW(sd.SetSessionId(0), std::invalid_argument);
}

TEST(SomeIpSd, OptionIndicesAccumulateAcrossEntries) {
  auto o1 = Endpoint(10, 30500);
  auto o2 = Endpoint(11, 30501, /*discardable=*/true);
  auto a = entry::ServiceEntry::CreateOfferServiceEntry(0x1111, 1, 1, 0, 3);
  auto b = entry::ServiceEntry::CreateOfferServiceEntry(0x2222, 1, 1, 0, 3);
  auto c = entry::ServiceEntry::CreateOfferServiceEntry(0x3333, 1, 1, 0, 3);
  a.AddFirstOption(o1);
  b.AddFirstOption(o2);  // c references no options at all
  SomeIpSDMessage sd;
  sd.AddEntry(&a); sd.AddEntry(&b); sd.AddEntry(&c);
  auto bytes = sd.Serializer();
  size_t entries = 24;
  EXPECT_EQ(bytes[entries + 1], 0);          // a: first run starts at option 0
  EXPECT_EQ(bytes[entries + 3], 0x10);
  EXPECT_EQ(bytes[entries + 16 + 1], 1);     // b: second option => index 1
  EXPECT_EQ(bytes[entries + 16 + 3], 0x10);
  EXPECT_EQ(bytes[entries + 32 + 1], 0);     // c: empty run must carry index 0
  EXPECT_EQ(bytes[entries + 32 + 2], 0);
  EXPECT_EQ(bytes[entries + 32 + 3], 0x00);
}

TEST(SomeIpSd, Ipv4EndpointOptionLayoutAndDiscardableBit) {
  auto offer = entry::ServiceEntry::CreateOfferServiceEntry(0x1234, 1, 1, 0, 3);
  offer.AddFirstOption(Endpoint(10, 0x765C, true));
  SomeIpSDMessage sd;
  sd.AddEntry(&offer);
  auto bytes = sd.Serializer();
  Bytes option(bytes.end() - 12, bytes.end());
  // Length 0x0009, type 0x04, discardable flag = MSB, address, reserved,
  // L4 proto UDP, port [PRS_SOMEIPSD_00273, 00275, 00307].
  EXPECT_EQ(option, (Bytes{0x00, 0x09, 0x04, 0x80, 192, 168, 0, 10, 0x00, 0x11, 0x76, 0x5C}));
  EXPECT_EQ(Bytes(bytes.end() - 16, bytes.end() - 12), (Bytes{0, 0, 0, 12}));
}

TEST(SomeIpSd, SubscribeEntryHasZeroReservedAndCounter) {
  auto sub = entry::EventgroupEntry::CreateSubscribeEventEntry(0x1234, 1, 1, 0x0042);
  SomeIpSDMessage sd;
  sd.AddEntry(&sub);
  auto bytes = sd.Serializer();
  Bytes e(bytes.begin() + 24, bytes.begin() + 40);
  // type 0x06, TTL 0xFFFFFF, Reserved(12)=0 Counter(4)=0, Eventgroup ID.
  EXPECT_EQ(e, (Bytes{0x06, 0, 0, 0, 0x12, 0x34, 0x00, 0x01,
                      0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x42}));
}

TEST(SomeIpSd, RoundTripKeepsEntriesAndOptionRuns) {
  auto o1 = Endpoint(10, 30500);
  auto o2 = Endpoint(11, 30501, true);
  auto a = entry::ServiceEntry::CreateOfferServiceEntry(0x1111, 1, 1, 7, 3);
  auto b = entry::EventgroupEntry::CreateSubscribeEventEntry(0x2222, 1, 1, 9);
  a.AddFirstOption(o1);
  b.AddFirstOption(o2);
  SomeIpSDMessage out;
  out.AddEntry(&a); out.AddEntry(&b);

  SomeIpSDMessage in;
  ASSERT_TRUE(in.Deserialize(out.Serializer()));
  ASSERT_EQ(in.Entries().size(), 2u);
  EXPECT_EQ(in.Entries()[0]->Type(), entry::EntryType::Offering);
  EXPECT_EQ(in.Entries()[0]->ServiceId(), 0x1111);
  EXPECT_EQ(in.Entries()[0]->TTL(), 3u);
  EXPECT_EQ(in.Entries()[1]->Type(), entry::EntryType::Subscribing);
  EXPECT_EQ(in.Entries()[1]->TTL(), 0xFFFFFFu);
  ASSERT_EQ(in.Entries()[0]->FirstOptions().size(), 1u);
  ASSERT_EQ(in.Entries()[1]->FirstOptions().size(), 1u);
  auto p0 = std::dynamic_pointer_cast<option::Ipv4EndpointOption>(in.Entries()[0]->FirstOptions()[0]);
  auto p1 = std::dynamic_pointer_cast<option::Ipv4EndpointOption>(in.Entries()[1]->FirstOptions()[0]);
  ASSERT_TRUE(p0 && p1);
  EXPECT_EQ(p0->Port(), 30500);
  EXPECT_FALSE(p0->Discardable());
  EXPECT_EQ(p1->Port(), 30501);  // second entry resolves to the second option
  EXPECT_TRUE(p1->Discardable());
  FreeEntries(in);
}

TEST(SomeIpSd, UnknownOptionTypesAreSkippedNotDereferenced) {
  // Configuration option (0x01, not parsed) followed by an IPv4 endpoint.
  Bytes config = {0x00, 0x03, 0x01, 0x00, 0x00, 0x00};
  Bytes ipv4 = {0x00, 0x09, 0x04, 0x00, 10, 0, 0, 1, 0x00, 0x06, 0x1F, 0x90};
  Bytes entry = kOfferNoOption;
  entry[1] = 0; entry[3] = 0x20;  // first run: options 0..1
  SomeIpSDMessage in;
  ASSERT_TRUE(in.Deserialize(BuildSd(0xC0, entry, Cat({config, ipv4}))));
  ASSERT_EQ(in.Entries().size(), 1u);
  ASSERT_EQ(in.Entries()[0]->FirstOptions().size(), 1u);
  EXPECT_EQ(in.Entries()[0]->FirstOptions()[0]->Type(), option::OptionType::IPv4Endpoint);
  FreeEntries(in);
}

TEST(SomeIpSd, UnknownEntryTypesAreIgnored) {
  Bytes bogus = kOfferNoOption;
  bogus[0] = 0x55;
  SomeIpSDMessage in;
  ASSERT_TRUE(in.Deserialize(BuildSd(0xC0, Cat({bogus, kOfferNoOption}), {})));
  ASSERT_EQ(in.Entries().size(), 1u);
  EXPECT_EQ(in.Entries()[0]->Type(), entry::EntryType::Offering);
  FreeEntries(in);
}

TEST(SomeIpSd, OutOfRangeOptionRunsAreIgnored) {
  Bytes entry = kOfferNoOption;
  entry[1] = 5; entry[3] = 0x20;  // refers to options that do not exist
  SomeIpSDMessage in;
  ASSERT_TRUE(in.Deserialize(BuildSd(0xC0, entry, {})));
  ASSERT_EQ(in.Entries().size(), 1u);
  EXPECT_TRUE(in.Entries()[0]->FirstOptions().empty());
  FreeEntries(in);
}

TEST(SomeIpSd, MalformedMessagesAreRejectedWithoutSideEffects) {
  Bytes good = BuildSd(0xC0, kOfferNoOption, {});
  SomeIpSDMessage in;
  EXPECT_FALSE(in.Deserialize({}));
  EXPECT_FALSE(in.Deserialize(Bytes(good.begin(), good.begin() + 20)));
  EXPECT_FALSE(in.Deserialize(Bytes(good.begin(), good.end() - 1)));       // options length field cut
  Bytes badEntriesLen = good; badEntriesLen[23] = 0x11;                    // not a multiple of 16
  EXPECT_FALSE(in.Deserialize(badEntriesLen));
  Bytes hugeEntries = good; hugeEntries[20] = 0x7F;                        // beyond the buffer
  EXPECT_FALSE(in.Deserialize(hugeEntries));
  Bytes overrun = BuildSd(0xC0, kOfferNoOption, Bytes{0x00, 0x20, 0x04, 0x00});
  EXPECT_FALSE(in.Deserialize(overrun));                                   // option longer than array
  EXPECT_TRUE(in.Entries().empty());
}

TEST(SomeIpSd, ReservedFlagBitsAreIgnoredOnReceive) {
  // [PRS_SOMEIPSD_00702]: undefined bits are ignored by the receiver.
  SomeIpSDMessage in;
  ASSERT_TRUE(in.Deserialize(BuildSd(0x7F, kOfferNoOption, {})));
  EXPECT_EQ(in.Serializer()[16], 0x40);  // reboot bit was 0 -> not rebooted
  FreeEntries(in);
}
