# Service Registration & Configuration System

This document describes the service configuration system that enables modular integration with external APIs and services.

## Overview

DeskHog features a sophisticated service-based architecture that allows for easy integration with external services like PostHog Analytics. The system provides dynamic configuration forms, secure credential management, and automatic service discovery through a centralized registry.

## Key Features

### Service-Based Architecture
- **Modular design**: Services are self-contained with their own configuration schemas
- **Dynamic forms**: Web UI automatically generates configuration forms based on service definitions  
- **Secure credentials**: Sensitive fields (API keys, passwords) are masked in responses and preserved during updates
- **Type safety**: Strongly-typed service definitions with validation
- **Hot configuration**: Services can be reconfigured without device restart

### Service Types Supported
- **PostHog Analytics**: Full integration with insights, team management, and regional endpoints
- **Future services**: Architecture supports easy addition of new service types

## 🏗️ Architecture

### Data Structures (Implemented in `src/config/ServiceConfig.h`)

#### `ServiceType` Enum
```cpp
enum class ServiceType {
    POSTHOG
    // New service types can be added here
};
```

#### `ServiceFieldType` Enum
```cpp
enum class ServiceFieldType {
    TEXT,       // Single-line text input
    PASSWORD,   // Masked password input (sensitive)
    SELECT,     // Dropdown/radio selection from predefined options
    TEXTAREA,   // Multi-line text input  
    BOOLEAN     // Checkbox for true/false values
};
```

#### `ServiceField` Struct
```cpp
struct ServiceField {
    String key;                         // Unique field identifier
    String label;                       // User-facing label
    String description;                 // Help text (supports HTML)
    ServiceFieldType type;              // Input field type
    String defaultValue;                // Default value
    bool required;                      // Whether field is mandatory
    bool sensitive;                     // For masking (passwords, API keys)
    std::vector<String> options;       // Available options (for SELECT type)
};
```

#### `ServiceConfig` Struct
```cpp
struct ServiceConfig {
    ServiceType type;                   // Service type identifier
    std::map<String, String> config;   // Dynamic key-value configuration
    
    // Helper methods
    String getConfig(const String& key, const String& defaultValue = "") const;
    void setConfig(const String& key, const String& value);
};
```

#### `ServiceDefinition` Struct
```cpp
struct ServiceDefinition {
    ServiceType type;                   // Service type
    String name;                        // Display name
    String description;                 // Service description
    std::vector<ServiceField> fields;   // Configuration fields
};
```

## Service Registry (`src/services/ServiceRegistry.h`)

### Centralized Service Management
The `ServiceRegistry` provides a central registry for all available services:

```cpp
class ServiceRegistry {
public:
    // Get all available service definitions
    static std::vector<ServiceDefinition> getAllServices();
    
    // Get specific service definition by type
    static ServiceDefinition getServiceDefinition(ServiceType type);
    
    // Check if service type is available
    static bool isServiceAvailable(ServiceType type);
};
```

### Service Registration
Services self-register through static methods:
```cpp
// In ServiceRegistry::getAllServices()
services.push_back(PostHogService::getServiceDefinition());
```

## PostHog Service Implementation (`src/services/PostHogService.h`)

### Service Definition
```cpp
class PostHogService {
public:
    static ServiceDefinition getServiceDefinition() {
        std::vector<ServiceField> fields = {
            ServiceField(
                "region", 
                "Region", 
                "", 
                {"us", "eu"},     // SELECT options
                "us",             // default
                true              // required
            ),
            ServiceField(
                "team_id", 
                "Team ID", 
                "This is the numeral that appears at the end of the URL...", 
                ServiceFieldType::TEXT, 
                "", 
                true              // required
            ),
            ServiceField(
                "api_key", 
                "API key", 
                "Create a new API key in your project settings...", 
                ServiceFieldType::PASSWORD, 
                "", 
                true,             // required
                true              // sensitive
            )
        };
        
        return ServiceDefinition(
            ServiceType::POSTHOG,
            "PostHog Analytics",
            "Analytics and insights platform for product teams...",
            fields
        );
    }
};
```

### Configuration Validation
```cpp
static bool validateConfig(const ServiceConfig& config);
```

### API URL Building
```cpp
static String buildApiUrl(const ServiceConfig& config, const String& endpoint);
static int getTimeoutMs();
static int getTeamId(const ServiceConfig& config);
```


