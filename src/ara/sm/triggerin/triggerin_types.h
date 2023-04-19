#ifndef SM_TRIGGERIN_TYPES_H_
#define SM_TRIGGERIN_TYPES_H_

namespace ara::sm::triggerin {
using TriggerInType = int;
// define enum for ucm states
enum class UCM_State {
  UCM_STATE_UNKNOWN,
  UCM_STATE_INITIALIZED,
  UCM_STATE_STARTED,
  UCM_STATE_STOPPED,
  UCM_STATE_ERROR
};

enum class OTA_State {
  OTA_STATE_UNKNOWN,
  OTA_STATE_INITIALIZED,
  OTA_STATE_STARTED,
  OTA_STATE_STOPPED,
  OTA_STATE_ERROR
};
} // namespace ara::sm::triggerin

#endif