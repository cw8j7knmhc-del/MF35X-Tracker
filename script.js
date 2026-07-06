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

const liveRef = ref(db, "tracker/live");

onValue(liveRef, (snapshot) => {
  const data = snapshot.val();

  if (!data || data.lat === undefined || data.lng === undefined) {
    setStatus("Keine Daten", false);
    return;
  }

  const lat = Number(data.lat);
  const lng = Number(data.lng);

  if (Number.isNaN(lat) || Number.isNaN(lng)) {
    setStatus("GPS ungültig", false);
    return;
  }

  lastDataTime = Date.now();
  setStatus("Online", true);

  document.getElementById("speed").innerText = Number(data.speed_kmh ?? 0).toFixed(1);
  document.getElementById("sat").innerText = data.satellites ?? 0;

  const now = new Date();
  document.getElementById("lastUpdateCard").innerText = now.toLocaleTimeString("de-AT");

  document.getElementById("battery").innerText =
    data.battery_v !== undefined ? Number(data.battery_v).toFixed(1) : "---";

  document.getElementById("oilpressure").innerText =
    data.oil_pressure !== undefined
    ? Number(data.oil_pressure).toFixed(1)
    : "---";

document.getElementById("oiltemp").innerText =
    data.oil_temp !== undefined
    ? Math.round(Number(data.oil_temp))
    : "---";

  document.getElementById("U/min").innerText =
    data.rpm !== undefined ? Math.round(Number(data.rpm)) : "---";

  marker.setLatLng([lat, lng]);

  if (firstPosition) {
    map.setView([lat, lng], 17);
    firstPosition = false;
  } else {
    map.panTo([lat, lng]);
  }

  document.getElementById("mapsButton").href =
    "https://www.google.com/maps?q=" + lat + "," + lng;
});

setInterval(() => {
  if (lastDataTime === 0) {
    setStatus("Keine Daten", false);
    setOfflineDisplay();
    return;
  }

  const ageSeconds = (Date.now() - lastDataTime) / 1000;

  if (ageSeconds > 10) {
    setStatus("Offline", false);
    setOfflineDisplay();
  } else {
    setStatus("Online", true);
  }
}, 1000);

function setOfflineDisplay() {
  document.getElementById("speed").innerText = "---";
  document.getElementById("sat").innerText = "---";
  document.getElementById("battery").innerText = "---";
  document.getElementById("oilpressure").innerText="---";
  document.getElementById("oiltemp").innerText="---";
  document.getElementById("U/min").innerText = "---";
}
