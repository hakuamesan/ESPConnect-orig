#include "ESPConnect.h"


/*
  Check if ESPConnect was configured before
*/

bool ESPConnectClass::isConfigured() {
  load_sta_credentials();
  return _sta_ssid.length() > 0;
}


/* 
  Loads STA Credentials into memory
*/

void ESPConnectClass::load_sta_credentials(){
  // Get saved WiFi Credentials
    Preferences preferences;
    preferences.begin("espconnect", false);
    _sta_ssid = preferences.getString("ssid", "");
    _sta_password = preferences.getString("password", "");
    preferences.end();
}


/*
  Start Captive Portal and Attach DNS & Webserver
*/
bool ESPConnectClass::start_portal(){
  bool configured = false;
  ESPCONNECT_SERIAL("Starting Captive Portal\n");
  // Try Connecting Station
  WiFi.mode(WIFI_AP_STA);
  // Start Access Point
  WiFi.softAP(_auto_connect_ssid.c_str(), _auto_connect_password.c_str());

  if(_sta_ssid != ""){
    WiFi.begin(_sta_ssid.c_str(), _sta_password.c_str());
  }

  // Start our DNS Server
  _dns = new DNSServer();
  _dns->setErrorReplyCode(DNSReplyCode::NoError);
  _dns->start(53, "*", WiFi.softAPIP());

  auto scanGET = _server->on("/espconnect/scan", HTTP_GET, [&](AsyncWebServerRequest *request){
    String json = "[";
    int n = WiFi.scanComplete();
    if(n == WIFI_SCAN_FAILED){
      WiFi.scanNetworks(true);
      return request->send(202);
    }else if(n == WIFI_SCAN_RUNNING){
      return request->send(202);
    }else{
      for (int i = 0; i < n; ++i){
        String ssid = WiFi.SSID(i);
        // Escape invalid characters
        ssid.replace("\\","\\\\");
        ssid.replace("\"","\\\"");
        json+="{";
        json+="\"name\":\""+ssid+"\",";
        json+="\"open\":"+String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true": "false");
        json+="}";
        if(i != n-1) json+=",";
      }
      WiFi.scanDelete();
      if(WiFi.scanComplete() == -2){
        WiFi.scanNetworks(true);
      }
    }
    json += "]";
    request->send(200, "application/json", json);
    json = String();
  });

  // Accept incomming WiFi Credentials
  auto connectPOST = _server->on("/espconnect/connect", HTTP_POST, [&](AsyncWebServerRequest *request){
    // Get FormData
    String ssid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value().c_str() : "";
    String password = request->hasParam("password", true) ? request->getParam("password", true)->value().c_str() : "";

    if(ssid.length() <= 0){
      return request->send(403, "application/json", "{\"message\":\"Invalid SSID\"}");
    }

    if(ssid.length() > 32 || password.length() > 64){
      return request->send(403, "application/json", "{\"message\":\"Credentials exceed character limit of 32 & 64 respectively.\"}");
    }
    
    // Save WiFi Credentials in Flash
    int ok = 0;
      Preferences preferences;
      preferences.begin("espconnect", false);
      preferences.putString("ssid", ssid.c_str());
      preferences.putString("password", password.c_str());
      preferences.end();

      if(ok == ESP_OK){
        configured = true;
        _sta_ssid = ssid;
        _sta_password = password;
        WiFi.begin(_sta_ssid.c_str(), _sta_password.c_str());
        request->send(200, "application/json", "{\"message\":\"Credentials Saved. Rebooting...\"}");
      }else{
        Serial.printf("WiFi config failed with: %d\n", ok);
        return request->send(500, "application/json", "{\"message\":\"Error while saving WiFi Credentials: "+String(ok)+"\"}");
      }
  });
  
  auto indexGET = _server->on("/espconnect", HTTP_GET, [&](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", ESPCONNECT_HTML, ESPCONNECT_HTML_SIZE);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  _server->onNotFound([&](AsyncWebServerRequest *request){
    //Serial.printf(ESPCONNECT_HTML.c_str());
    Serial.println("Headers:");
    Serial.printf(request->header());
    Serial.println(request->url());
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", ESPCONNECT_HTML, ESPCONNECT_HTML_SIZE);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  auto portalRewrite = _server->rewrite("/", "/espconnect").setFilter(ON_AP_FILTER);

  // Begin Webserver    
  _server->begin();

  unsigned long lastMillis = millis();
  while(WiFi.status() != WL_CONNECTED && (unsigned long)(millis() - lastMillis) < _auto_connect_timeout){
    _dns->processNextRequest();
    yield();
  }

  if(WiFi.status() != WL_CONNECTED){
    ESPCONNECT_SERIAL("Portal timed out\n");
  }else{
    ESPCONNECT_SERIAL("Connected to STA\n");
  }

  _server->removeHandler(&indexGET);
  _server->removeHandler(&scanGET);
  _server->removeHandler(&connectPOST);
  _server->removeRewrite(&portalRewrite);
  _server->onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });
  
  _dns->stop();
  delete _dns;
  _dns = nullptr;
  
  ESPCONNECT_SERIAL("Closed Portal\n");
  WiFi.softAPdisconnect(true);
  if(configured){
    WiFi.mode(WIFI_STA);
    WiFi.begin(_sta_ssid.c_str(), _sta_password.c_str());
    return true;
  }else{
    return false;
  }
}


