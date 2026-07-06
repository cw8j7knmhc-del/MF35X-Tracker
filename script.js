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

// Startposition Österreich, wird sofort ersetzt sobald Firebase-Daten kommen
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
    document.getElementById("status").innerText = "Keine Daten";
    return;
  }

  const lat = Number(data.lat);
  const lng = Number(data.lng);

  if (Number.isNaN(lat) || Number.isNaN(lng)) {
    document.getElementById("status").innerText = "GPS ungültig";
    return;
  }
lastDataTime = Date.now();
document.getElementById("status").innerText = "Online";
  document.getElementById("speed").innerText = Number(data.speed_kmh ?? 0).toFixed(1);
  document.getElementById("sat").innerText = data.satellites ?? 0;
  document.getElementById("coords").innerText = lat.toFixed(6) + ", " + lng.toFixed(6);

  const now = new Date();
  document.getElementById("lastUpdate").innerText = now.toLocaleTimeString("de-AT");

  marker.setLatLng([lat, lng]);

  if (firstPosition) {
    map.setView([lat, lng], 17);
    firstPosition = false;
  } else {
    map.panTo([lat, lng]);
  }

  const mapsLink = "https://www.google.com/maps?q=" + lat + "," + lng;
  document.getElementById("mapsButton").href = mapsLink;
});
