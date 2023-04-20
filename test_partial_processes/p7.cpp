#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include "P7_Trace.h"
#include "P7_Telemetry.h"


class P7Wrapper {
public:
  P7Wrapper();
  ~P7Wrapper();

  void Connect(bool i_bConnect);
  void Telemetry_Enable(tUINT16 i_wId, tBOOL i_bEnable);

  void InitializeTrace(std::string channelName);
  void AddTraceMessage(const char *format, ...);
  void AddInfoMessage(const char *format, ...);
  void AddDebugMessage(const char *format, ...);
  void AddWarningMessage(const char *format, ...);
  void AddErrorMessage(const char *format, ...);
  void AddCriticalMessage(const char *format, ...);

private:
  static void Telemetry_Enable_Callback(void *i_pContext, tUINT16 i_wId,
                                        tBOOL i_bEnable) {
    auto pThis = (P7Wrapper *)i_pContext;
    pThis->Telemetry_Enable(i_wId, i_bEnable);
  }

  static void Connect_Callback(void *i_pContext, tBOOL i_bConnect) {
    auto pThis = (P7Wrapper *)i_pContext;
    pThis->Connect(i_bConnect);
  }

  IP7_Client *m_pClient{nullptr};
  IP7_Trace *m_pTrace{nullptr};
  IP7_Telemetry *m_pTelemetry{nullptr};
  IP7_Trace::hModule m_hModule;
  stTelemetry_Conf m_stConf;
};

P7Wrapper::P7Wrapper() {
  // create P7 client object
  m_pClient = P7_Create_Client(TM("/P7.Pool=32768"));

  if (nullptr == m_pClient) {
    throw std::runtime_error("Error creating P7 client object");
  }

  // initialize telemetry configuration structure
  m_stConf.pContext = this;
  m_stConf.pEnable_Callback = &P7Wrapper::Telemetry_Enable_Callback;
  m_stConf.pTimestamp_Callback = nullptr;
  m_stConf.qwTimestamp_Frequency = 0ULL;
  m_stConf.pConnect_Callback = &P7Wrapper::Connect_Callback;


}

P7Wrapper::~P7Wrapper() {
  if (m_pTelemetry) {
    m_pTelemetry->Release();
    m_pTelemetry = nullptr;
  }

  if (m_pTrace) {
    m_pTrace->Release();
    m_pTrace = nullptr;
  }

  if (m_pClient) {
    m_pClient->Release();
    m_pClient = nullptr;
  }
}

void P7Wrapper::Connect(bool i_bConnect) {
  // called when P7 client connects or disconnects
  printf("Connect=%d\n", (tUINT32)i_bConnect);
}

void P7Wrapper::Telemetry_Enable(tUINT16 i_wId, tBOOL i_bEnable) {
  // called when a telemetry counter is enabled or disabled
  printf("Id=%d, Enable=%d\n", (tUINT32)i_wId, (tUINT32)i_bEnable);
}

void P7Wrapper::InitializeTrace(std::string channelName) {
    std::cout << "Initializing trace" << std::endl;
  // create P7 trace object
  m_pTrace = P7_Create_Trace(m_pClient, TM(channelName.c_str()));
  if (nullptr == m_pTrace) {
    throw std::runtime_error("Error creating P7 trace object");
  }

  m_pTrace->Register_Thread(TM("Application"), 0);
  m_pTrace->Register_Module(TM("Main"), &m_hModule);
}

void P7Wrapper::AddTraceMessage(const char *format, ...) {
  va_list argList;
  va_start(argList, format);
  m_pTrace->P7_TRACE(m_hModule, format, argList);
  va_end(argList);
}

void P7Wrapper::AddInfoMessage(const char *format, ...) {
  va_list argList;
  va_start(argList, format);
  m_pTrace->P7_INFO(m_hModule, format, argList);
  va_end(argList);
}

void P7Wrapper::AddDebugMessage(const char *format, ...) {
  va_list argList;
  va_start(argList, format);
  m_pTrace->P7_DEBUG(m_hModule, format, argList);
  va_end(argList);
}

void P7Wrapper::AddWarningMessage(const char *format, ...) {
  va_list argList;
  va_start(argList, format);
  m_pTrace->P7_WARNING(m_hModule, format, argList);
  va_end(argList);
}

void P7Wrapper::AddErrorMessage(const char *format, ...) {
  va_list argList;
  va_start(argList, format);
  m_pTrace->P7_ERROR(m_hModule, format, argList);
  va_end(argList);
}

void P7Wrapper::AddCriticalMessage(const char *format, ...) {
  va_list argList;
  va_start(argList, format);
  m_pTrace->P7_CRITICAL(m_hModule, format, argList);
  va_end(argList);
}




int main(int i_iArgC, char* i_pArgV[])
{
    P7Wrapper p7Wrapper;
    p7Wrapper.InitializeTrace("Adaptive EM");
    p7Wrapper.AddTraceMessage("Hello");
    p7Wrapper.AddInfoMessage("Hello");
    p7Wrapper.AddDebugMessage("Hello");
    p7Wrapper.AddWarningMessage("Hello");
    p7Wrapper.AddErrorMessage("Hello");
    p7Wrapper.AddCriticalMessage("Hello");

    P7Wrapper p7Wrapper2;
    p7Wrapper2.InitializeTrace("CLASSIC EM");
    p7Wrapper2.AddTraceMessage("Hello");
    p7Wrapper2.AddInfoMessage("Hello");
    p7Wrapper2.AddDebugMessage("Hello");
    p7Wrapper2.AddWarningMessage("Hello");
    p7Wrapper2.AddErrorMessage("Hello");
    p7Wrapper2.AddCriticalMessage("Hello");




    return 0;
}



