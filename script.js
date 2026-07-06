import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";

const firebaseConfig = {
  apiKey: "AIzaSyARYH1P-dQ8-tbp4BnhcThHqqNuLaZmUxU",
  authDomain: "tracker-989a9.firebaseapp.com",
  databaseURL: "https://tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app",
  projectId: "tracker-989a9",
  storageBucket: "tracker-989a9.firebasestorage.app",
  messagingSenderId: "523207717217",
  appId: "1:523207717217:web:b7ea5a4e90b0a653aed520"
};

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

let map = L.map("map").setView([48.2, 16.3], 15);

L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
  maxZoom: 19,
  attribution: "&copy; OpenStreetMap"
}).addTo(map);

let marker = L.marker([48.2, 16.3]).addTo(map);
let firstPosition = true;
let lastDataTime = 0;
let lastValidPosition = null;

const liveRef = ref(db, "tracker/live");

onValue(liveRef, (snapshot) => {
  const data = snapshot.val();

  if (!data || data.lat === undefined || data.lng === undefined) {
    setOfflineValues("Keine Daten");
    return;
  }

  const lat = Number(data.lat);
  const lng = Number(data.lng);

  if (Number.isNaN(lat) || Number.isNaN(lng)) {
    setOfflineValues("GPS ungültig");
    return;
  }

  lastDataTime = Date.now();
  lastValidPosition = { lat, lng };
  setStatus("Online", true);

  document.getElementById("speed").innerText =
    data.speed_kmh !== undefined ? Number(data.speed_kmh).toFixed(1) : "---";

  document.getElementById("sat").innerText =
    data.satellites !== undefined ? data.satellites : "---";

  const now = new Date();
  const timeText = now.toLocaleTimeString("de-AT");
  document.getElementById("lastUpdateSmall").innerText = timeText;

  document.getElementById("battery").innerText =
    data.battery_v !== undefined ? Number(data.battery_v).toFixed(1) : "---";

  document.getElementById("rpm").innerText =
    data.rpm !== undefined ? Math.round(Number(data.rpm)) : "---";

  document.getElementById("oilpressure").innerText =
    data.oil_pressure !== undefined ? Number(data.oil_pressure).toFixed(1) : "---";

  document.getElementById("oiltemp").innerText =
    data.oil_temp !== undefined ? Math.round(Number(data.oil_temp)) : "---";

  document.getElementById("cyltemp").innerText =
    data.cylinder_temp !== undefined ? Math.round(Number(data.cylinder_temp)) : "---";

  marker.setLatLng([lat, lng]);

  if (firstPosition) {
    map.setView([lat, lng], 17);
    firstPosition = false;
  } else {
    map.panTo([lat, lng]);
  }

  const mapsButton = document.getElementById("mapsButton");
  mapsButton.href = "https://www.google.com/maps?q=" + lat + "," + lng;
  mapsButton.classList.remove("disabled");
});

setInterval(() => {
  if (lastDataTime === 0) {
    setOfflineValues("Keine Daten");
    return;
  }

  const ageSeconds = (Date.now() - lastDataTime) / 1000;

  if (ageSeconds > 10) {
    setOfflineValues("Offline");
  } else {
    setStatus("Online", true);
  }
}, 1000);

function setStatus(text, isOnline) {
  const status = document.getElementById("status");
  const dot = document.getElementById("statusDot");

  status.innerText = text;
  status.classList.remove("online", "offline");
  dot.classList.remove("online-dot", "offline-dot");

  if (isOnline) {
    status.classList.add("online");
    dot.classList.add("online-dot");
  } else {
    status.classList.add("offline");
    dot.classList.add("offline-dot");
  }
}

function setOfflineValues(statusText) {
  setStatus(statusText, false);

  document.getElementById("speed").innerText = "---";
  document.getElementById("sat").innerText = "---";
  document.getElementById("battery").innerText = "---";
  document.getElementById("rpm").innerText = "---";
  document.getElementById("oilpressure").innerText = "---";
  document.getElementById("oiltemp").innerText = "---";
  document.getElementById("cyltemp").innerText = "---";

  if (lastDataTime === 0) {
    document.getElementById("lastUpdateSmall").innerText = "---";
  }

  const mapsButton = document.getElementById("mapsButton");

  if (lastValidPosition) {
    mapsButton.href = "https://www.google.com/maps?q=" + lastValidPosition.lat + "," + lastValidPosition.lng;
    mapsButton.classList.remove("disabled");
  } else {
    mapsButton.href = "#";
    mapsButton.classList.add("disabled");
  }
}
