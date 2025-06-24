function showScreen(screenId) {
    const screens = ['config-screen'];
    screens.forEach(id => {
        const el = document.getElementById(id);
        if (el) el.classList.add('hidden');
    });
    const screenToShow = document.getElementById(screenId);
    if (screenToShow) screenToShow.classList.remove('hidden');
    
    let title = "DeskHog configuration";
    document.getElementById('page-title').textContent = title;
}

// Handle WiFi form submission
function saveWifiConfig() {
    const form = document.getElementById('wifi-form');
    const formData = new FormData(form);
    const globalActionStatusEl = document.getElementById('global-action-status');
    
    fetch('/api/actions/save-wifi', { 
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        if (data && data.status === 'queued') {
            console.log("Save WiFi action successfully queued.", data.message);
            if (globalActionStatusEl) {
                globalActionStatusEl.textContent = data.message || "Save WiFi initiated. Device will attempt to connect.";
                globalActionStatusEl.className = 'status-message info';
                globalActionStatusEl.style.display = 'block';
                setTimeout(() => {
                    if (globalActionStatusEl.textContent === (data.message || "Save WiFi initiated. Device will attempt to connect.")) {
                        globalActionStatusEl.style.display = 'none';
                        globalActionStatusEl.textContent = '';
                        globalActionStatusEl.className = 'status-message';
                    }
                }, 5000);
            }
        } else {
            const errorMessage = (data && data.message) ? data.message : "Failed to initiate WiFi save.";
            console.error("Failed to initiate WiFi save:", errorMessage);
            if (globalActionStatusEl) {
                globalActionStatusEl.textContent = errorMessage;
                globalActionStatusEl.className = 'status-message error';
                globalActionStatusEl.style.display = 'block';
                setTimeout(() => {
                    if (globalActionStatusEl.className.includes('error')) {
                        globalActionStatusEl.style.display = 'none';
                        globalActionStatusEl.textContent = '';
                        globalActionStatusEl.className = 'status-message';
                    }
                }, 7000);
            }
        }
    })
    .catch(() => {
        console.error("Communication error trying to save WiFi.");
        if (globalActionStatusEl) {
            globalActionStatusEl.textContent = "Communication error trying to save WiFi.";
            globalActionStatusEl.className = 'status-message error';
            globalActionStatusEl.style.display = 'block';
            setTimeout(() => {
                if (globalActionStatusEl.className.includes('error')) {
                    globalActionStatusEl.style.display = 'none';
                    globalActionStatusEl.textContent = '';
                    globalActionStatusEl.className = 'status-message';
                }
            }, 7000);
        }
    });
    
    return false; 
}


// Toggle API key visibility
function toggleApiKeyVisibility() {
    const apiKeyInput = document.getElementById('apiKey');
    apiKeyInput.type = apiKeyInput.type === 'password' ? 'text' : 'password';
}

// Global variables for card management
let availableCardTypes = [];
let configuredCards = [];

// Global variables for service management
let availableServices = [];
let configuredServices = [];

// Load service definitions from the device
async function loadServiceDefinitions() {
    try {
        const response = await fetch('/api/services/definitions');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const definitions = await response.json();
        availableServices = definitions;
        updateAvailableServicesList();
        console.log('Loaded', definitions.length, 'service definitions');
    } catch (error) {
        console.error('Failed to load service definitions:', error);
        availableServices = [];
        updateAvailableServicesList();
    }
}

// Load configured services from the device
async function loadConfiguredServices() {
    try {
        const response = await fetch('/api/services/configured');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const services = await response.json();
        configuredServices = services;
        updateAvailableServicesList(); // Update availability status
        console.log('Loaded', services.length, 'configured services');
    } catch (error) {
        console.error('Failed to load configured services:', error);
        configuredServices = [];
        updateAvailableServicesList();
    }
}

// Render configuration fields for a service definition
function renderServiceConfigFields(serviceDef) {
    if (!serviceDef.fields || serviceDef.fields.length === 0) {
        return ''; // No configuration fields
    }
    
    return serviceDef.fields.map(field => {
        const fieldId = `service-config-${serviceDef.type}-${field.key}`;
        const required = field.required ? 'required' : '';
        
        switch (field.type) {
            case 'TEXT':
                return `
                    <div class="config-field">
                        <label for="${fieldId}">${field.label}${field.required ? ' *' : ''}</label>
                        <input type="text" class="config-input" id="${fieldId}" 
                               placeholder="${field.label}" value="${field.defaultValue || ''}" ${required}>
                        ${field.description ? `<small class="field-description">${field.description}</small>` : ''}
                    </div>
                `;
            case 'PASSWORD':
                return `
                    <div class="config-field">
                        <label for="${fieldId}">${field.label}${field.required ? ' *' : ''}</label>
                        <input type="password" class="config-input" id="${fieldId}" 
                               placeholder="${field.label}" value="${field.defaultValue || ''}" ${required}>
                        ${field.description ? `<small class="field-description">${field.description}</small>` : ''}
                    </div>
                `;
            case 'SELECT':
                const options = field.options || [];
                
                // If exactly 2 options, render as radio buttons like the existing region selector
                if (options.length === 2) {
                    const radioButtons = options.map(option => {
                        const radioId = `${fieldId}-${option}`;
                        const checked = option === field.defaultValue ? 'checked' : '';
                        const displayText = option === 'us' ? 'US' : option === 'eu' ? 'EU' : option;
                        return `
                            <label for="${radioId}">
                                <input type="radio" name="${fieldId}" id="${radioId}" value="${option}" ${checked} ${required}>
                                ${displayText}
                            </label>
                        `;
                    }).join('');
                    
                    return `
                        <div class="config-field">
                            <label>${field.label}${field.required ? ' *' : ''}</label>
                            <div class="region-options">
                                ${radioButtons}
                            </div>
                            ${field.description ? `<small class="field-description">${field.description}</small>` : ''}
                        </div>
                    `;
                } else {
                    // Regular dropdown for more than 2 options
                    const optionItems = options.map(option => 
                        `<option value="${option}" ${option === field.defaultValue ? 'selected' : ''}>${option}</option>`
                    ).join('');
                    return `
                        <div class="config-field">
                            <label for="${fieldId}">${field.label}${field.required ? ' *' : ''}</label>
                            <select class="config-input" id="${fieldId}" ${required}>
                                <option value="">Select ${field.label}</option>
                                ${optionItems}
                            </select>
                            ${field.description ? `<small class="field-description">${field.description}</small>` : ''}
                        </div>
                    `;
                }
            case 'BOOLEAN':
                return `
                    <div class="config-field">
                        <label class="checkbox-label">
                            <input type="checkbox" class="config-input" id="${fieldId}" 
                                   ${field.defaultValue === 'true' ? 'checked' : ''} ${required}>
                            ${field.label}${field.required ? ' *' : ''}
                        </label>
                        ${field.description ? `<small class="field-description">${field.description}</small>` : ''}
                    </div>
                `;
            default:
                return `<input type="text" class="config-input" id="${fieldId}" placeholder="${field.label}" ${required}>`;
        }
    }).join('');
}

