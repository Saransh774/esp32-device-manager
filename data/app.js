 // State Management
let distanceHistory = [];
const maxHistory = 30;
let lastSeen = Date.now();

// UI Elements
const distanceEl = document.getElementById('distance');
const relayBtn = document.getElementById('relay-btn');
const autoModeCb = document.getElementById('auto-mode-cb');
const thresholdVal = document.getElementById('threshold-val');
const thresholdSlider = document.getElementById('threshold-slider');
const rssiEl = document.getElementById('rssi');
const lastSeenEl = document.getElementById('last-seen');
const statusBadge = document.getElementById('c3-status');
const canvas = document.getElementById('distance-chart');
const ctx = canvas.getContext('2d');

// SSE Connection
const source = new EventSource('/events');

source.onmessage = (event) => {
    const data = JSON.parse(event.data);
    updateDashboard(data);
};

source.addEventListener('ota_progress', (event) => {
    const data = JSON.parse(event.data);
    updateOTAProgress(data);
});

function updateDashboard(data) {
    // Update Distance
    distanceEl.innerText = data.distance;
    updateSparkline(data.distance);

    // Update Relay
    relayBtn.innerText = data.relay ? 'ON' : 'OFF';
    relayBtn.className = data.relay ? 'btn btn-on' : 'btn btn-off';
    autoModeCb.checked = data.auto;
    thresholdVal.innerText = data.threshold;
    thresholdSlider.value = data.threshold;

    // Update Status
    rssiEl.innerText = data.rssi || 'N/A';
    lastSeen = Date.now();
    statusBadge.innerText = 'C3: Online';
    statusBadge.className = 'status-badge online';
}

function updateSparkline(newVal) {
    distanceHistory.push(newVal);
    if (distanceHistory.length > maxHistory) distanceHistory.shift();

    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.beginPath();
    ctx.strokeStyle = '#38bdf8';
    ctx.lineWidth = 2;

    const xStep = canvas.width / (maxHistory - 1);
    const yScale = canvas.height / 200; // Assuming 0-200cm range

    distanceHistory.forEach((val, i) => {
        const x = i * xStep;
        const y = canvas.height - (val * yScale);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });
    ctx.stroke();
}

// Control Handlers
relayBtn.onclick = () => sendRelayUpdate({ relay: relayBtn.innerText === 'OFF' });
autoModeCb.onchange = () => sendRelayUpdate({ auto: autoModeCb.checked });
thresholdSlider.oninput = () => thresholdVal.innerText = thresholdSlider.value;
thresholdSlider.onchange = () => sendRelayUpdate({ threshold: parseInt(thresholdSlider.value) });

async function sendRelayUpdate(payload) {
    await fetch('/relay', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
}

// OTA Update
document.getElementById('update-btn').onclick = async () => {
    document.getElementById('progress-container').style.display = 'block';
    document.getElementById('ota-status').innerText = 'Initializing...';
    await fetch('/ota/update', { method: 'POST' });
};

function updateOTAProgress(data) {
    const bar = document.getElementById('progress-bar');
    const msg = document.getElementById('ota-status');
    bar.style.width = data.percent + '%';
    bar.innerText = data.percent + '%';
    msg.innerText = data.message;
    if (data.percent === 100) {
        setTimeout(() => {
            document.getElementById('progress-container').style.display = 'none';
            msg.innerText = 'Update Complete!';
        }, 2000);
    }
}

// Last Seen Timer
setInterval(() => {
    const diff = Math.floor((Date.now() - lastSeen) / 1000);
    lastSeenEl.innerText = diff + 's ago';
    if (diff > 5) {
        statusBadge.innerText = 'C3: Offline';
        statusBadge.className = 'status-badge offline';
    }
}, 1000);
