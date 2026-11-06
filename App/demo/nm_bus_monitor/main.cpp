// nm_bus_monitor - three simulated ECUs on a virtual CAN-like bus running the AUTOSAR NM state
// machine. Simulated time (ManualClock) makes the run instant and reproducible; every state
// change is printed as a timeline row, followed by a summary of who was awake when.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ara/nm/nm_channel.h"

namespace {
using ara::nm::Millis;
using ara::nm::NmChannel;
using ara::nm::NmConfig;
using ara::nm::NmPdu;
using ara::nm::NmState;

struct Ecu {
  std::string name;
  std::unique_ptr<NmChannel> ch;
  Millis awake{0};
  Millis lastChange{0};
  bool isAwake{false};
};

std::shared_ptr<ara::nm::ManualClock> gClock;
std::vector<std::unique_ptr<Ecu>> gEcus;
std::size_t gPdus = 0;

char const *Glyph(NmState s) {
  switch (s) {
    case NmState::kBusSleep: return "BusSleep       ";
    case NmState::kPrepareBusSleep: return "PrepareBusSleep";
    case NmState::kReadySleep: return "ReadySleep     ";
    case NmState::kNormalOperation: return "NormalOperation";
    case NmState::kRepeatMessage: return "RepeatMessage  ";
  }
  return "?";
}

void Log(char const *who, std::string const &msg) {
  std::printf("t=%5lld ms  %-8s %s\n", static_cast<long long>(gClock->Now().count()), who, msg.c_str());
}

void Add(std::string name, std::uint8_t node, std::uint32_t pnMask) {
  NmConfig cfg;
  cfg.nodeId = node;
  cfg.pnFilterMask = pnMask;
  cfg.repeatMessageTime = Millis(600);
  cfg.messageCycleTime = Millis(200);
  cfg.nmTimeout = Millis(800);
  cfg.waitBusSleepTime = Millis(500);
  cfg.pnResetTime = Millis(1200);
  auto r = NmChannel::Create(name, cfg, gClock);
  if (!r.HasValue()) {
    std::printf("cannot create %s\n", name.c_str());
    return;
  }
  auto e = std::make_unique<Ecu>();
  e->name = std::move(name);
  e->ch = std::move(r).Value();
  Ecu *raw = e.get();
  raw->ch->SetStateChangeHandler([raw](NmState from, NmState to) {
    Log(raw->name.c_str(), std::string(Glyph(from)) + " -> " + Glyph(to));
    Millis now = gClock->Now();
    if (raw->isAwake) raw->awake += now - raw->lastChange;
    raw->isAwake = to != NmState::kBusSleep;
    raw->lastChange = now;
  });
  raw->ch->SetTxHandler([raw](NmPdu const &p) {
    ++gPdus;
    for (auto &o : gEcus)
      if (o.get() != raw) o->ch->OnPduReceived(p);
  });
  gEcus.push_back(std::move(e));
}

void Run(Millis d) {
  for (Millis t{0}; t < d; t += Millis(10)) {
    gClock->Advance(Millis(10));
    for (auto &e : gEcus) e->ch->Process();
  }
}

Ecu &Find(char const *n) {
  for (auto &e : gEcus)
    if (e->name == n) return *e;
  return *gEcus.front();
}
}  // namespace

int main() {
  gClock = std::make_shared<ara::nm::ManualClock>();
  std::puts("== AUTOSAR NM bus monitor: 3 ECUs, partial networks PN0 (comfort) and PN1 (adas) ==");
  Add("Gateway", 1, 0x3);
  Add("Door", 2, 0x1);
  Add("Camera", 3, 0x2);

  std::puts("\n-- scene 1: Door ECU requests the network (the others stay in BusSleep: no wake-up source, no partial-network info)");
  (void)Find("Door").ch->Request();
  Run(Millis(1500));

  std::puts("\n-- scene 2: Camera requests partial network PN1 (wakes the Gateway, which serves PN1); Door releases");
  (void)Find("Camera").ch->Request();
  (void)Find("Camera").ch->RequestPartialNetwork(1);
  (void)Find("Door").ch->Release();
  Run(Millis(1500));

  std::puts("\n-- scene 3: everybody releases -> bus goes to sleep");
  (void)Find("Camera").ch->ReleasePartialNetwork(1);
  (void)Find("Camera").ch->Release();
  Run(Millis(4000));

  std::puts("\n== summary ==");
  for (auto &e : gEcus) {
    Millis a = e->awake;
    if (e->isAwake) a += gClock->Now() - e->lastChange;
    std::printf("%-8s awake %5lld ms of %lld ms, final state %s\n", e->name.c_str(),
                static_cast<long long>(a.count()), static_cast<long long>(gClock->Now().count()),
                ara::nm::ToString(e->ch->GetState()));
  }
  std::printf("NM PDUs on the bus: %zu\n", gPdus);
  return 0;
}