// Update the available services list
function updateAvailableServicesList() {
    const container = document.getElementById('available-services-list');
    if (!container) return;
    
    // Handle empty state
    if (!availableServices || availableServices.length === 0) {
        if (container.innerHTML !== '<p>No services available</p>') {
            container.innerHTML = '<p>No services available</p>';
        }
        return;
    }
    
    // Clear and rebuild the list
    container.innerHTML = '';
    
    availableServices.forEach(serviceDef => {
        // Find existing configuration for this service
        const existingConfig = configuredServices.find(service => service.type === serviceDef.type);
        const isConfigured = !!existingConfig;
        
        const serviceItem = document.createElement('div');
        serviceItem.className = 'available-service-item';
        serviceItem.setAttribute('data-service-type', serviceDef.type);
        
        serviceItem.innerHTML = `
            <div class="available-service-info">
                <div class="available-service-name">${serviceDef.name}</div>
                <div class="available-service-description">${serviceDef.description || ''}</div>
                <div class="available-service-status">${isConfigured ? 'Configured' : 'Not configured'}</div>
            </div>
            <div class="available-service-actions">
                ${renderServiceConfigFields(serviceDef)}
                <button class="save-service-btn" onclick="saveServiceConfiguration('${serviceDef.type}')">
                    ${isConfigured ? 'Update Configuration' : 'Save Configuration'}
                </button>
            </div>
        `;
        
        container.appendChild(serviceItem);
        
        // Pre-populate existing configuration values
        if (isConfigured && existingConfig.config) {
            for (const [key, value] of Object.entries(existingConfig.config)) {
                const fieldId = `service-config-${serviceDef.type}-${key}`;
                
                // Find the corresponding field definition to determine type
                const fieldDef = serviceDef.fields.find(f => f.key === key);
                
                if (fieldDef && fieldDef.type === 'SELECT' && fieldDef.options && fieldDef.options.length === 2) {
                    // This is a radio button group (2-option SELECT)
                    const radioGroup = document.getElementsByName(fieldId);
                    for (const radio of radioGroup) {
                        radio.checked = (radio.value === value);
                    }
                } else {
                    // Regular input field
                    const input = document.getElementById(fieldId);
                    if (input) {
                        if (input.type === 'checkbox') {
                            input.checked = value === 'true';
                        } else {
                            // Handle masked values for sensitive fields
                            if (value.includes('****')) {
                                // Show that there's a value but allow re-entry
                                input.value = ''; // Keep empty for security
                                input.placeholder = 'Current: ' + value + ' (leave empty to keep current)';
                                // Change password fields to text temporarily to show the mask
                                if (input.type === 'password') {
                                    input.type = 'text';
                                    input.value = value;
                                    input.setAttribute('readonly', true);
                                    // Add a small edit button or click handler
                                    input.style.backgroundColor = '#f0f0f0';
                                    input.onclick = function() {
                                        this.type = 'password';
                                        this.value = '';
                                        this.placeholder = 'Enter new value or leave empty to keep current';
                                        this.removeAttribute('readonly');
                                        this.style.backgroundColor = '';
                                        this.onclick = null;
                                    };
                                }
                            } else {
                                input.value = value;
                            }
                        }
                    }
                }
            }
        }
    });
}