## Web UI Integration (`CaptivePortal`)

### API Endpoints

#### `GET /api/services/definitions`
Returns all available service definitions with their configuration schemas:
```json
[
  {
    "type": "POSTHOG",
    "name": "PostHog Analytics", 
    "description": "Analytics and insights platform for product teams...",
    "fields": [
      {
        "key": "region",
        "label": "Region",
        "description": "",
        "type": "SELECT",
        "options": ["us", "eu"],
        "defaultValue": "us",
        "required": true,
        "sensitive": false
      },
      {
        "key": "team_id",
        "label": "Team ID",
        "description": "This is the numeral that appears at the end of the URL...",
        "type": "TEXT",
        "defaultValue": "",
        "required": true,
        "sensitive": false
      },
      {
        "key": "api_key",
        "label": "API key", 
        "description": "Create a new API key in your project settings...",
        "type": "PASSWORD",
        "defaultValue": "",
        "required": true,
        "sensitive": true
      }
    ]
  }
]
```

#### `GET /api/services/configured`
Returns currently configured services with sensitive fields masked:
```json
[
  {
    "type": "POSTHOG",
    "config": {
      "region": "eu",
      "team_id": "70750", 
      "api_key": "phx_****"
    }
  }
]
```

#### `POST /api/services/configured`
Saves service configurations with automatic sensitive field preservation:
```json
[
  {
    "type": "POSTHOG",
    "config": {
      "region": "us",
      "team_id": "12345",
      "api_key": "phx_1234567890abcdef"
    }
  }
]
```

### Dynamic Form Generation

The web UI automatically generates appropriate form fields based on service definitions:

- **TEXT fields**: Single-line text inputs
- **PASSWORD fields**: Masked inputs with click-to-edit functionality
- **SELECT fields**: 
  - Dropdown for >2 options
  - Radio buttons for exactly 2 options (like region selection)
- **BOOLEAN fields**: Checkboxes
- **TEXTAREA fields**: Multi-line text areas

### Sensitive Field Handling

- **Display**: Sensitive fields show masked values (`phx_****`) 
- **Editing**: Click masked fields to reveal password input for editing
- **Preservation**: Empty sensitive fields during save preserve existing values
- **Security**: Actual sensitive values never sent to browser after initial save

## PostHogClient Integration

### Service-Based Configuration
`PostHogClient` has been modernized to use the service configuration system:

```cpp
// Before: Direct ConfigManager methods
String region = _config.getRegion();
int teamId = _config.getTeamId(); 
String apiKey = _config.getApiKey();

// After: Service configuration system
ServiceConfig posthogConfig = _config.getServiceConfig(ServiceType::POSTHOG);
String region = posthogConfig.getConfig("region", "us");
String teamIdStr = posthogConfig.getConfig("team_id");
String apiKey = posthogConfig.getConfig("api_key");
```

### Benefits
- **Single source of truth**: Configuration comes from service system
- **Validation**: Service-level validation of configuration
- **Flexibility**: Easy to add new PostHog configuration options
- **Consistency**: Same configuration system used across all services

## Storage & Persistence (`ConfigManager`)

### Service Configuration Storage
```cpp
class ConfigManager {
    // Get all configured services
    std::vector<ServiceConfig> getServiceConfigs();
    
    // Save service configurations 
    bool saveServiceConfigs(const std::vector<ServiceConfig>& configs);
    
    // Get specific service configuration
    ServiceConfig getServiceConfig(ServiceType type);
};
```

### Storage Format
Service configurations are stored as JSON in ESP32 preferences:
```json
[
  {
    "type": "POSTHOG",
    "config": {
      "region": "eu",
      "team_id": "70750",
      "api_key": "phx_1234567890abcdef"
    }
  }
]
```

### Event System Integration
Configuration changes trigger events:
```cpp
_eventQueue.publishEvent(EventType::SERVICE_CONFIG_CHANGED, "");
```

## 🔧 Adding New Services

### 1. Define Service Type
Add to `ServiceType` enum in `ServiceConfig.h`:
```cpp
enum class ServiceType {
    POSTHOG,
    MY_NEW_SERVICE  // Add here
};
```

