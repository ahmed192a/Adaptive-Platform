# ara::com – conformance against AUTOSAR R25-11

Scope: the legacy `ara::com` cluster (`src/ara/com`): SOME/IP message, SOME/IP-SD message,
entries/options, and the proxy/skeleton API. Audited on branch `feature/spec-com`.

## Sources and how far they could be verified

| Document | Used for | Verification level |
|---|---|---|
| [AUTOSAR_FO_PRS_SOMEIPProtocol (R25-11)](https://www.autosar.org/fileadmin/standards/R25-11/FO/AUTOSAR_FO_PRS_SOMEIPProtocol.pdf) | header, message types, session ID | **Verified** against the PDF text (IDs below are quoted from it). The return-code table body ([PRS_SOMEIP_00191]) and the payload serialization chapter were *not* in the retrievable text. |
| [AUTOSAR_FO_PRS_SOMEIPServiceDiscoveryProtocol (R25-11)](https://www.autosar.org/fileadmin/standards/R25-11/FO/AUTOSAR_FO_PRS_SOMEIPServiceDiscoveryProtocol.pdf) | SD header, entries, options, flags, reboot/session rules | **Verified**; the SD_PORT / multicast values (configuration chapter) were not retrievable. |
| [AUTOSAR_AP_SWS_CommunicationManagement (R24-11)](https://www.autosar.org/fileadmin/standards/R24-11/AP/AUTOSAR_AP_SWS_CommunicationManagement.pdf) | `ara::com` API names | **Names only.** The fetchable text contained the table of contents and traceability tables but not the chapter-8 signatures, and the R25-11 AP document was not reachable. API rows are therefore *by name* and carry no per-signature `SWS_CM_` IDs. |

Rule used: no requirement ID is cited unless it appeared in the retrieved text. "Unverified" means
the spec body could not be read, not that the code is believed wrong.

Status legend: **Implemented**, **Fixed** (deviation found and corrected in this branch, with a test),
**Partial**, **Deviation** (known, not changed), **Missing**, **Unverified**.

Tests: `Testing/COM` (`ctest -L com`, 20 tests). Expected byte strings are written out by hand from the
spec, independent of the implementation.

## SOME/IP message (`SOMEIP/Message.{h,cpp}`)

| Requirement | Status | Notes |
|---|---|---|
| Field order of the 16 byte header [PRS_SOMEIP_00031] | Implemented | |
| All fields big endian [PRS_SOMEIP_00368] | **Fixed** | Header was a packed bit-field struct memcpy'd in host (little-endian) order. Now encoded/decoded explicitly. |
| Length = bytes from Request ID to end [PRS_SOMEIP_00042] | **Fixed** | Started at 16 and accumulated on every `SetPayload`; now `8 + payload` and replaced, not added. `Message::EmptyLength` exposes the constant; `SD_s/skeleton.cpp` (`Length() > 16`) updated to use it. |
| Protocol Version = 1 [PRS_SOMEIP_00051] | **Fixed** / Partial | All senders used 2, 3 or 5; they now use `kProtocolVersion`. `ValidateHeader()` returns `E_WRONG_PROTOCOL_VERSION`, but the existing dispatchers do not call it yet. |
| Interface Version = service major version [PRS_SOMEIP_00053] | Deviation | Senders hard-code 7 (and 4 for notifications). `InterfaceVersion()` returned the *protocol* version – **fixed**. |
| Message types and values [PRS_SOMEIP_00055] | **Fixed** | `TP_RESPONSE`/`TP_ERROR` were 0x23/0x24, now 0xA0/0xA1. |
| TP flag / segmentation [PRS_SOMEIP_00367] | Missing | Only the enum values exist; no SOME/IP-TP segmentation or header. |
| Return code table [PRS_SOMEIP_00191] | Unverified | Values 0x00–0x0F match the sequence of earlier PRS revisions; the R25-11 table body was not retrievable. |
| Request → E_OK; ERROR ≠ E_OK [PRS_SOMEIP_00757] | Partial | Not enforced by the constructors. |
| Session ID range 1..0xFFFF, wrap to 1 [PRS_SOMEIP_00933, 00521] | **Fixed** | Wrapped at 0xFF. |
| Session ID 0 when session handling inactive / for REQUEST_NO_RETURN [PRS_SOMEIP_00932, 00939] | Missing | Not modelled. |
| Session ID incremented per call, response matching [PRS_SOMEIP_00533, 00739] | Deviation | Proxies/skeletons use constant `Request_ID{5, 6}`; no matching of responses to requests. |
| Method ID 0x0000–0x7FFF, event ID 0x8xxx (recommendation, [PRS_SOMEIP_00245]) | Implemented | `check_Methode_ID()`. One proxy call site masks the method id with `0x7F`, limiting methods to 128 per service. |
| Consumers of the Length field (`sm::triggerin/triggerinout/triggerout`, `ucm::PackageManagementSkeleton`) | **Fixed** | They tested `Length() > 16` (the old, wrong "empty message" value) to tell a field SET from a GET. With the spec value 8 + payload a one byte SET was treated as GET and silently dropped, so SM never saw `UCM_STATE_INITIALIZED` and the platform stalled. They now compare against `Message::EmptyLength`. Found by the end-to-end platform scenario, not by the unit tests. |
| Malformed input handling | **Fixed** | `Deserialize` memcpy'd without any size check. Now `TryDeserialize` (non-throwing) / `Deserialize` (throws `std::invalid_argument`) reject short buffers and Length/buffer mismatches. |
| Payload serialization (`serializer.h`, `deserializer.h`, `marshal.h`) | Deviation (not verified) | Values are `memcpy`'d in host byte order; strings/vectors use a host-order `int` length prefix. The SOME/IP serialization chapter was not retrievable, and changing it alters every service payload, so it was left unchanged. Likely not wire-compatible with other SOME/IP stacks. |
| E2E protection | Missing | Only the return-code enumerators exist. |

## SOME/IP-SD message (`SOMEIP/SomeipSDMessage.*`, `entry/`, `option/`)

| Requirement | Status | Notes |
|---|---|---|
| Message ID 0xFFFF8100, client ID 0, versions 1/1, NOTIFICATION, E_OK [PRS_SOMEIPSD_00151, 00152, 00154, 00161–00164] | Implemented | |
| Length field [PRS_SOMEIPSD_00153] | **Fixed** | `Length()` used a wrong formula and the serialized header still carried the payload-less value (16). Now `8 + 4 + (4 + entries) + (4 + options)` and written to the wire. |
| Flags byte: Reboot (bit 7), Unicast always 1, other bits 0; 24 reserved bits 0 [PRS_SOMEIPSD_00254, 00259, 00540, 00702, 00261] | **Fixed** | Code sent 0xE0/0x60, setting the *Explicit Initial Data Control* bit that R25-11 removed (change history: removed in R21-11). Now 0xC0/0x40. Receivers ignore undefined bits. |
| Session ID starts at 1, wraps to 1, never 0; Reboot flag cleared on first wrap [PRS_SOMEIPSD_00157–00159, 00255] | **Fixed** | Wrap point was 0xFF (shared `Message` bug). |
| Per-peer unicast/multicast session counters and reboot detection on receive [PRS_SOMEIPSD_00160, 00256, 00258, 00631] | Missing | One counter per message object; no receiver-side reboot detection. |
| Entries and options arrays with uint32 lengths [PRS_SOMEIPSD_00262, 00264, 00265] | **Fixed** | Deserializer had no bounds checks. `Deserialize` now returns `bool`, validates every length, and leaves the object unchanged on failure. |
| Service entries: Find 0x00, Offer 0x01, StopOffer = Offer with TTL 0; TTL 0xFFFFFF = until reboot [PRS_SOMEIPSD_00268, 00356, 00364] | Implemented | |
| Find TTL is ignored [PRS_SOMEIPSD_00351] | Deviation (minor) | `CreateFindServiceEntry` rejects TTL 0. |
| Eventgroup entries: Subscribe/StopSubscribe 0x06, Ack/Nack 0x07; Reserved(12)=0, Counter(4), Eventgroup ID [PRS_SOMEIPSD_00270, 00386, 00389] | **Fixed** / Partial | The 16-bit field was written as 0x0080, putting a bit into *Reserved*. Now 0x0000. The Counter is not exposed (always 0). |
| Option runs: index of first option, count; empty run has index 0 [PRS_SOMEIPSD_00341–00343, 00833, 00834] | **Fixed** | The running index was advanced twice for every option (wrong indices from the second entry on), and an empty run carried the running index instead of 0. Receive side: out-of-range runs are ignored instead of reading past the array. |
| Option header: Length (excl. length and type), Type, Discardable flag = MSB [PRS_SOMEIPSD_00273, 00275] | **Fixed** | Discardable was written/read as bit 0. |
| IPv4 endpoint 0x04 / multicast 0x14 / SD endpoint 0x24, length 0x0009 [PRS_SOMEIPSD_00307, 00326, 00552] | Implemented | Layout checked byte by byte. |
| Load-balancing option 0x02, length 0x0005 [PRS_SOMEIPSD_00544] | Partial | Can be serialized; not parsed on receive. |
| Configuration option 0x01 | Partial | Type exists; no payload class, not parsed. |
| IPv6 endpoint/multicast/SD options 0x06 / 0x16 / 0x26 | Missing | Enumerators only. |
| Unknown option / entry types on receive | **Fixed** | Unknown options produced a `nullptr` that was dereferenced; unknown entries were stored as `nullptr`. Both are now skipped (options by their on-wire length). |
| SD transport: UDP, SD_PORT, SD_MULTICAST_IP [PRS_SOMEIPSD_00600–00603] | Deviation (by design) | Service discovery is a local daemon reached over TCP on 127.0.0.1 with a 4-byte host-order size prefix; there is no multicast SD. SD_PORT/multicast values were not retrievable. |
| SD phases (initial wait, repetition, main), TTL expiry | Partial | `TtlTimer` implements expiry (unit is seconds – comment corrected); no repetition/initial-wait phases. |

## Proxy / skeleton API (`proxy_skeleton/`) – by name, R24-11 chapter 8

These rows compare names and shapes only; signatures could not be read from the available text.

| API element (SWS_CommunicationManagement) | Status | Notes |
|---|---|---|
| `ComErrc`, `ComErrorDomain`, `ComException` | Missing | No `ara::com` error domain; errors are `std::cout` messages and return codes. |
| `SamplePtr`, `SampleAllocateePtr`, `SampleContainer` | Partial | `SampleAllocateePtr` (= `unique_ptr`) and `SampleContainer` (= `vector<Sample>`, not of `SamplePtr`) are declared but unused by the event classes; `SamplePtr` is missing. |
| `ServiceHandleContainer`, `FindServiceHandler`, `FindServiceHandle` | Implemented / Unverified | Aliases and a handle struct with `==`/`<` exist. |
| `FindService(InstanceIdentifier)`, `StartFindService`, `StopFindService` | Deviation / Missing | Only a blocking `ServiceProxy::FindService(FindServiceHandle)` that retries forever. Now also survives malformed SD answers instead of indexing `entries[0]` blindly. |
| `InstanceIdentifier` | Partial | Constructors, `toString`, `operator==`/`<` present; no `Create`. |
| `ProxyEvent`: `Subscribe(maxSampleCount)`, `Unsubscribe`, `GetNewSamples`, `SetReceiveHandler`/`UnsetReceiveHandler`, `GetFreeSampleCount`, `GetSubscriptionState`, `SetSubscriptionStateChangeHandler` | Deviation | `Event<T>` has `Subscribe()` (no count), `UnSubscribe()`, `handlecall()` and `get_value()` – a single latest value, no sample queue, no handlers. |
| `SubscriptionState` | Implemented | `kSubscribed/kNotSubscribed/kSubscriptionPending` declared; not returned by any API. |
| `ProxyField` `Get`/`Set` | Partial | `Get()`/`Set()` exist and are synchronous (spec: `Future`-returning). |
| `ProxyMethod::operator()` returning a `Future<Output>` | Deviation | Synchronous `SendRequest<R>(method_id, args...)` over a new TCP connection per call. |
| Skeleton `OfferService` / `StopOfferService` | Implemented | |
| Skeleton `ProcessNextMethodCall`, `SkeletonEvent::Allocate/Send` | Deviation / Missing | Method dispatch is socket-thread driven; events use `Event::notify()`. |
| E2E: `ProfileCheckStatus`, `SMState`, `ComE2EErrc`, `TransportFaultCondition(Handler)` | Missing | |

## Summary

* Verified items fixed: 17 (header byte order, Length, protocol version, interface-version getter, message
  types, session wrap, malformed handling; SD Length, flags, session wrap, bounds, eventgroup reserved bits,
  option indices, discardable flag, unknown option/entry handling).
* Remaining gaps with the largest impact: payload serialization format, `Request_ID`/session handling and
  response matching, SOME/IP-TP, SD over UDP multicast with reboot detection, the whole
  chapter-8 API surface (sample queues, handlers, `ComErrc`, async `Future`s), and E2E.
* To close the "Unverified" rows, the R25-11 AP communication-management PDF (chapter 8 text) and the SOME/IP
  return-code / serialization chapters need to be supplied.