// Save service configuration
async function saveServiceConfiguration(serviceType) {
    const globalActionStatusEl = document.getElementById('global-action-status');
    
    // Find the service definition
    const serviceDef = availableServices.find(def => def.type === serviceType);
    if (!serviceDef) {
        console.error('Service definition not found for type:', serviceType);
        return;
    }
    
    let configData = {};
    
    // Collect values from all configuration fields
    if (serviceDef.fields && serviceDef.fields.length > 0) {
        for (const field of serviceDef.fields) {
            const fieldId = `service-config-${serviceType}-${field.key}`;
            let value = '';
            
            // Handle different field types
            if (field.type === 'SELECT' && field.options && field.options.length === 2) {
                // This is a radio button group (2-option SELECT)
                const radioGroup = document.getElementsByName(fieldId);
                for (const radio of radioGroup) {
                    if (radio.checked) {
                        value = radio.value;
                        break;
                    }
                }
            } else {
                // Regular input field
                const input = document.getElementById(fieldId);
                
                if (!input) {
                    console.error(`Config input not found: ${fieldId}`);
                    continue;
                }
                
                if (input.type === 'checkbox') {
                    value = input.checked ? 'true' : 'false';
                } else {
                    value = input.value.trim();
                }
            }
            
            // Handle sensitive fields - preserve existing value if empty
            if (!value && field.sensitive) {
                // Check if there's an existing value for this field
                const existingConfig = configuredServices.find(service => service.type === serviceType);
                if (existingConfig && existingConfig.config && existingConfig.config[field.key]) {
                    // Preserve existing value for sensitive fields when user leaves them empty
                    configData[field.key] = existingConfig.config[field.key];
                }
            }
            
            // Validate required fields (after handling sensitive field preservation)
            if (field.required && !value && !configData[field.key]) {
                if (globalActionStatusEl) {
                    globalActionStatusEl.textContent = `Please enter a value for ${field.label}`;
                    globalActionStatusEl.className = 'status-message error';
                    globalActionStatusEl.style.display = 'block';
                    setTimeout(() => {
                        globalActionStatusEl.style.display = 'none';
                        globalActionStatusEl.textContent = '';
                        globalActionStatusEl.className = 'status-message';
                    }, 3000);
                }
                return;
            }
            
            // Only add non-empty values (sensitive fields already handled above)
            if (value) {
                configData[field.key] = value;
            }
        }
    }
    
    // Create service configuration
    const serviceConfig = {
        type: serviceType,
        config: configData
    };
    
    // Update existing config or add new one
    const existingIndex = configuredServices.findIndex(service => service.type === serviceType);
    if (existingIndex >= 0) {
        configuredServices[existingIndex] = serviceConfig;
    } else {
        configuredServices.push(serviceConfig);
    }
    
    // Save to device
    try {
        const response = await fetch('/api/services/configured', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(configuredServices)
        });
        
        const result = await response.json();
        if (result.success) {
            console.log('Service configuration saved successfully');
            
            // Reload configurations to reflect changes
            await loadConfiguredServices();
            
            if (globalActionStatusEl) {
                globalActionStatusEl.textContent = "Service configuration saved successfully";
                globalActionStatusEl.className = 'status-message info';
                globalActionStatusEl.style.display = 'block';
                setTimeout(() => {
                    globalActionStatusEl.style.display = 'none';
                    globalActionStatusEl.textContent = '';
                    globalActionStatusEl.className = 'status-message';
                }, 3000);
            }
        } else {
            console.error('Failed to save service configuration:', result.message);
            if (globalActionStatusEl) {
                globalActionStatusEl.textContent = result.message || 'Failed to save service configuration';
                globalActionStatusEl.className = 'status-message error';
                globalActionStatusEl.style.display = 'block';
                setTimeout(() => {
                    globalActionStatusEl.style.display = 'none';
                    globalActionStatusEl.textContent = '';
                    globalActionStatusEl.className = 'status-message';
                }, 5000);
            }
        }
    } catch (error) {
        console.error('Error saving service configuration:', error);
        if (globalActionStatusEl) {
            globalActionStatusEl.textContent = 'Error saving service configuration';
            globalActionStatusEl.className = 'status-message error';
            globalActionStatusEl.style.display = 'block';
            setTimeout(() => {
                globalActionStatusEl.style.display = 'none';
                globalActionStatusEl.textContent = '';
                globalActionStatusEl.className = 'status-message';
            }, 5000);
        }
    }
}

// Load card definitions from the device
async function loadCardDefinitions() {
    try {
        const response = await fetch('/api/cards/definitions');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const definitions = await response.json();
        availableCardTypes = definitions;
        updateAvailableCardsList();
        console.log('Loaded', definitions.length, 'card definitions');
    } catch (error) {
        console.error('Failed to load card definitions:', error);
        // Set empty array as fallback
        availableCardTypes = [];
    }
}

// Load configured cards from the device
async function loadConfiguredCards() {
    try {
        const response = await fetch('/api/cards/configured');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const cards = await response.json();
        configuredCards = cards;
        updateCardsListUI();
        updateAvailableCardsList(); // Update availability status
        console.log('Loaded', cards.length, 'configured cards');
    } catch (error) {
        console.error('Failed to load configured cards:', error);
        // Set empty array as fallback
        configuredCards = [];
        updateCardsListUI();
        updateAvailableCardsList(); // Update availability status
    }
}

// Render configuration fields for a card definition
function renderConfigFields(cardDef) {
    
    // Handle new dynamic config fields
    if (cardDef.configFields && cardDef.configFields.length > 0) {
        return cardDef.configFields.map(field => {
            const fieldId = `config-${cardDef.id}-${field.key}`;
            const required = field.required ? 'required' : '';
            
            switch (field.type) {
                case 'TEXT':
                    return `
                        <div class="config-field">
                            <label for="${fieldId}">${field.inputLabel}${field.required ? ' *' : ''}</label>
                            <input type="text" class="config-input" id="${fieldId}" 
                                   placeholder="${field.inputLabel}" value="${field.defaultValue || ''}" ${required}>
                            ${field.uiDescription ? `<small class="field-description">${field.uiDescription}</small>` : ''}
                        </div>
                    `;
                case 'TEXTAREA':
                    return `
                        <div class="config-field">
                            <label for="${fieldId}">${field.inputLabel}${field.required ? ' *' : ''}</label>
                            <textarea class="config-input" id="${fieldId}" 
                                      placeholder="${field.inputLabel}" ${required}>${field.defaultValue || ''}</textarea>
                            ${field.uiDescription ? `<small class="field-description">${field.uiDescription}</small>` : ''}
                        </div>
                    `;
                case 'SELECT':
                    const options = field.options || [];
                    const optionItems = options.map(option => 
                        `<option value="${option}" ${option === field.defaultValue ? 'selected' : ''}>${option}</option>`
                    ).join('');
                    return `
                        <div class="config-field">
                            <label for="${fieldId}">${field.inputLabel}${field.required ? ' *' : ''}</label>
                            <select class="config-input" id="${fieldId}" ${required}>
                                <option value="">Select ${field.inputLabel}</option>
                                ${optionItems}
                            </select>
                            ${field.uiDescription ? `<small class="field-description">${field.uiDescription}</small>` : ''}
                        </div>
                    `;
                case 'BOOLEAN':
                    return `
                        <div class="config-field">
                            <label class="checkbox-label">
                                <input type="checkbox" class="config-input" id="${fieldId}" 
                                       ${field.defaultValue === 'true' ? 'checked' : ''} ${required}>
                                ${field.inputLabel}${field.required ? ' *' : ''}
                            </label>
                            ${field.uiDescription ? `<small class="field-description">${field.uiDescription}</small>` : ''}
                        </div>
                    `;
                default:
                    return `<input type="text" class="config-input" id="${fieldId}" placeholder="${field.inputLabel}" ${required}>`;
            }
        }).join('');
    }
    
    return ''; // No configuration fields
}