### 2. Create Service Class
Create `src/services/MyNewService.h`:
```cpp
class MyNewService {
public:
    static ServiceDefinition getServiceDefinition() {
        std::vector<ServiceField> fields = {
            ServiceField("api_url", "API URL", "Your service API endpoint", 
                        ServiceFieldType::TEXT, "https://api.example.com", true),
            ServiceField("api_key", "API Key", "Your service API key",
                        ServiceFieldType::PASSWORD, "", true, true)
        };
        
        return ServiceDefinition(
            ServiceType::MY_NEW_SERVICE,
            "My New Service",
            "Description of what this service does",
            fields
        );
    }
    
    static bool validateConfig(const ServiceConfig& config);
    // ... other service-specific methods
};
```

### 3. Register in ServiceRegistry
Update `ServiceRegistry.h`:
```cpp
static std::vector<ServiceDefinition> getAllServices() {
    std::vector<ServiceDefinition> services;
    services.push_back(PostHogService::getServiceDefinition());
    services.push_back(MyNewService::getServiceDefinition());  // Add here
    return services;
}

static ServiceDefinition getServiceDefinition(ServiceType type) {
    switch (type) {
        case ServiceType::POSTHOG:
            return PostHogService::getServiceDefinition();
        case ServiceType::MY_NEW_SERVICE:  // Add here
            return MyNewService::getServiceDefinition();
        default:
            return ServiceDefinition();
    }
}
```

### 4. Update String Conversions
Add to `ServiceConfig.h` utility functions:
```cpp
inline String serviceTypeToString(ServiceType type) {
    switch (type) {
        case ServiceType::POSTHOG: return "POSTHOG";
        case ServiceType::MY_NEW_SERVICE: return "MY_NEW_SERVICE";  // Add here
        default: return "UNKNOWN";
    }
}

inline ServiceType stringToServiceType(const String& str) {
    if (str == "POSTHOG") return ServiceType::POSTHOG;
    if (str == "MY_NEW_SERVICE") return ServiceType::MY_NEW_SERVICE;  // Add here
    return ServiceType::POSTHOG; // Default fallback
}
```

### 5. Implement Service Client
Create your service client that uses the configuration:
```cpp
class MyServiceClient {
private:
    ConfigManager& _config;
    
public:
    MyServiceClient(ConfigManager& config) : _config(config) {}
    
    void makeApiCall() {
        ServiceConfig config = _config.getServiceConfig(ServiceType::MY_NEW_SERVICE);
        String apiUrl = config.getConfig("api_url");
        String apiKey = config.getConfig("api_key");
        
        // Use configuration to make API calls
    }
};
```

## 🔐 Security Features

### Sensitive Field Protection
- **Masking**: Sensitive fields (passwords, API keys) are masked in API responses
- **Preservation**: Empty sensitive fields during updates preserve existing values
- **No exposure**: Actual sensitive values never sent to browser after initial configuration

### Input Validation
- **Required fields**: Enforced at both client and server level
- **Service-specific validation**: Each service can implement custom validation logic
- **Type safety**: Field types enforced by the configuration system

### Secure Storage
- **ESP32 preferences**: Configuration stored in encrypted preferences partition
- **JSON serialization**: Structured storage with proper error handling


## 🎯 Benefits

### For Developers
- **Easy integration**: Simple pattern for adding new external services
- **Type safety**: Strongly-typed configuration with compile-time validation
- **Automatic UI**: Web forms generated automatically from service definitions
- **Consistent patterns**: Same architecture for all external service integrations

### For Users  
- **Intuitive configuration**: Rich web forms with validation and help text
- **Secure credential management**: Masked sensitive fields with preservation
- **Service discovery**: Automatic discovery of available services
- **Live reconfiguration**: Update service settings without device restart

### For System Architecture
- **Modularity**: Services are self-contained and independent
- **Extensibility**: Easy to add new services without touching core code
- **Maintainability**: Clear separation of concerns between services
- **Testability**: Each service can be tested independently

## 📋 Implementation Status

- **Service architecture**: Complete service registration and configuration system
- **PostHog service**: Full PostHog integration with regional support
- **Web UI**: Dynamic form generation with sensitive field handling
- **Storage system**: Persistent configuration with event integration
- **PostHogClient modernization**: Updated to use service configuration
- **Documentation**: Complete implementation and usage documentation

The service configuration system is production-ready and provides a solid foundation for future service integrations.