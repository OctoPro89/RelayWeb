let mylist = [];
const torrentListContainer = document.getElementById("torrent-list");
const selectedTitle = document.getElementById("selected-torrent-title");
const mainHeaderRegion = document.getElementById("main-header-region");
const mainProgressBar = document.getElementById("main-progress-bar");
const metricsGridContainer = document.getElementById("metrics-grid-container");
const fileViewContainer = document.getElementById("file-view-container");
const searchInput = document.getElementById("search-input");

function formatBytes(bytes, decimals = 2) {
    if (!bytes || bytes === 0) return "0 Bytes";
    const k = 1024;
    const dm = decimals < 0 ? 0 : decimals;
    const sizes = ["B", "KB", "MB", "GB", "TB"];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + " " + sizes[i];
}

function formatSpeed(bytesPerSec) {
    if (!bytesPerSec || bytesPerSec === 0) return "0 B/s";
    return formatBytes(bytesPerSec, 1) + "/s";
}

function formatETA(seconds) {
    if (seconds === null || seconds === undefined || seconds === 0) return "∞";
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h.toString().padStart(2, "0")}h ${m.toString().padStart(2, "0")}m ${s.toString().padStart(2, "0")}s`;
}

searchInput.addEventListener("input", function (e) {
    const query = e.target.value.toLowerCase();
    const filtered = mylist.filter(
        (torrent) =>
            torrent.title.toLowerCase().includes(query) ||
            torrent.id.toString().includes(query),
    );
    updateMylistUI(filtered);
});

function updateMylistUI(itemsToRender = mylist) {
    torrentListContainer.innerHTML = "";
    if (itemsToRender.length === 0) {
        torrentListContainer.innerHTML =
            '<div class="empty-state">No torrents found</div>';
        return;
    }

    itemsToRender.forEach((torrent) => {
        const item = document.createElement("div");
        item.className = "torrent-item";
        item.innerHTML = `
            <div class="torrent-title">${torrent.title}</div>
            <div class="torrent-meta">
                Status: <span class="status-badge status-${torrent.state}">${torrent.state}</span> • ${torrent.progress}%<br>
                Size: ${formatBytes(torrent.size)} • DL: ${formatSpeed(torrent.downloadSpeed)}
            </div>
            <div class="progress-container"><div class="progress-bar" style="width: ${torrent.progress}%"></div></div>
        `;
        item.addEventListener("click", () => {
            document.querySelectorAll(".torrent-item").forEach((el) => el.classList.remove("active"));
            item.classList.add("active");
            renderTorrentDetails(torrent);
        });

        const deleteBtn = document.createElement("button");
        deleteBtn.className = "btn-delete-torrent";
        deleteBtn.innerHTML = "&times;"; // visual "X" closing element icon symbol
        deleteBtn.title = "Delete Torrent";
        
        // prevent click from selecting the torrent card when deleting
        deleteBtn.onclick = (e) => {
            e.stopPropagation(); 
            deleteTorrent(torrent.id);
        };
        
        item.appendChild(deleteBtn);
        torrentListContainer.appendChild(item);
    });
}

function renderTorrentDetails(torrent) {
    mainHeaderRegion.style.display = "block";
    selectedTitle.textContent = torrent.title;
    mainProgressBar.style.width = `${torrent.progress}%`;

    metricsGridContainer.innerHTML = `
        <div class="metric-card"><div class="metric-label">Torrent ID</div><div class="metric-value" style="text-transform: capitalize;">${torrent.id}</div></div>
        <div class="metric-card"><div class="metric-label">Status</div><div class="metric-value" style="text-transform: capitalize;">${torrent.state}</div></div>
        <div class="metric-card"><div class="metric-label">Total Size</div><div class="metric-value">${formatBytes(torrent.size)}</div></div>
        <div class="metric-card"><div class="metric-label">Seeds / Peers</div><div class="metric-value">${torrent.seeds} / ${torrent.peers}</div></div>
        <div class="metric-card"><div class="metric-label">Up Speed</div><div class="metric-value">${formatSpeed(torrent.uploadSpeed)}</div></div>
        <div class="metric-card"><div class="metric-label">Down Speed</div><div class="metric-value">${formatSpeed(torrent.downloadSpeed)}</div></div>
    `;
    /*
        <div class="metric-card"><div class="metric-label">Share Ratio</div><div class="metric-value">${torrent.ratio}</div></div>
        <div class="metric-card"><div class="metric-label">ETA Remaining</div><div class="metric-value">${formatETA(torrent.eta)}</div></div>
        <div class="metric-card" style="grid-column: span 2;"><div class="metric-label">Info Hash</div><div class="metric-value" style="font-size: 0.75rem; word-break: break-all; font-family: monospace;">${torrent.hash}</div></div>
    */
    if (!torrent.files || torrent.files.length === 0) {
        fileViewContainer.innerHTML = '<div class="empty-state">No files listed in this torrent pack.</div>';
        return;
    }

    let tableHTML = `
        <table class="file-table">
        <thead>
            <tr>
            <th style="width: 70px;">ID</th>
            <th>File Name</th>
            <th style="width: 110px; text-align: right;">Size</th>
            <th style="width: 150px; text-align: center;">Action</th>
            </tr>
        </thead>
        <tbody>
    `;

    torrent.files.forEach((file) => {
        tableHTML += `<tr>
            <td>${file.id}</td>
            <td style="word-break: break-all; font-weight: 500;">${file.title}</td>
            <td style="text-align: right; font-family: monospace;">${formatBytes(file.size)}</td>
            <td style="text-align: center;">
                <button class="btn-dl" onclick="requestDirectDownload('${torrent.id}', '${file.id}', '${escapeHtml(file.title)}')">Request Link</button>
            </td>
        </tr>`;
    });
    tableHTML += `</tbody></table>`;
    fileViewContainer.innerHTML = tableHTML;
}

async function requestDirectDownload(torrentId, fileId, fileName) {
    const res = await fetch(`/api/requestdl?torrent_id=${torrentId}&file_id=${fileId}`, { method: 'GET' });

    const data = await res.json();
    if (!data.success) {
        alert(`Error getting direct download link: ${data.error}`);
    } else {
        navigator.clipboard.writeText(data.data);
        alert('Success! Copied direct download link to clipboard');
    }
}

function escapeHtml(string) {
    return String(string).replace(/'/g, "\\'").replace(/"/g, "&quot;");
}

// opens the Modal View Panel
function createTorrent() {
    document.getElementById("create-torrent-modal").style.display = "flex";
}

// closes and clears form input states
function closeCreateModal() {
    document.getElementById("create-torrent-modal").style.display = "none";
    document.getElementById("modal-magnet-input").value = "";
    document.getElementById("modal-seed-select").value = "auto";
    document.getElementById("modal-allow-zip").checked = false;
}

// extracts payload parameters and passes them downstream
function submitCreateTorrent() {
    const magnetValue = document.getElementById("modal-magnet-input").value.trim();
    const seedStrategy = document.getElementById("modal-seed-select").value;
    const allowZip = document.getElementById("modal-allow-zip").checked;

    if (!magnetValue) {
        alert("Please paste a valid magnet link text reference sequence.");
        return;
    }

    const payload = {
        magnet: magnetValue,
        seeding_option: seedStrategy,
        allow_zip: allowZip,
    };

    alert("TODO")
    
    closeCreateModal();
}

function deleteTorrent() {
    alert("deleting torrent");
}

async function getMyList() {
    const url = '/api/mylist';

    fetch(url, {
        method: 'GET'
    })
    .then(response => {
        if (!response.ok) {
            throw new Error('Network response failed');
        }
        return response.json();
    })
    .then(async (parsed) => {
        try {
            const listData = parsed.data || parsed;
            mylist = [];

            const itemsArray = Array.isArray(listData) ? listData : [listData];
            itemsArray.forEach((torrent) => {
                const object = {
                    id: torrent.id,
                    title: torrent.name || "Unknown Name",
                    hash: torrent.hash || "N/A",
                    size: torrent.size || 0,
                    state: torrent.download_state || "unknown",
                    progress: torrent.progress !== undefined ? (torrent.progress * 100).toFixed(1) : 0,
                    seeds: torrent.seeds || 0,
                    peers: torrent.peers || 0,
                    ratio: torrent.ratio || 0,
                    downloadSpeed: torrent.download_speed || 0,
                    uploadSpeed: torrent.upload_speed || 0,
                    eta: torrent.eta || 0,
                    files: [],
                };

                if (torrent.files) {
                    torrent.files.forEach((f) => {
                        object.files.push({
                            title: f.short_name || f.name || `File ID: ${f.id}`,
                            id: f.id,
                            size: f.size || 0,
                            hash: f.hash || object.hash,
                        });
                    });
                    object.files.sort((a, b) => {
                        const numA = a.title.match(/\d+/)?.[0] || 0;
                        const numB = b.title.match(/\d+/)?.[0] || 0;
                        return Number(numA) - Number(numB);
                    });
                }
                mylist.push(object);
            });

            searchInput.value = "";
            updateMylistUI(mylist);
        } catch (err) {
            alert("Invalid JSON format structural reference: " + err.message);
        }
    })
    .catch(error => {
        alert(`Error: ${error}`);
    });
}

getMyList();