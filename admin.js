// MF35X Admin loader
// Bestehende, bestätigte Admin-Logik unverändert laden und nur gezielte Fixes ergänzen.
await import(`./admin-core.js?v=9.5.11-${Date.now()}`);
await import(`./admin-reset-fix.js?v=9.5.12-${Date.now()}`);
