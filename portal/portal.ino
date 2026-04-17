#include "esp_mac.h"
#include "esp_wifi.h"
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

const bool DEBUG = true;
const bool UNDERCLOCK = false;

const String FIREBASE_PROJECT = "punto-8888";
const String FIREBASE_APIKEY = "AIzaSyA6sA_c3yNUZvvo_dZanhydLn7jXl-55hU";
