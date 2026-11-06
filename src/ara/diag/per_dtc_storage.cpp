#include "ara/diag/per_dtc_storage.h"

#include <cstdio>
#include <sstream>

namespace ara::diag {
namespace {
std::string Hex(ByteVector const &v) {
  if (v.empty()) return "-";
  std::string s;
  char b[3];
  for (auto x : v) {
    std::snprintf(b, sizeof b, "%02X", x);
    s += b;
  }
  return s;
}

bool FromHex(std::string const &s, ByteVector &out) {
  out.clear();
  if (s == "-") return true;
  if (s.size() % 2 != 0) return false;
  for (std::size_t i = 0; i < s.size(); i += 2) {
    unsigned v = 0;
    if (std::sscanf(s.c_str() + i, "%2x", &v) != 1) return false;
    out.push_back(static_cast<std::uint8_t>(v));
  }
  return true;
}
} // namespace

// Line format:  <dtc hex6> <status hex2> <occurrences dec> [s<rec>=<hex>] [e<rec>=<hex>] ...   (lines joined by '\n')
std::string EncodeDtcMemory(DtcMemory const &memory) {
  std::ostringstream out;
  char b[64];
  for (auto const &[dtc, rec] : memory) {
    std::snprintf(b, sizeof b, "%06X %02X %u", dtc & 0xFFFFFFU, rec.status, static_cast<unsigned>(rec.occurrences));
    out << b;
    for (auto const &[n, d] : rec.snapshots) out << " s" << static_cast<unsigned>(n) << '=' << Hex(d);
    for (auto const &[n, d] : rec.extendedData) out << " e" << static_cast<unsigned>(n) << '=' << Hex(d);
    out << '\n';
  }
  return out.str();
}

DtcMemory DecodeDtcMemory(std::string const &text) {
  DtcMemory mem;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string dtcS, stS, occS;
    if (!(ls >> dtcS >> stS >> occS)) continue; // malformed line: skip
    unsigned dtc = 0, st = 0, occ = 0;
    if (std::sscanf(dtcS.c_str(), "%x", &dtc) != 1 || std::sscanf(stS.c_str(), "%x", &st) != 1 ||
        std::sscanf(occS.c_str(), "%u", &occ) != 1 || st > 0xFF || occ > 0xFF || dtc > 0xFFFFFF)
      continue;
    DtcRecord rec;
    rec.status = static_cast<std::uint8_t>(st);
    rec.occurrences = static_cast<std::uint8_t>(occ);
    std::string tok;
    while (ls >> tok) {
      auto eq = tok.find('=');
      if (tok.size() < 4 || eq == std::string::npos || (tok[0] != 's' && tok[0] != 'e')) continue;
      unsigned num = 0;
      if (std::sscanf(tok.substr(1, eq - 1).c_str(), "%u", &num) != 1 || num > 0xFF) continue;
      ByteVector data;
      if (!FromHex(tok.substr(eq + 1), data)) continue;
      (tok[0] == 's' ? rec.snapshots : rec.extendedData)[static_cast<std::uint8_t>(num)] = std::move(data);
    }
    mem[dtc] = std::move(rec);
  }
  return mem;
}

DtcStorage MakePerDtcStorage(ara::per::SharedHandle<ara::per::KeyValueStorage> kvs, std::string key) {
  DtcStorage s;
  s.load = [kvs, key] {
    auto v = kvs->GetValue<std::string>(key);
    return v.HasValue() ? DecodeDtcMemory(v.Value()) : DtcMemory{};
  };
  s.save = [kvs, key](DtcMemory const &m) {
    (void)kvs->SetValue(key, EncodeDtcMemory(m));
    (void)kvs->SyncToStorage();
  };
  return s;
}

} // namespace ara::diag