// Update the available cards list
function updateAvailableCardsList() {
    const container = document.getElementById('available-cards-list');
    if (!container) return;
    
    // Handle empty state
    if (!availableCardTypes || availableCardTypes.length === 0) {
        if (container.innerHTML !== '<p>No card types available</p>') {
            container.innerHTML = '<p>No card types available</p>';
        }
        return;
    }
    
    // Update existing items or create new ones
    availableCardTypes.forEach(cardDef => {
        let cardItem = container.querySelector(`[data-card-type="${cardDef.id}"]`);
        
        // Check if this card type is already configured and if it allows multiple instances
        const existingCount = configuredCards.filter(card => card.type === cardDef.id).length;
        const canAdd = cardDef.allowMultiple || existingCount === 0;
        
        let statusText = '';
        if (!cardDef.allowMultiple && existingCount > 0) {
            statusText = 'Already added (single instance)';
        } else if (existingCount > 0) {
            statusText = `${existingCount} instance${existingCount > 1 ? 's' : ''} configured`;
        }
        
        if (!cardItem) {
            // Create new item only if it doesn't exist
            cardItem = document.createElement('div');
            cardItem.className = 'available-card-item';
            cardItem.setAttribute('data-card-type', cardDef.id);
            
            cardItem.innerHTML = `
                <div class="available-card-info">
                    <div class="available-card-name">${cardDef.name}</div>
                    <div class="available-card-description">${cardDef.description || cardDef.uiDescription || ''}</div>
                    <div class="available-card-status"></div>
                </div>
                <div class="available-card-actions">
                    ${renderConfigFields(cardDef)}
                    <button class="add-card-btn" onclick="addCardFromList('${cardDef.id}')" ${!canAdd ? 'style="display:none"' : ''}>
                        + Add card
                    </button>
                </div>
            `;
            
            container.appendChild(cardItem);
        } else {
            // Update existing item without destroying input values
            const statusEl = cardItem.querySelector('.available-card-status');
            if (statusEl) {
                statusEl.textContent = statusText;
            }
            
            const addBtn = cardItem.querySelector('.add-card-btn');
            if (addBtn) {
                addBtn.style.display = canAdd ? '' : 'none';
            }
        }
    });
    
    // Remove any cards that no longer exist in availableCardTypes
    const existingItems = container.querySelectorAll('[data-card-type]');
    existingItems.forEach(item => {
        const cardType = item.getAttribute('data-card-type');
        if (!availableCardTypes.find(def => def.id === cardType)) {
            item.remove();
        }
    });
}

// Save card configuration to device
async function saveCardConfiguration() {
    try {
        const response = await fetch('/api/cards/configured', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(configuredCards)
        });
        
        const result = await response.json();
        if (result.success) {
            console.log('Card configuration saved successfully');
            // Reload to reflect changes
            await loadConfiguredCards();
        } else {
            console.error('Failed to save card configuration:', result.message);
        }
    } catch (error) {
        console.error('Error saving card configuration:', error);
    }
}

// Add new card from the list interface
function addCardFromList(cardTypeId) {
    const globalActionStatusEl = document.getElementById('global-action-status');
    
    // Find the card definition
    const cardDef = availableCardTypes.find(def => def.id === cardTypeId);
    if (!cardDef) {
        console.error('Card definition not found for type:', cardTypeId);
        return;
    }
    
    let cardConfig = '';
    let configMap = {};
    
    // Handle dynamic configuration fields
    if (cardDef.configFields && cardDef.configFields.length > 0) {
        // Collect values from all dynamic config fields
        for (const field of cardDef.configFields) {
            const fieldId = `config-${cardTypeId}-${field.key}`;
            const input = document.getElementById(fieldId);
            
            if (!input) {
                console.error(`Config input not found: ${fieldId}`);
                continue;
            }
            
            let value = '';
            if (input.type === 'checkbox') {
                value = input.checked ? 'true' : 'false';
            } else {
                value = input.value.trim();
            }
            
            // Validate required fields
            if (field.required && !value) {
                if (globalActionStatusEl) {
                    globalActionStatusEl.textContent = `Please enter a value for ${field.inputLabel}`;
                    globalActionStatusEl.className = 'status-message error';
                    globalActionStatusEl.style.display = 'block';
                    setTimeout(() => {
                        globalActionStatusEl.style.display = 'none';
                        globalActionStatusEl.textContent = '';
                        globalActionStatusEl.className = 'status-message';
                    }, 3000);
                }
                return;
            }
            
            if (value) {
                configMap[field.key] = value;
            }
        }
    }
    
    // Create new card configuration
    const newCard = {
        type: cardTypeId,
        name: cardDef.name,
        order: configuredCards.length // Add to end
    };
    
    // Add configuration data
    if (Object.keys(configMap).length > 0) {
        newCard.configMap = configMap;
    }
    
    // Add to current configuration
    configuredCards.push(newCard);
    
    // Save to device
    saveCardConfiguration();
    
    // Clear the config inputs
    if (cardDef.configFields && cardDef.configFields.length > 0) {
        for (const field of cardDef.configFields) {
            const fieldId = `config-${cardTypeId}-${field.key}`;
            const input = document.getElementById(fieldId);
            if (input) {
                if (input.type === 'checkbox') {
                    input.checked = field.defaultValue === 'true';
                } else {
                    input.value = field.defaultValue || '';
                }
            }
        }
    }
    
    // Update the available cards list to reflect new state
    updateAvailableCardsList();
    
    if (globalActionStatusEl) {
        globalActionStatusEl.textContent = "Card added successfully";
        globalActionStatusEl.className = 'status-message info';
        globalActionStatusEl.style.display = 'block';
        setTimeout(() => {
            globalActionStatusEl.style.display = 'none';
            globalActionStatusEl.textContent = '';
            globalActionStatusEl.className = 'status-message';
        }, 3000);
    }
}

