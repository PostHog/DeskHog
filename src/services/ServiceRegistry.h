#pragma once

#include "config/ServiceConfig.h"
#include "services/PostHogService.h"

/**
 * @brief Central registry for all available services
 * 
 * This class manages service definitions and provides a central place
 * to register and discover available services in the system.
 */
class ServiceRegistry {
public:
    /**
     * @brief Get all available service definitions
     * @return Vector of all registered service definitions
     */
    static std::vector<ServiceDefinition> getAllServices() {
        std::vector<ServiceDefinition> services;
        
        // Register PostHog service
        services.push_back(PostHogService::getServiceDefinition());
        
        // Future services can be added here
        // services.push_back(HomeAssistantService::getServiceDefinition());
        
        return services;
    }
    
    /**
     * @brief Get a specific service definition by type
     * @param type The service type to look up
     * @return ServiceDefinition for the requested type, or empty definition if not found
     */
    static ServiceDefinition getServiceDefinition(ServiceType type) {
        switch (type) {
            case ServiceType::POSTHOG:
                return PostHogService::getServiceDefinition();
            
            // Future services can be added here
            // case ServiceType::HOME_ASSISTANT:
            //     return HomeAssistantService::getServiceDefinition();
            
            default:
                return ServiceDefinition(); // Return empty definition for unknown services
        }
    }
    
    /**
     * @brief Check if a service type is available/registered
     * @param type The service type to check
     * @return true if the service is registered
     */
    static bool isServiceAvailable(ServiceType type) {
        switch (type) {
            case ServiceType::POSTHOG:
                return true;
            
            // Future services can be added here
            // case ServiceType::HOME_ASSISTANT:
            //     return true;
            
            default:
                return false;
        }
    }
};