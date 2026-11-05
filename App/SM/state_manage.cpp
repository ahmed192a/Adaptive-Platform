#include "state_manage.h"

using namespace ara::sm::server;

bool StateManager::isRunning = true;
void StateManager::init(void) {
  // ********* under discussion ******** ==> get our process id  ==> getPid();

  //  using the constructor and the preconstructor to append to the array
  //  check the implementation critiria of the function_group_state (if we
  //  should extract it manually in the SM)

  /** initial communication with EM **/
//   EM.GetInitialMachineStateTransitionResult();
}

// StateManager::StateManager() {
//   std::cout << "\n[SM] State Management intitializing..." << std::endl;
//   // print SM PID
//   std::cout << "\t[SM] PID: " << getpid() << std::endl;
//   // set hamdle function for user signal

//   client.ReportExecutionState(
//       ara::exec::ExecutionState::kRunning); // report execution state to the
//                                             // execution server

//   std::cout << std::endl << "[SM]creating execution client " << std::endl;
// }

void StateManager::run() {

  // std::cout<<"[SM] pthread0"<<std::endl;
  std::cout << std::endl;
  std::cout << "[SM SERVER] OPEN SOCKET ON " << std::endl;
  server_main_socket.OpenSocket(
      SERVER_PORT);                // open socket for methods handle (TCP)
  server_main_socket.BindServer(); // bind socket for methods handle (TCP)
  server_main_socket.ListenServer(
      MAX_QUEUE_CLIENTS); // listen to server socket (TCP)
  std::cout << std::endl;
  std::cout << "[SM] Offering services to the SD" << std::endl;
  std::cout << std::endl;

  UCM_triggerin_skeleton_ptr->OfferService(); // offering service to the SD
  std::cout << "Offering Service:\t UCM_triggerin" << std::endl;
  std::cout << "---------------------------------------------" << std::endl;
  std::cout << std::endl;
  OTA_triggerin_skeleton_ptr->OfferService(); // offering service to the SD
  std::cout << "Offering Service:\t OTA_triggerin" << std::endl;
  std::cout << "---------------------------------------------" << std::endl;
  std::cout << std::endl;
  std::cout << "[SM] Offered services to the SD" << std::endl;
  std::cout << "---------------------------------------------" << std::endl;
  std::cout << "---------------------------------------------" << std::endl;

  // Create Function group state for MachineFG to Running state
  FunctionGroupState::CtorToken token;
  token.fg_name = "MachineFG";
  token.c_state = "Running";
  FunctionGroupState FGS(std::move(token));
  std::cout << std::endl;
  std::cout << "[SM] Create Function group state for MachineFG to Running state"
            << std::endl;
  std::future<std::variant<std::monostate, ara::exec::ExecErrc>> _future =
      state_client.SetState(FGS);
  std::variant<std::monostate, ara::exec::ExecErrc> var = _future.get();
  if (var.index() == 0)
    std::cout << "[SM] state changed successfully" << std::endl;
  else
    std::cout << "[SM] state change failed" << std::endl;
  std::cout << std::endl;
  std::cout << "---------------------------------------------" << std::endl;
  std::cout << "---------------------------------------------" << std::endl;
  SM_State = State::STATE_INITIAL;

  while (isRunning) {
    Socket Sclient =
        server_main_socket.AcceptServer(); // accept client socket (TCP)
    std::cout << "[SM] Accepted client socket" << std::endl;
    // std::cout << "\t[SM]  accepted" << std::endl;

    std::vector<uint8_t> msg; // Payload message from client
    uint32_t msg_size;        // Size of message

    // Receive a payload from client containing the method name and parameters
    Sclient.Receive((void *)&msg_size,
                    sizeof(msg_size)); // recieve message size
    msg.resize(
        msg_size); //  resize the vector to allocate size of the coming message
    Sclient.Receive((void *)&msg[0], msg_size); //  recieve the message
    std::cout << "[SM] Received message from client" << std::endl;

    // deserialize message to SOMEIP_MESSAGE::Message object
    auto parsed_msg = ara::com::SOMEIP_MESSAGE::Message::TryDeserialize(msg);
    if (!parsed_msg) { // malformed datagram: drop the request
      Sclient.CloseSocket();
      continue;
    }
    ara::com::SOMEIP_MESSAGE::Message someip_msg = *parsed_msg;

    // check if message is a method call or field methods (SET, GET)
    if (someip_msg.MessageId().serivce_id ==
        UCM_triggerin_skeleton_ptr->GetServiceId()) {
      if (someip_msg.check_Methode_ID() == true) { // No methods in SM Services
        std::cout << "[SD] Method request from UCM_triggerin " << std::endl;
      } else {

        UCM_triggerin_skeleton_ptr->field_method_dispatch(someip_msg, Sclient);
      }
    } else if (someip_msg.MessageId().serivce_id ==
               OTA_triggerin_skeleton_ptr->GetServiceId()) {
      if (someip_msg.check_Methode_ID() == true) { // No methods in SM Services
        std::cout << "[SD] Method request from OTA_triggerin " << std::endl;
      } else {
        OTA_triggerin_skeleton_ptr->field_method_dispatch(someip_msg, Sclient);
      }
    } else {
      std::cout << "Unknown service id" << std::endl;
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::NoServiceHandler(
          someip_msg, Sclient);
    }

    // std::cout<<"[SM] Client socket closed"<<std::endl;
    std::cout << "[SM] Request finished" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;

    /*******************************          FSM
     * **************************************/
    if (SM_State == State::STATE_INITIAL &&
        UCM_triggerin_skeleton_ptr->trigger.get_event() ==
            ara::sm::triggerin::UCM_State::UCM_STATE_INITIALIZED) {
      FunctionGroupState::CtorToken token;
      token.fg_name = "FG_1";
      token.c_state = "on";
      FunctionGroupState FGS(std::move(token));
      // std::cout<<"[SM] FGS created "<<std::endl;
      std::cout << std::endl;
      std::cout << "[SM] Create Function group state for MachineFG to ON state"
                << std::endl;
      std::cout << "[SM] FGS created " << std::endl;
      std::future<std::variant<std::monostate, ara::exec::ExecErrc>> _future =
          state_client.SetState(FGS);
      std::variant<std::monostate, ara::exec::ExecErrc> var = _future.get();
      std::cout << "[SM] state changed" << std::endl;
      std::cout << "---------------------------------------------" << std::endl;
      std::cout << "---------------------------------------------" << std::endl;
      SM_State = State::STATE_RUNNING;
    } else if (SM_State == State::STATE_RUNNING &&
               OTA_triggerin_skeleton_ptr->trigger.get_event() ==
                   ara::sm::triggerin::OTA_State::OTA_STATE_INITIALIZED) {
      // used for OTA
      //  Comment the next code in this if condition to disable system shutdown
      //  and to run the system normally
      std::cout << "[SM] OTA_STATE_INITIALIZED" << std::endl;
      SM_State = State::STATE_UPDATE;
      shutdownSeq();
    }

    /*******************************        END OF FSM
     * **************************************/
  }
  std::cout << "[SM] terminating" << std::endl;
  client.ReportExecutionState(
      ara::exec::ExecutionState::kTerminating); // report execution state to the
                                                // execution server
  // UCM_triggerin_skeleton_ptr->StopOfferService();     // stop offering
  // service
  server_main_socket.CloseSocket(); // close server socket
}

