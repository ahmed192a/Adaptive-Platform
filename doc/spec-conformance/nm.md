# ara::nm - conformance to AUTOSAR AP R25-11 (SWS_NetworkManagement)

Source: *AUTOSAR_AP_SWS_NetworkManagement.pdf*, R25-11 (requirement prefix `SWS_ANM_`).
Status values: **Implemented**, **Partial**, **Missing**, **Deviation** (implemented differently),
**Unverified** (the retrievable SWS text does not settle it - nothing is guessed).
Every row below is covered by `Testing/nm` unless noted.

## Architecture note (the main deviation, now fixed)

The SWS defines `ara::nm::NetworkHandle` as the *State Management facing* API with only two
states, `kNoCom` / `kFullCom` (`SWS_ANM_01000`, `01358`). The first implementation used that name for
the NM state-machine engine. The engine is now `ara::nm::NmChannel` (`nm_channel.h`), and
`ara::nm::NetworkHandle` (`network_handle.h`) is the spec facade on top of it. A handle is bound to a
channel through its port `InstanceSpecifier`: `NetworkManager::BindPort(specifier, network)`.
An unbound handle reports `kServiceNotAvailable`, like a lost daemon connection.

## Public API (NetworkHandle, error domain)

| Requirement / API | Status | Notes |
|---|---|---|
| SWS_ANM_01000 `NetworkHandle`, namespace `ara::nm` | Implemented | "Use by State Management only" is not enforced. |
| SWS_ANM_01001 `NetworkHandle(const InstanceSpecifier&) noexcept` | Implemented | Binding to the NM daemon is modelled by `NetworkManager::BindPort`. |
| SWS_ANM_01002-01006 dtor, move ctor/assign, copy deleted | Implemented | Observers are removed on destruction; moved-from handles must not be used. |
| SWS_ANM_01007 `GetNetworkState()` | Implemented | `kFullCom` in Repeat Message / Normal Operation / Ready Sleep, else `kNoCom` (00029, 00083). |
| SWS_ANM_01008 `GetNetworkRequestedState()` | Implemented | |
| SWS_ANM_01009 `SetNetworkRequestedState(NetworkStateType)` | Implemented | `kNoCom` without a prior request is a no-op (idempotent). |
| SWS_ANM_01010, 01012 `Register...ChangeNotifier(notifier)` | Implemented | One notifier per kind, overwrite on re-register, empty -> `kInvalidHandler`. |
| SWS_ANM_01011, 01013 `Unregister...` | Implemented | |
| SWS_ANM_01014, 01015 overloads with `ExecutorT&&` | **Missing** | Needs the ara::core executor concept; notifiers run in the thread driving the channel. SWS_ANM_00086 is therefore also Missing. |
| SWS_ANM_01020 `NetworkStateChangeNotifier` | Implemented | `std::function<void(const NetworkStateType&)>`. |
| SWS_ANM_01358 `NetworkStateType {kNoCom=0, kFullCom=1}` | Implemented | |
| SWS_ANM_01100 `NmException` | Implemented | |
| SWS_ANM_01101 `NmErrorDomain` (+01103/01104 `Errc`, `Exception` aliases, 01105-01108) | Implemented | Aliases added in this audit. |
| SWS_ANM_01109 `GetNmDomain()`, 01110 `MakeErrorCode(NmErrc, SupportDataType)` | Implemented | `MakeErrorCode` has an extra defaulted `message` parameter (compatible). |
| SWS_ANM_01111 `NmErrc {kServiceNotAvailable=1, kInvalidHandler=2}` | Implemented | Values corrected (they were different before). |
| Extra codes `kUnknownNetwork`... `kInvalidConfig` (0x100+) | Deviation | Implementation extensions in the NM domain, placed outside the normative range. |
| NmErrorDomain id / name | **Unverified** | The SWS defers to SWS_CORE_90023, which the retrievable Core text (R21-11) does not contain beyond Future `0x8000'0000'0000'0013` and Core `...0014`. `0x8000000000000501` is an implementation choice; the former clash with ara::tsync is resolved (tsync = `...0502`). |
| SWS_ANM_CONSTR_00001 (no `NetworkManagementPortInterface.namespace`) | n/a | Model constraint, nothing in code. |

## Network state semantics

| Requirement | Status | Notes |
|---|---|---|
| SWS_ANM_00083 `GetNetworkState` lowest-wins over PNCs/VLANs/channels | Partial | One channel per port; no aggregation over several channels/PNCs. |
| SWS_ANM_00084 `SetNetworkRequestedState(kFullCom)` highest-wins | Partial | Same limitation. |
| SWS_ANM_00029 Bus-Sleep reports `kNoCom` | Implemented | Prepare Bus-Sleep also maps to `kNoCom`. |
| SWS_ANM_00088 default state after start is NO_COM | Implemented | |
| SWS_ANM_00090 on SIGTERM withdraw requests, `GetNetworkState` -> `kNoCom` | Partial | `NetworkManager::WithdrawAll()` / `NmChannel::Withdraw()` withdraw all requests; no signal hook and the state follows the normal timers (Ready Sleep -> ... -> Bus-Sleep) instead of jumping to `kNoCom`. |

