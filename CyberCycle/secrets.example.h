#pragma once

// Copy this file to secrets.h and list every Wi-Fi network the T-QT should be
// able to update from. secrets.h is ignored by Git and must never be committed.
//
// Order does not matter. At each check the board scans, keeps the networks it
// recognises and joins the strongest one, so it updates itself wherever it
// happens to be. Add as many lines as you like - home, office, a phone
// hotspot. Mind the trailing backslash on every line but the last, and the
// comma at the end of each entry.
//
// The networks compiled into the firmware GitHub publishes come from the
// repository secrets instead, not from this file. See README section 2.

#define OTA_WIFI_NETWORKS \
  { "your-wifi-name",  "your-wifi-password" }, \
  { "another-network", "its-password" },
