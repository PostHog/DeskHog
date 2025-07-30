#pragma once

#include "config/ServiceConfig.h"

/**
 * @brief PostHog Analytics Service
 * 
 * Provides service definition and helper methods for PostHog integration.
 * This service enables analytics, insights, and data visualization capabilities.
 */
class PostHogService {
public:
    /**
     * @brief Get the service definition for PostHog
     * @return ServiceDefinition with all configuration fields
     */
    static ServiceDefinition getServiceDefinition() {
        std::vector<ServiceField> fields = {
            ServiceField(
                "region", 
                "Region", 
                "", // no description needed for radio buttons
                {"us", "eu"}, // options for SELECT type
                "us",         // default value
                true          // required
            ),
            ServiceField(
                "team_id", 
                "Team ID", 
                "This is the numeral that appears at the end of the URL when you're looking at your <a href=\"https://app.posthog.com/\" target=\"_blank\">project's homepage dashboard</a>.", 
                ServiceFieldType::TEXT, 
                "", 
                true    // required
            ),
            ServiceField(
                "api_key", 
                "API key", 
                "Create a <a href=\"https://app.posthog.com/settings/user-api-keys\" target=\"_blank\">new API key in your project settings</a>. Give it read access to insights.",
                ServiceFieldType::PASSWORD, 
                "", 
                true,   // required
                true    // sensitive
            )
        };
        
        return ServiceDefinition(
            ServiceType::POSTHOG,
            "PostHog Analytics",
            "Connect to view PostHog insights right on your DeskHog.",
            fields
        );
    }
    
    /**
     * @brief Validate PostHog service configuration
     * @param config The service configuration to validate
     * @return true if configuration appears valid
     */
    static bool validateConfig(const ServiceConfig& config) {
        String apiKey = config.getConfig("api_key");
        String region = config.getConfig("region");
        String teamId = config.getConfig("team_id");
        
        // Basic validation
        if (apiKey.isEmpty() || region.isEmpty() || teamId.isEmpty()) {
            return false;
        }
        
        // API key should start with "phc_" for PostHog
        if (!apiKey.startsWith("phc_")) {
            return false;
        }
        
        // Region should be us or eu
        if (region != "us" && region != "eu") {
            return false;
        }
        
        // Team ID should be numeric
        if (teamId.toInt() <= 0) {
            return false;
        }
        
        return true;
    }
    
    /**
     * @brief Build API URL for PostHog requests
     * @param config The service configuration
     * @param endpoint The API endpoint (e.g., "/api/projects/123/insights/")
     * @return Complete URL for the API request
     */
    static String buildApiUrl(const ServiceConfig& config, const String& endpoint) {
        String region = config.getConfig("region", "us");
        String host;
        
        // Build host URL based on region
        if (region == "eu") {
            host = "https://eu.posthog.com";
        } else {
            host = "https://app.posthog.com"; // Default to US
        }
        
        // Ensure endpoint starts with slash
        String cleanEndpoint = endpoint;
        if (!cleanEndpoint.startsWith("/")) {
            cleanEndpoint = "/" + cleanEndpoint;
        }
        
        return host + cleanEndpoint;
    }
    
    /**
     * @brief Get request timeout for PostHog API calls
     * @return Timeout in milliseconds (fixed at 10 seconds)
     */
    static int getTimeoutMs() {
        return 10000; // 10 seconds timeout
    }
    
    /**
     * @brief Get team ID as integer from configuration
     * @param config The service configuration
     * @return Team ID as integer, or -1 if not set/invalid
     */
    static int getTeamId(const ServiceConfig& config) {
        String teamIdStr = config.getConfig("team_id");
        if (teamIdStr.isEmpty()) {
            return -1;
        }
        int teamId = teamIdStr.toInt();
        return teamId > 0 ? teamId : -1;
    }
    
};