// Get display text for card configuration
function getConfigDisplayText(card) {
    if (card.configMap && Object.keys(card.configMap).length > 0) {
        // Display dynamic configuration
        const configEntries = Object.entries(card.configMap)
            .map(([key, value]) => `${key}: ${value}`)
            .join(', ');
        return ` • Config: ${configEntries}`;
    }
    return ''; // No configuration
}

// Update the cards list UI with drag-and-drop functionality
function updateCardsListUI() {
    const container = document.getElementById('cards-list');
    if (!container) return;
    
    container.innerHTML = '';
    
    if (!configuredCards || configuredCards.length === 0) {
        container.innerHTML = '<p>No cards configured</p>';
        return;
    }
    
    // Sort cards by order
    const sortedCards = [...configuredCards].sort((a, b) => a.order - b.order);
    
    const list = document.createElement('div');
    list.className = 'cards-list';
    
    sortedCards.forEach((card, index) => {
        const item = document.createElement('div');
        item.className = 'card-item';
        item.draggable = true;
        item.dataset.cardIndex = index;
        
        item.innerHTML = `
            <div style="display: flex; justify-content: space-between; align-items: center;">
                <div style="display: flex; align-items: center;">
                    <span class="drag-handle">⋮⋮</span>
                    <div>
                        <strong>${card.name}</strong>
                        <br>
                        <small>Type: ${card.type}${getConfigDisplayText(card)}</small>
                    </div>
                </div>
                <div>
                    <button onclick="deleteCard(${index})" class="delete-card-btn">Delete</button>
                </div>
            </div>
        `;
        
        // Add drag event listeners
        item.addEventListener('dragstart', handleDragStart);
        item.addEventListener('dragover', handleDragOver);
        item.addEventListener('drop', handleDrop);
        item.addEventListener('dragend', handleDragEnd);
        item.addEventListener('dragenter', handleDragEnter);
        item.addEventListener('dragleave', handleDragLeave);
        
        list.appendChild(item);
    });
    
    container.appendChild(list);
}

// Drag and drop variables
let draggedElement = null;
let draggedIndex = null;

// Drag event handlers
function handleDragStart(e) {
    draggedElement = e.target;
    draggedIndex = parseInt(e.target.dataset.cardIndex);
    e.target.classList.add('dragging');
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/html', e.target.outerHTML);
}

function handleDragOver(e) {
    if (e.preventDefault) {
        e.preventDefault();
    }
    e.dataTransfer.dropEffect = 'move';
    return false;
}

function handleDragEnter(e) {
    if (e.target !== draggedElement) {
        e.target.classList.add('drag-over');
    }
}

function handleDragLeave(e) {
    e.target.classList.remove('drag-over');
}

function handleDrop(e) {
    if (e.stopPropagation) {
        e.stopPropagation();
    }
    
    const dropIndex = parseInt(e.target.closest('.card-item').dataset.cardIndex);
    
    if (draggedIndex !== dropIndex) {
        // Reorder the cards
        const sortedCards = [...configuredCards].sort((a, b) => a.order - b.order);
        const draggedCard = sortedCards[draggedIndex];
        
        // Remove the dragged card from its current position
        sortedCards.splice(draggedIndex, 1);
        
        // Insert it at the new position
        sortedCards.splice(dropIndex, 0, draggedCard);
        
        // Update order values
        sortedCards.forEach((card, index) => {
            card.order = index;
        });
        
        // Update the global array
        configuredCards = sortedCards;
        
        // Save and update UI
        saveCardConfiguration();
    }
    
    return false;
}

function handleDragEnd(e) {
    e.target.classList.remove('dragging');
    
    // Clean up drag-over classes from all items
    document.querySelectorAll('.card-item').forEach(item => {
        item.classList.remove('drag-over');
    });
    
    draggedElement = null;
    draggedIndex = null;
}

// Delete a card
function deleteCard(index) {
    const sortedCards = [...configuredCards].sort((a, b) => a.order - b.order);
    const cardToDelete = sortedCards[index];
    
    if (!confirm(`Are you sure you want to delete "${cardToDelete.name}"?`)) {
        return;
    }
    
    // Remove the card from the array
    configuredCards = configuredCards.filter(card => 
        card.type !== cardToDelete.type || 
        card.config !== cardToDelete.config || 
        card.order !== cardToDelete.order
    );
    
    // Reorder remaining cards
    configuredCards.forEach((card, idx) => {
        card.order = idx;
    });
    
    // Save and update UI
    saveCardConfiguration();
    
    const globalActionStatusEl = document.getElementById('global-action-status');
    if (globalActionStatusEl) {
        globalActionStatusEl.textContent = "Card deleted successfully";
        globalActionStatusEl.className = 'status-message info';
        globalActionStatusEl.style.display = 'block';
        setTimeout(() => {
            globalActionStatusEl.style.display = 'none';
            globalActionStatusEl.textContent = '';
            globalActionStatusEl.className = 'status-message';
        }, 3000);
    }
}


// Refresh network list - UI update part will be in pollApiStatus
function _updateNetworksListUI(networks) {
    const select = document.getElementById('ssid');
    const currentVal = select.value;
    select.innerHTML = '<option value="">Select a network</option>';
    
    if (!networks || networks.length === 0) {
        select.innerHTML += '<option disabled>No networks found</option>';
        return;
    }
    
    networks.forEach(network => {
        const option = document.createElement('option');
        option.value = network.ssid;
        
        let label = network.ssid;
        if (network.rssi >= -50) label += ' (Excellent)';
        else if (network.rssi >= -60) label += ' (Good)';
        else if (network.rssi >= -70) label += ' (Fair)';
        else label += ' (Poor)';
        if (network.encrypted) label += ' 🔒';
        
        option.textContent = label;
        if (network.ssid === currentVal) {
            option.selected = true;
        }
        select.appendChild(option);
    });
}