/*
  Set Custom AP Credentials
*/
void ESPConnectClass::autoConnect(const char* ssid, const char* password, unsigned long timeout){
  _auto_connect_ssid = ssid;
  _auto_connect_password = password;
  _auto_connect_timeout = timeout;
}


/*
  Connect to saved WiFi Credentials
*/
bool ESPConnectClass::begin(AsyncWebServer* server, unsigned long timeout){
  _server = server;

  load_sta_credentials();

  Serial.println("Beginning..."); delay(1000);
  Serial.printf("SSID:");
  Serial.println(_sta_ssid);

  if(_sta_ssid != ""){
    ESPCONNECT_SERIAL("STA Pre-configured:\n");
    ESPCONNECT_SERIAL("SSID: "+_sta_ssid+"\n");
    ESPCONNECT_SERIAL("Password: "+_sta_password+"\n\n");
    ESPCONNECT_SERIAL("Connecting to STA [");
    
    // Try connecting to STA
    WiFi.persistent(false);
    WiFi.setAutoConnect(false);
    WiFi.mode(WIFI_STA);
    Serial.printf("SSID:");
    Serial.println(_sta_ssid);
    Serial.printf("PWD:");
    Serial.println(_sta_password);
  
    WiFi.begin(_sta_ssid.c_str(), _sta_password.c_str());

    // Check WiFi connection status till timeout
    unsigned long lastMillis = millis();
    while(WiFi.status() != WL_CONNECTED && (unsigned long)(millis() - lastMillis) < timeout){
      Serial.print("#");
      delay(500);
      yield();
    }
    Serial.print("]\n");

    if(WiFi.status() != WL_CONNECTED){
      ESPCONNECT_SERIAL("Connection to STA Falied [!]\n");
    }
  }

  // Start Captive Portal if not connected to STA
  if(WiFi.status() != WL_CONNECTED){
    // Start captive portal
    return start_portal();
  }else{
    ESPCONNECT_SERIAL("Connected to STA\n");
    return true;
  }
}


/*
  Erase Stored WiFi Credentials
*/
bool ESPConnectClass::erase(){
    Preferences preferences;
    preferences.begin("espconnect", false);
    preferences.putString("ssid", "");
    preferences.putString("password", "");
    preferences.end();
    WiFi.disconnect(true, true);
    return true;
}


/*
  Return Connection Status
*/
bool ESPConnectClass::isConnected(){
  return (WiFi.status() == WL_CONNECTED);
}

String ESPConnectClass::getSSID(){
  return _sta_ssid;
}

String ESPConnectClass::getPassword(){
  return _sta_password;
}

ESPConnectClass ESPConnect;
