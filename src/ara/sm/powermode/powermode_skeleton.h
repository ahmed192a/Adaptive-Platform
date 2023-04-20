/**
 * @file powermode_skeleton.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "ara/com/proxy_skeleton/data_type.h"
#include "ara/com/proxy_skeleton/skeleton/field.h"
#include "ara/com/proxy_skeleton/skeleton/service_skeleton.h"
#include "ara/sm/powermode/powermode_return_types.h"
#include "ara/sm/powermode/powermode_types.h"
#include <future>

// include the file which contains this definitions

namespace ara::sm::powermode::skeleton
{

                class PowerMode_Skeleton : public ara::com::proxy_skeleton::skeleton::ServiceSkeleton
                {
                public:
                
                    PowerMode_Skeleton(ara::com::InstanceIdentifier instance, ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle skeleton_handle)
                        :  ara::com::proxy_skeleton::skeleton::ServiceSkeleton(serviceid, instance, skeleton_handle)
                    {
                    }
                    ~PowerMode_Skeleton();
                    /*sends PowerModeMsg defined in 9.1 Type definition to all Processes to request a PowerMode.
                     *Message to all running Processes in the system to indicate a request to enter this state.*/
                    std::future<ara::sm::powermode::MessagePowermodeOutput> MessagePowerMode(PowerModeMsg const& msg){
                        return std::async(std::launch::async, [this, msg](){
                            ara::sm::powermode::MessagePowermodeOutput result = std::monostate();
                            
                            
                            return result;
                        });
                    }

                    /*ResponseMessage from a Processes which received PowerMode request from State Management.*/
                    std::future<ara::sm::powermode::EventPowermodeOutput> EventPowerMode(PowerModeRespMsg const& respMsg){
                        return std::async(std::launch::async, [this, respMsg](){
                            ara::sm::powermode::EventPowermodeOutput result = std::monostate();
                            
                            
                            return result;
                        });
                    }

                    void method_dispatch(ara::com::SOMEIP_MESSAGE::Message const &message, Socket &cserver)
                    {
                        int methodID = message.MessageId().method_id;

                        std::cout << "\t[SERVER] Dispatch " << methodID << std::endl;

                        switch (methodID)
                        {
                        case 0:
                            HandleCall(*this, &PowerMode_Skeleton::MessagePowerMode, message, cserver);
                            break;
                        case 1:
                            HandleCall(*this, &PowerMode_Skeleton::EventPowerMode, message, cserver);
                            break;
                        default:
                            std::cout << "\t[SERVER] Method not found" << std::endl;
                            break;
                        }
                    }

                private:
                    ara::com::proxy_skeleton::ServiceId serviceid{43};
                };
}