function requestScanNetworks() {
    const globalActionStatusEl = document.getElementById('global-action-status');
    fetch('/api/actions/start-wifi-scan', { method: 'POST' })
        .then(response => {
            if (!response.ok) {
                return response.json().then(errData => {
                    throw new Error(errData.message || `HTTP error ${response.status}`);
                }).catch(() => {
                    throw new Error(`HTTP error ${response.status}`);
                });
            }
            return response.json();
        })
        .then(data => {
            if (data.status === 'queued') {
                console.log("WiFi scan action successfully queued:", data.message);
                const ssidSelect = document.getElementById('ssid');
                if (globalActionStatusEl) {
                    globalActionStatusEl.textContent = data.message || "WiFi scan requested. List will update shortly.";
                    globalActionStatusEl.className = 'status-message info';
                    globalActionStatusEl.style.display = 'block';
                    setTimeout(() => {
                        if (globalActionStatusEl.textContent === (data.message || "WiFi scan requested. List will update shortly.")) {
                            globalActionStatusEl.style.display = 'none';
                            globalActionStatusEl.textContent = '';
                            globalActionStatusEl.className = 'status-message';
                        }
                    }, 5000);
                }
                if (ssidSelect && (ssidSelect.options.length <= 1 || ssidSelect.firstChild.value === "")) {
                     ssidSelect.innerHTML = '<option>Scan requested, list updating...</option>';
                }

            } else {
                const errorMessage = `Failed to queue WiFi scan: ${data.message || 'Unexpected server response.'}`;
                console.error(errorMessage);
                if (globalActionStatusEl) {
                    globalActionStatusEl.textContent = errorMessage;
                    globalActionStatusEl.className = 'status-message error';
                    globalActionStatusEl.style.display = 'block';
                }
                document.getElementById('ssid').innerHTML = '<option>Scan request issue.</option>';
            }
        })
        .catch(error => {
            console.error('Error requesting network scan:', error.message);
            if (globalActionStatusEl) {
                globalActionStatusEl.textContent = `Error requesting scan: ${error.message}`;
                globalActionStatusEl.className = 'status-message error';
                globalActionStatusEl.style.display = 'block';
            }
            document.getElementById('ssid').innerHTML = '<option value="">Error starting scan</option>';
        });
}

// Main function to poll /api/status and update UI
let lastProcessedAction = null;
let lastProcessedActionMessage = "";
let lastWifiUpdateTime = 0;
const WIFI_UPDATE_INTERVAL = 10000; // Update WiFi list every 10 seconds

function pollApiStatus() {
    fetch('/api/status')
        .then(response => response.json())
        .then(data => {
            const portalStatus = data.portal;
            const globalActionStatusEl = document.getElementById('global-action-status');

            if (portalStatus && globalActionStatusEl) {
                if (portalStatus.action_in_progress && portalStatus.action_in_progress !== 'NONE') {
                    globalActionStatusEl.textContent = `Processing: ${portalStatus.action_in_progress.replace(/_/g, ' ').toLowerCase()}...`;
                    globalActionStatusEl.className = 'status-message info'; 
                    globalActionStatusEl.style.display = 'block';
                    lastProcessedAction = null; 
                }
                else if (portalStatus.last_action_completed && portalStatus.last_action_completed !== 'NONE') {
                    const completedActionKey = portalStatus.last_action_completed + '-' + (portalStatus.last_action_status || 'UNKNOWN') + '-' + (portalStatus.last_action_message || 'NO_MSG');

                    if (lastProcessedAction !== completedActionKey) {
                        console.log(`Action completed: ${portalStatus.last_action_completed}, Status: ${portalStatus.last_action_status}, Msg: ${portalStatus.last_action_message}`);

                        if (portalStatus.last_action_status === 'SUCCESS') {
                            let successMsg = portalStatus.last_action_message || `${portalStatus.last_action_completed.replace(/_/g, ' ')} successful.`;
                            if (!portalStatus.last_action_message) {
                                switch (portalStatus.last_action_completed) {
                                    case 'SAVE_WIFI': 
                                        successMsg = 'WiFi configuration saved. Device will attempt to connect.'; 
                                        if (data.wifi && data.wifi.is_connected) {
                                            successMsg += ` Connected to ${data.wifi.connected_ssid}.`;
                                        } else if (data.wifi) {
                                            successMsg += ' Checking connection...';
                                        }
                                        break;
                                    case 'SAVE_DEVICE_CONFIG': successMsg = 'Device configuration saved.'; break;
                                    case 'SAVE_INSIGHT': successMsg = 'New insight saved.'; break;
                                    case 'DELETE_INSIGHT': successMsg = 'Insight deleted.'; break;
                                }
                            }
                            globalActionStatusEl.textContent = successMsg;
                            globalActionStatusEl.className = 'status-message success';
                            globalActionStatusEl.style.display = 'block';

                            setTimeout(() => {
                                if (globalActionStatusEl.textContent === successMsg && !globalActionStatusEl.className.includes('info')) { 
                                    globalActionStatusEl.style.display = 'none';
                                    globalActionStatusEl.textContent = '';
                                    globalActionStatusEl.className = 'status-message';
                                }
                            }, 7000); 

                        } else if (portalStatus.last_action_status === 'ERROR') {
                            const errorMsgText = portalStatus.last_action_message || "An unknown error occurred with " + portalStatus.last_action_completed + ".";
                            console.error("Action failed:", errorMsgText);
                            if (globalActionStatusEl) {
                                globalActionStatusEl.textContent = `Status: ${errorMsgText}`;
                                globalActionStatusEl.className = 'status-message error';
                                globalActionStatusEl.style.display = 'block';
                                setTimeout(() => {
                                    if (globalActionStatusEl.className.includes('error')) {
                                        globalActionStatusEl.style.display = 'none';
                                        globalActionStatusEl.textContent = '';
                                        globalActionStatusEl.className = 'status-message';
                                    }
                                }, 10000); 
                            }
                        }
                        lastProcessedAction = completedActionKey;
                    }
                } else {
                    if (globalActionStatusEl.className.includes('info') && globalActionStatusEl.textContent.startsWith('Processing:')) {
                    } 
                }
            } else if (globalActionStatusEl && portalStatus === null) { 
                 if (globalActionStatusEl.style.display !== 'none' && !globalActionStatusEl.className.includes('success') && !globalActionStatusEl.className.includes('error')){
                    globalActionStatusEl.style.display = 'none';
                    globalActionStatusEl.textContent = '';
                    globalActionStatusEl.className = 'status-message';
                 }
            }

            // 2. Update WiFi Info
            if (data.wifi) {
                // Update WiFi networks list only if enough time has passed
                const currentTime = Date.now();
                if (currentTime - lastWifiUpdateTime >= WIFI_UPDATE_INTERVAL) {
                    _updateNetworksListUI(data.wifi.networks);
                    lastWifiUpdateTime = currentTime;
                }
                
                // Always update connection status
                const wifiStatusEl = document.getElementById('wifi-connection-status');
                if (wifiStatusEl) {
                    if (data.wifi.is_connected) {
                        wifiStatusEl.textContent = `Connected to ${data.wifi.connected_ssid} (${data.wifi.ip_address})`;
                    } else if (portalStatus && portalStatus.action_in_progress === 'SAVE_WIFI') {
                        wifiStatusEl.textContent = "Attempting to connect...";
                    } else {
                        wifiStatusEl.textContent = "Not Connected";
                    }
                }
            }


            
            // 4a. Refresh card configuration periodically
            // Note: We refresh cards on successful completion of card-related actions
            if (portalStatus && portalStatus.last_action_completed && 
                (portalStatus.last_action_completed.includes('CARD') || 
                 portalStatus.last_action_status === 'SUCCESS')) {
                // Refresh card data when actions complete
                loadConfiguredCards();
            }

            // 5. Update OTA Firmware Info & UI State
            if (data.ota) {
                updateOtaUI(data.ota, data.portal);
            }

        })
        .catch(error => {
            console.error('Error polling /api/status:', error);
            const globalActionStatusEl = document.getElementById('global-action-status');
            if(globalActionStatusEl) {
                globalActionStatusEl.textContent = 'Lost connection to device. Please check and refresh.';
                globalActionStatusEl.className = 'status-message error';
                globalActionStatusEl.style.display = 'block';
            }
        });
}


