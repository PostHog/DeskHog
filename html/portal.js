function showScreen(screenId) {
    const screens = ['config-screen', 'success-screen', 'error-screen'];
    screens.forEach(id => {
        document.getElementById(id).classList.add('hidden');
    });
    document.getElementById(screenId).classList.remove('hidden');
    
    // Update page title
    let title = "DeskHog Configuration";
    if (screenId === 'success-screen') title = "Configuration Saved";
    else if (screenId === 'error-screen') title = "Configuration Error";
    document.getElementById('page-title').textContent = title;
    
    // Start countdown if success screen
    if (screenId === 'success-screen') {
        startCountdown();
        document.getElementById('progress-bar').style.width = '100%';
    }
}

// Handle WiFi form submission
async function saveWifiConfig() {
    try {
        const form = document.getElementById('wifi-form');
        const formData = new FormData(form);
        
        const response = await fetch('/save-wifi', {
            method: 'POST',
            body: formData
        });
        const data = await response.json();
        
        if (data.success) {
            showScreen('success-screen');
        } else {
            showScreen('error-screen');
        }
    } catch (error) {
        console.error('Error saving WiFi config:', error);
        showScreen('error-screen');
    }
    return false; // Prevent default form submission
}

// Handle device config form submission
async function saveDeviceConfig() {
    try {
        const form = document.getElementById('device-form');
        const formData = new FormData(form);
        
        const response = await fetch('/save-device-config', {
            method: 'POST',
            body: formData
        });
        const data = await response.json();
        
        if (data.success) {
            showScreen('success-screen');
        } else {
            showScreen('error-screen');
        }
    } catch (error) {
        console.error('Error saving device config:', error);
        showScreen('error-screen');
    }
    return false;
}

// Toggle API key visibility
function toggleApiKeyVisibility() {
    const apiKeyInput = document.getElementById('apiKey');
    apiKeyInput.type = apiKeyInput.type === 'password' ? 'text' : 'password';
}

// Add new insight
async function addInsight() {
    try {
        const form = document.getElementById('insight-form');
        const formData = new FormData(form);
        
        const response = await fetch('/save-insight', {
            method: 'POST',
            body: formData
        });
        const data = await response.json();
        
        if (data.success) {
            form.reset();
            await loadInsights();
        } else {
            showScreen('error-screen');
        }
    } catch (error) {
        console.error('Error adding insight:', error);
        showScreen('error-screen');
    }
    return false;
}

// Delete insight
async function deleteInsight(id) {
    if (!confirm('Are you sure you want to delete this insight?')) {
        return;
    }
    
    try {
        const response = await fetch('/delete-insight', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ id: id })
        });
        const data = await response.json();
        
        if (data.success) {
            await loadInsights();
        } else {
            showScreen('error-screen');
        }
    } catch (error) {
        console.error('Error deleting insight:', error);
        showScreen('error-screen');
    }
}

// Load insights list
async function loadInsights() {
    try {
        const response = await fetch('/get-insights');
        const data = await response.json();
        
        const container = document.getElementById('insights-list');
        container.innerHTML = '';
        
        if (data.insights.length === 0) {
            container.innerHTML = '<p>No insights configured</p>';
            return;
        }
        
        const list = document.createElement('ul');
        list.className = 'insights-list';
        
        data.insights.forEach(insight => {
            const item = document.createElement('li');
            item.className = 'insight-item';
            item.innerHTML = `
                <button onclick="deleteInsight('${insight.id}')" class="button danger">Delete ${insight.title}</button>
            `;
            list.appendChild(item);
        });
        
        container.appendChild(list);
    } catch (error) {
        console.error('Error loading insights:', error);
        const container = document.getElementById('insights-list');
        container.innerHTML = '<p>Error loading insights</p>';
    }
}

// Refresh network list
async function refreshNetworks(config) {
    try {
        const response = await fetch('/scan-networks');
        const data = await response.json();
        
        const { ssid } = config;
        const select = document.getElementById('ssid');
        select.innerHTML = '<option value="">Select a network</option>';
        
        if (!data.networks || data.networks.length === 0) {
            select.innerHTML += '<option disabled>No networks found</option>';
            return;
        }
        
        data.networks.forEach(network => {
            const option = document.createElement('option');
            option.value = network.ssid;
            
            let label = network.ssid;
            
            // Add signal strength indicator
            if (network.rssi >= -50) {
                label += ' (Excellent)';
            } else if (network.rssi >= -60) {
                label += ' (Good)';
            } else if (network.rssi >= -70) {
                label += ' (Fair)';
            } else {
                label += ' (Poor)';
            }
            
            // Add lock icon for encrypted networks
            if (network.encrypted) {
                label += ' 🔒';
            }

            if (ssid === network.ssid) {
                option.selected = true;
            }
            
            option.textContent = label;
            select.appendChild(option);
        });
    } catch (error) {
        console.error('Error refreshing networks:', error);
        const select = document.getElementById('ssid');
        select.innerHTML = '<option value="">Error loading networks</option>';
    }
}

// Start countdown on success screen
function startCountdown() {
    let seconds = 10;
    const countdownEl = document.getElementById('countdown');
    
    const interval = setInterval(() => {
        seconds--;
        countdownEl.textContent = seconds;
        
        if (seconds <= 0) {
            clearInterval(interval);
            window.location.href = '/';
        }
    }, 1000);
}

// Load current configuration
async function loadCurrentConfig() {
    try {
        const response = await fetch('/get-device-config');
        const data = await response.json();
        
        if (data.teamId !== undefined) {
            document.getElementById('teamId').value = data.teamId;
        }
        if (data.apiKey) {
            document.getElementById('apiKey').value = data.apiKey;
        }
        if (data.baseUrl) {
            document.getElementById('baseUrl').value = data.baseUrl;
        }
        return data;
    } catch (error) {
        console.error('Error loading device config:', error);
        throw error;
    }
}

// Initialize page
document.addEventListener('DOMContentLoaded', async function() {
    // Check if we need to show a specific screen based on URL hash
    const hash = window.location.hash.substr(1);
    if (hash && ['config-screen', 'success-screen', 'error-screen'].includes(hash)) {
        showScreen(hash);
    }
    
    // Load current configuration
    const config = await loadCurrentConfig();
    
    // Load insights list
    await loadInsights();

    // Populate the networks list
    await refreshNetworks(config);
});