## Operational state machine (per channel)

| Requirement | Status | Notes |
|---|---|---|
| SWS_ANM_00005 Network Mode sub-states | Implemented | |
| SWS_ANM_00006 Bus-Sleep/Prepare Bus-Sleep -> Network Mode defaults to Repeat Message | Implemented | |
| SWS_ANM_00007-00009 NM-Timeout timer: started on entering Network Mode, restarted on NM Rx **and Tx** | Implemented | Tx restart added in this audit. |
| SWS_ANM_00013 `nmRepeatMessageTime` | Implemented | |
| SWS_ANM_00014 / 00015 Repeat Message -> Normal Operation if requested / Ready Sleep if released | Implemented | |
| SWS_ANM_00018 Normal Operation -> Ready Sleep on release | Implemented | |
| SWS_ANM_00019, 00023 Repeat Message Request bit received -> Repeat Message | Implemented | |
| SWS_ANM_00020 Ready Sleep stops NM transmission | Implemented | Test `ReadySleepDoesNotTransmit`. |
| SWS_ANM_00021 Ready Sleep -> Prepare Bus-Sleep on NM-Timeout | Implemented | |
| SWS_ANM_00022 Ready Sleep -> Normal Operation when requested | Implemented | |
| SWS_ANM_00024 Prepare Bus-Sleep -> Bus-Sleep after `nmWaitBusSleepTime` | Implemented | |
| SWS_ANM_00025 NM message in Prepare Bus-Sleep -> Network Mode | Implemented | |
| SWS_ANM_00062 / 91002 operational state machine | Implemented | Flattened into one enum (`NmState`). |
| `nmMsgCycleTime` | Implemented | |
| `nmMsgCycleOffset` | **Missing** | First message is sent immediately on entering Repeat Message. |
| `nmImmediateNmCycleTime`, `nmImmediateNmTransmissions` | Implemented | New: `NmConfig::immediateNmCycleTime` / `immediateNmTransmissions` (default 0 = disabled). |

## Partial networking

| Requirement | Status | Notes |
|---|---|---|
| SWS_ANM_00051 PNI=1 processed only with `nmPncParticipation` | Partial | Participation = `pnFilterMask != 0`. A non-participating node still treats a PNI=1 message as an ordinary NM message (wakes, restarts timer); whether the SWS wants it ignored entirely is **Unverified** from the extract. |
| SWS_ANM_00052 PNI=0 messages still processed | Implemented | |
| SWS_ANM_00053, 00056 PN reset timer, must exceed `nmMsgCycleTime` | Implemented | `NmChannel::Create` -> `kInvalidConfig` otherwise (when PN is active). |
| SWS_ANM_00054 PNI transmitted when participating | Implemented | |
| SWS_ANM_00055 PN bit vector located by `pncVectorOffset`/`pncVectorLength` | Deviation | Fixed layout: bytes 2..5, little endian, 32 PNs. |
| SWS_ANM_00081, 00089 drop messages with no served PN bit unless `allNmMessagesKeepAwake` | Implemented | New: `NmConfig::allNmMessagesKeepAwake` (default false). Dropped messages neither wake the node nor restart the timer. |

## Message format and Control Bit Vector

| Requirement | Status | Notes |
|---|---|---|
| SWS_ANM_00033, 00034 `nmNidPosition`, `nmNodeId` | Deviation | Node id is always byte 0. |
| SWS_ANM_00035 `nmCbvPosition` | Deviation | CBV is always byte 1. |
| SWS_ANM_00037 transmitted Repeat Message Request bit always 0 | Implemented | **Fixed**: the engine used to set RMR while in Repeat Message. |
| SWS_ANM_00038, 00071, 00091 Active Wakeup / NM Coordinator Sleep Ready / PNL bits always 0 on Tx | Implemented | Those bits are reserved (written 0, ignored on Rx). |
| SWS_ANM_00040, 00095 `nmUserDataLength` | Deviation | Fixed 2 user-data bytes (8 byte PDU). |
| CBV bit positions (RMR bit 0, PNI bit 4 here) | **Unverified** | The AP text defers to the classic NM layout; classic CanNm is believed to use bit 6 for PNI, but that is not confirmed by the retrieved text. |

## Known gaps (not implemented)

Executor overloads (01014/01015, 00086); multi-channel/PNC aggregation (00083/00084); signal-driven
shutdown (00090); `nmMsgCycleOffset`; configurable PDU layout (00033-00035, 00040, 00055);
NM coordinator functions; bus drivers (the engine only produces/consumes PDU objects).