// Initialize page
document.addEventListener('DOMContentLoaded', function() {
    const hash = window.location.hash.substr(1);
    if (hash && ['config-screen'].includes(hash)) {
        showScreen(hash);
    } else {
        showScreen('config-screen');
    }

    const checkUpdateBtn = document.getElementById('check-update-btn');
    const installUpdateBtn = document.getElementById('install-update-btn');

    if (checkUpdateBtn) {
        checkUpdateBtn.addEventListener('click', requestCheckFirmwareUpdate);
    }
    if (installUpdateBtn) {
        installUpdateBtn.addEventListener('click', requestStartFirmwareUpdate);
    }

    pollApiStatus();
    setInterval(pollApiStatus, 3000); // Poll every 3 seconds for responsiveness

    const refreshBtn = document.getElementById('refresh-networks-btn');
    if(refreshBtn) {
        refreshBtn.addEventListener('click', requestScanNetworks);
    }
    
    // Initialize service and card management with small delays to avoid overwhelming the device
    setTimeout(() => {
        loadServiceDefinitions();
        setTimeout(() => {
            loadConfiguredServices();
        }, 300);
    }, 500);
    
    setTimeout(() => {
        loadCardDefinitions();
        setTimeout(() => {
            loadConfiguredCards();
        }, 500);
    }, 1000);
});

// Enum for OtaManager::UpdateStatus::State (mirror from C++)
// This helps in making the JS code more readable when checking status.
const OTA_STATUS_STATE = {
    IDLE: 0,
    CHECKING_VERSION: 1,
    DOWNLOADING: 2,
    WRITING: 3,
    SUCCESS: 4,
    ERROR_WIFI: 5,
    ERROR_HTTP_CHECK: 6,
    ERROR_HTTP_DOWNLOAD: 7,
    ERROR_JSON: 8,
    ERROR_UPDATE_BEGIN: 9,
    ERROR_UPDATE_WRITE: 10,
    ERROR_UPDATE_END: 11,
    ERROR_NO_ASSET: 12,
    ERROR_NO_SPACE: 13
};

function requestCheckFirmwareUpdate() {
    console.log("Requesting firmware update check...");
    const checkUpdateBtn = document.getElementById('check-update-btn');
    if(checkUpdateBtn) checkUpdateBtn.disabled = true;

    fetch('/api/actions/check-ota-update', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'initiated') {
                console.log('Firmware check initiated.');
            } else {
                console.error('Failed to initiate firmware check:', data.message);
                if(checkUpdateBtn) checkUpdateBtn.disabled = false;
                const updateStatusTextEl = document.getElementById('update-status-text');
                if(updateStatusTextEl) updateStatusTextEl.textContent = `${data.message || 'Could not start check.'}`;
            }
        })
        .catch(error => {
            console.error('Error requesting firmware check:', error);
            if(checkUpdateBtn) checkUpdateBtn.disabled = false;
            const updateStatusTextEl = document.getElementById('update-status-text');
            if(updateStatusTextEl) updateStatusTextEl.textContent = 'Communication error during check request.';
        });
}