void StateManager::shutdownSeq() {

  FunctionGroupState::CtorToken token;
  token.fg_name = "FG_1";
  token.c_state = "off";
  FunctionGroupState FGS(std::move(token));
  std::cout << "[SM] Create Function group state for FG1 to off state"
            << std::endl;

  std::future<std::variant<std::monostate, ara::exec::ExecErrc>> _future =
      state_client.SetState(FGS);
  std::variant<std::monostate, ara::exec::ExecErrc> var = _future.get();
  if (var.index() == 0)
    std::cout << "[SM] state changed successfully" << std::endl;
  else
    std::cout << "[SM] state change failed" << std::endl;

  //--------------------------------------------------------------------------------------------------
  token.fg_name = "MachineFG";
  token.c_state = "off";
  FunctionGroupState FGS1(std::move(token));
  std::cout << "[SM] Create Function group state for MachineFG to off state"
            << std::endl;

  std::future<std::variant<std::monostate, ara::exec::ExecErrc>> _future1 =
      state_client.SetState(FGS1);
  std::variant<std::monostate, ara::exec::ExecErrc> var1 = _future1.get();
  if (var1.index() == 0)
    std::cout << "[SM] state changed successfully" << std::endl;
  else
    std::cout << "[SM] state change failed" << std::endl;
}