function requestStartFirmwareUpdate() {
    console.log("Requesting firmware update start...");
    const installUpdateBtn = document.getElementById('install-update-btn');
    const checkUpdateBtn = document.getElementById('check-update-btn');
    if(installUpdateBtn) installUpdateBtn.disabled = true;
    if(checkUpdateBtn) checkUpdateBtn.disabled = true;

    fetch('/api/actions/start-ota-update', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'initiated') {
                console.log('Firmware update process initiated.');
            } else {
                console.error('Failed to initiate firmware update:', data.message);
                if(installUpdateBtn) installUpdateBtn.disabled = false;
                if(checkUpdateBtn) checkUpdateBtn.disabled = false;
                const updateStatusTextEl = document.getElementById('update-status-text');
                if(updateStatusTextEl) updateStatusTextEl.textContent = `Error: ${data.message || 'Could not start update.'}`;
            }
        })
        .catch(error => {
            console.error('Error requesting firmware update start:', error);
            if(installUpdateBtn) installUpdateBtn.disabled = false;
            if(checkUpdateBtn) checkUpdateBtn.disabled = false;
            const updateStatusTextEl = document.getElementById('update-status-text');
            if(updateStatusTextEl) updateStatusTextEl.textContent = 'Communication error during update request.';
        });
}

// Helper function to update OTA UI based on status from /api/status
function updateOtaUI(otaData, portalData) {
    const currentVersionEl = document.getElementById('current-version');
    const availableVersionEl = document.getElementById('available-version');
    const availableVersionContainerEl = document.getElementById('available-version-container');
    const releaseNotesEl = document.getElementById('release-notes');
    const updateAvailableSection = document.getElementById('update-available-section');
    const installUpdateBtn = document.getElementById('install-update-btn');
    const updateStatusTextEl = document.getElementById('update-status-text');
    const updateErrorEl = document.getElementById('update-error-message');
    const checkUpdateBtn = document.getElementById('check-update-btn');
    const updateProgressContainer = document.getElementById('update-progress-container');
    const updateProgressBar = document.getElementById('update-progress-bar');

    let portalOtaMessageDisplayed = false;
    if (portalData && portalData.portal_ota_action_message) {
        if (updateStatusTextEl) updateStatusTextEl.textContent = portalData.portal_ota_action_message;
        portalOtaMessageDisplayed = true;
        if (portalData.action_in_progress === 'CHECK_OTA_UPDATE' || portalData.action_in_progress === 'START_OTA_UPDATE') {
            if (checkUpdateBtn) checkUpdateBtn.disabled = true;
            if (installUpdateBtn) installUpdateBtn.disabled = true;
        } else {
        }
    }

    if (currentVersionEl) currentVersionEl.textContent = otaData.current_firmware_version || 'N/A';
    
    if (availableVersionContainerEl && availableVersionEl) {
        if (otaData.update_available && otaData.available_version && otaData.available_version !== 'N/A') {
            availableVersionEl.textContent = otaData.available_version;
            availableVersionContainerEl.style.display = '';
        } else {
            availableVersionEl.textContent = 'N/A';
            availableVersionContainerEl.style.display = 'none';
        }
    }

    if (releaseNotesEl) releaseNotesEl.textContent = otaData.update_available ? (otaData.release_notes || 'No release notes.') : '';

    if (updateAvailableSection) updateAvailableSection.style.display = otaData.update_available ? 'block' : 'none';
    
    if (installUpdateBtn) {
        installUpdateBtn.disabled = !otaData.update_available || 
                                  (portalData && (portalData.action_in_progress === 'START_OTA_UPDATE' || portalData.action_in_progress === 'CHECK_OTA_UPDATE')) ||
                                  otaData.status_code === OTA_STATUS_STATE.DOWNLOADING || 
                                  otaData.status_code === OTA_STATUS_STATE.WRITING;
    }
    
    if (checkUpdateBtn) {
        checkUpdateBtn.disabled = (portalData && (portalData.action_in_progress === 'CHECK_OTA_UPDATE' || portalData.action_in_progress === 'START_OTA_UPDATE')) ||
                                otaData.status_code === OTA_STATUS_STATE.CHECKING_VERSION || 
                                otaData.status_code === OTA_STATUS_STATE.DOWNLOADING || 
                                otaData.status_code === OTA_STATUS_STATE.WRITING;
    }

    if (!portalOtaMessageDisplayed || (portalData && portalData.portal_ota_action_message && portalData.portal_ota_action_message.includes("Successfully dispatched"))) {
        if (updateStatusTextEl) {
            let displayMessage = otaData.status_message || 'Idle';
            if (portalOtaMessageDisplayed && otaData.status_message && otaData.status_message !== "Idle") {
                updateStatusTextEl.textContent = displayMessage;
            } else if (!portalOtaMessageDisplayed) {
                 updateStatusTextEl.textContent = displayMessage;
            }
        }
    }
    
    if (updateErrorEl) {
        updateErrorEl.textContent = otaData.error_message || '';
    }

    if (otaData.status_code === OTA_STATUS_STATE.DOWNLOADING || otaData.status_code === OTA_STATUS_STATE.WRITING) {
        if (updateProgressContainer) updateProgressContainer.style.display = 'block';
        if (updateProgressBar) updateProgressBar.style.width = `${otaData.progress || 0}%`;
    } else {
        if (otaData.status_code !== OTA_STATUS_STATE.SUCCESS && updateProgressContainer) {
        }
    }
    
    if (otaData.status_code === OTA_STATUS_STATE.SUCCESS) {
        if (updateStatusTextEl) updateStatusTextEl.textContent = otaData.status_message || 'Update successful! Device will reboot.';
        if (updateProgressBar) updateProgressBar.style.width = '100%';
        if (updateProgressContainer) updateProgressContainer.style.display = 'block';
        if (installUpdateBtn) installUpdateBtn.disabled = true;
        if (checkUpdateBtn) checkUpdateBtn.disabled = true;
    }
}