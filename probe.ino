// /!\ Work In Progress
// change password line 25
// greatly inspired by http://blog.idleman.fr/raspberry-pi-24-creer-une-multi-sonde-wifi-pour-11e/

//Inclusion librairie WIFI
#include <ESP8266WiFi.h>
//Inclusion librairie utilisation de l'eeprom (mémoire interne)
#include <EEPROM.h>
//Inclusion librairie mode serveur web
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <FS.h>
//Inclusion librairie mode client web
#include <ESP8266HTTPClient.h>
//Inclusion librairie utilisation DHT (composant temperature / humidité)
#include <DHTesp.h>

// DHT related variables.
DHTesp DHT;
TempAndHumidity dhtData;

//Déclaration du SSID et du mot de passe WIFI
// Une fois connecté Le hotspot est accessible via l'adresse 192.168.4.1
const String WIFI_SSID = "ESP8266";
const char WIFI_PASSWORD[] = "123";
//Variables contenant les ID wifi finaux (ceux du réseau courant)
String ssid = "";
String password = "";
String host = "";

//PIN du boutton poussoir reset
const int BUTTON = 4;
//Pin de la LED RGB couleur bleu
const int BLUE_LED = 12;
//Pin de la LED RGB couleur rouge
const int RED_LED = 15;
//Pin de la LED RGB couleur verte
const int GREEN_LED = 13;
//Pin du capteur de luminosité
const int LIGHT_PIN = A0;
//Pin du DHT (temperature / humidité)
const int DHT_PIN = 16;
//Pin du capteur de mouvement 
const int MVT_PIN = 14;
//En-tete des pages http
String header;
//Pied de page des pages http
String footer;
//Si cette variable est a true, on est en hotspot (installation) sinon on est en mode
// client (fonctionnement normale)
bool hostMode = true;
//Si cette variable est a true on affiche les état via la led, sinon on la desactive pour
//ne pas gener l'utilisateur (surtout la nuit)
int enableLed = true;
//Si une présence a été détéctée, cette var est à 1 , sinon à 0
int presence = 0;
//Stoque le timestamp de la derniere detection de présence 
unsigned long start_presence;
//Stoque le timestamp du dernier envois de donnée au serveur distant
unsigned long last_send =0;
//Représente le serveur web sur le port 80
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);



// Witty Cloud Board specifc pins
const int LDR = A0;
//const int BUTTON = 4;
const int RED = 15;
const int GREEN = 12;
const int BLUE = 13;


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
  String text = String((char *) &payload[0]);
  char * textC = (char *) &payload[0];
  String rssi;
  String LDRvalue;
  String ButtonState;
  String temp;
  int nr;
  int on;
  uint32_t rmask;
  int i;
  char b[10];   //declaring character array
  String str;  //declaring string

  switch(type) {
      case WStype_DISCONNECTED:
          Serial.printf("[%u] Disconnected!\n", num);
          break;
      case WStype_CONNECTED:
          {
              IPAddress ip = webSocket.remoteIP(num);
              Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      
              // send message to client
              delay(5);
              webSocket.sendTXT(num, "C");
          }
          break;
      case WStype_TEXT:

          // send data to all connected clients
          // webSocket.broadcastTXT("message here");
        
          switch(payload[0]){
            case 'w': case 'W':  // Request RSSI wx
              rssi = String(WiFi.RSSI());
               // Serial.printf("[%u] Got message: %s\n", num, payload);
              delay(5);
              webSocket.sendTXT(0,rssi);
              break;

            case 'l': case 'L':  // Request LDR
              LDRvalue = analogRead(LDR);
               // Serial.printf("[%u] Got message: %s\n", num, payload);
              delay(5);
              webSocket.sendTXT(0,LDRvalue);
              break;

            case 'b': case 'B':  // Request Button state
              ButtonState = digitalRead(BUTTON);
               // Serial.printf("[%u] Got message: %s\n", num, payload);
              delay(5);
              webSocket.sendTXT(0,ButtonState);
              break;

            case '#':  // RGB LED
              {
              // we get RGB data
              uint32_t rgb = (uint32_t) strtol((const char *) &payload[1], NULL, 16);
              // decode and scale it to max on PWM i.e. 1024
              analogWrite(RED, ((rgb >> 16) & 0xFF)*4);
              analogWrite(GREEN, ((rgb >> 8) & 0xFF)*4);
              analogWrite(BLUE, ((rgb >> 0) & 0xFF)*4);
              // Serial.printf("[%u] Got message: %s\n", num, payload);
              delay(5);
              webSocket.sendTXT(0,"OK");
              }
              break;

            case 'p': // ping, will reply pong
              Serial.printf("[%u] Got message: %s\n", num, payload);
              delay(5);
              webSocket.sendTXT(0,"pong");
              break;

            case 'e': case 'E':   //Echo
              delay(5);
              webSocket.sendTXT(0,text);
              break;

            default:
              delay(5);
              webSocket.sendTXT(0,"**** UNDEFINED ****");
              Serial.printf("[%u] Got UNDEFINED message: %s\n", num, payload);
              break;
          }
          break;
      
      case WStype_BIN:
          Serial.printf("[%u] get binary lenght: %u\n", num, lenght);
          hexdump(payload, lenght);

          // send message to client
          // webSocket.sendBIN(num, payload, lenght);
          break;
  }
}


//holds the current upload
File fsUploadFile;

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}



void setup() {
        
  //On affiche les logs sur le bauds 115200
  Serial.begin(115200);
  Serial.println("Démarrage programme");
  //On utilise 512 octets dans l'eeprom
  EEPROM.begin(512);
  pinMode(LIGHT_PIN, INPUT);
  pinMode(BUTTON, INPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode (MVT_PIN,INPUT);



  // Setup DHT sensor.
  DHT.setup(DHT_PIN, DHTesp::DHT22);
  DHT.getTemperature();
  
  DHTesp::DHT_ERROR_t check = DHT.getStatus();
  switch (check)
  {
    case DHTesp::ERROR_NONE:
      Serial.print("DHT22: OK.\n");
      break;
    case DHTesp::ERROR_TIMEOUT:
      Serial.print("DHT22: Timeout error.\n");
      break;
    case DHTesp::ERROR_CHECKSUM:
      Serial.print("DHT22: Checksum error.\n");
      break;
    default:
      Serial.print("DHT22: Unkown error.\n");
      break;
  }

  black();



  if (digitalRead(BUTTON) == LOW) {
      Serial.println("Button reset poussé");
      digitalWrite(GREEN_LED,HIGH);
      digitalWrite(BLUE_LED,HIGH);
      clearConfig();
      delay(1000);
      black();
   }

  
  for (int i = 0; i < 32; ++i){ ssid += char(EEPROM.read(i));}
  for (int i = 32; i < 96; ++i){ password += char(EEPROM.read(i));}
  for (int i = 96; i < 296; ++i){ 
    if(EEPROM.read(i)!=0) {
      host += char(EEPROM.read(i));
    }
  }

  if(EEPROM.read(297)==0) enableLed = false;
  
  
  Serial.println("SSID :"+ssid);





  SPIFFS.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);


//SERVER INIT
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, [](){
    if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  //server.begin();

  if(ssid.length()>0 && ssid!=""){
    Serial.println("Configuration wifi existante, mode client");

    green();
    
    hostMode = false;
    
    Serial.println("Try to connect on wifi "+ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    green();
    Serial.println("WIFID connected !\r\n IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("PROPISE client mode started"); 



    initialiseHeaderAndFooter();

    server.on("/", []() {
          Serial.println("Requete sur la racine");
         
         
          String content;
          content = ""+header;
       
          dhtData = DHT.getTempAndHumidity();

          content += "Humidité : "+String(dhtData.humidity, 2)+"<br/>";
          content += "Température : "+String(dhtData.temperature, 2)+"<br/>";
          
          content += footer;
          
          server.send(200, "text/html", content);  
  });
    
    //Lancement du serveur
    server.begin();
    
    return;
  }
  
  Serial.println("Connexion wifi inexistante, mode hote");
  blue();
  WiFi.mode(WIFI_AP);
  // Nom de la ssid = WIFI_SSID + deux derniers bits du mac:
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) + String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String AP_NameString = WIFI_SSID +"-"+ macID;
  char AP_NameChar[AP_NameString.length() + 1];
  memset(AP_NameChar, 0, AP_NameString.length() + 1);
  //Conversion du string en char * 
  for (int i=0; i<AP_NameString.length(); i++)
    AP_NameChar[i] = AP_NameString.charAt(i);
  //Définition ssid + mdp du wifi
  WiFi.softAP(AP_NameChar, WIFI_PASSWORD);
  

 
  initialiseHeaderAndFooter();
  Serial.println("Serveur hote pret");

  server.on("/", []() {
          Serial.println("Requete sur la racine");
         
         
          String content;
          content = ""+header;
       
          content += "<form method='GET' action='setting'>";
          content += "<h3>Salut ! J'ai besoin de quelques infos</h3>";

          content += "<label for='ssid'>Identifiant Wifi</label>";
          content += "<input type='text' name='ssid' length='32'>";

          content += "<label for='password'>Mot de passe Wifi</label>";
          content += "<input type='text' name='password' length='64'>";

          content += "<label for='host'>Adresse du serveur <small>(ex: http://192.168.0.14/monscript/mapage.php?id=3)</small></label>";
          content += "<input type='text' name='host' length='200'>";

          content += "<label for='led'>Activer la led temoin</small></label>";
          content += "<input type='checkbox' name='led' length='1'>";

          content += "<input type='submit' id='send' value='Envoyer'>";
          content += "</form>";
          
          content += footer;
          
          server.send(200, "text/html", content);  
  });

  server.on("/setting", []() {
    Serial.println("Requete sur le setting");
    String txtSsid = server.arg("ssid");
    String txtPassword = server.arg("password");
    String txtHost = server.arg("host");
    String txtEnableLed = server.arg("led");
    
    Serial.println("SSID:"+txtSsid);
    Serial.println("PASSWORD:"+txtPassword);
    Serial.println("URL:"+txtHost);
    Serial.println("LED:"+txtEnableLed);
    
    if (txtSsid.length() > 0 && txtPassword.length() > 0 && txtHost.length() > 0) {

      Serial.println("Ecriture EEPROM...");
      for (int i = 0; i < txtSsid.length(); ++i)
      {
              Serial.print(txtSsid[i]);
              EEPROM.write(i, txtSsid[i]);
      }
      Serial.println("");
      for (int i = 0; i < txtPassword.length(); ++i)
      {
              Serial.print(txtPassword[i]);
              EEPROM.write(32+i, txtPassword[i]);
      }
      Serial.println("");
      for (int i = 0; i < txtHost.length(); ++i)
      {
              Serial.print(txtHost[i]);
              EEPROM.write(96+i, txtHost[i]);
      }
      if(txtEnableLed=="on"){
        EEPROM.write(297, 1);
      }else{
        EEPROM.write(297, 0);
      }
      
      Serial.println("Commit sur EEPROM...");
      EEPROM.commit();
      Serial.println("Enregistré !");
      EEPROM.end();
    }

    String content;
    content = header;
    content += "<h3>Tout baigne ! Tu peux me redémarrer :)</h3>";
    content += footer;
    server.send(200, "text/html", content); 
  });
  
  //Lancement du serveur
  server.begin();
}

void initialiseHeaderAndFooter() {
  header = "<!doctype html>";
  header += "<html class='no-js' lang=''>";
  header += "<head>";
  header += "<meta charset='utf-8'>";
  header += "<meta http-equiv='X-UA-Compatible' content='IE=edge,chrome=1'>";
  header += "<title>PROPISE 1.0</title>";
  header += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  header += "<style>";
  header += "html,body{";
  header += "margin:0;";
  header += "padding:0;";
  header += "background-color: #C75C5C;";
  header += "color:#FFFFFF;";
  header += "font-family:Calibri,Arial,Verdana;";
  header += "}";
  header += "div{";
  header += "width:100%;";
  header += "max-width:500px;";
  header += "margin:20px auto;";
  header += "padding:15px;";
  header += "text-align:center;";
  header += "}";
  header += "label,input{";
  header += "display:block;";
  header += "}";
  header += "input{";
  header += "border:2px solid #ffffff;";
  header += "padding:5px;";
  header += "border-radius:5px;";
  header += "width:100%;";
  header += "color:#ffffff;";
  header += "text-align:center;";
  header += "background:transparent;";
  header += "margin:10px auto;";
  header += "font-weight:bold;";
  header += "box-sizing:border-box;";
  header += "}";
  header += "#send{";
  header += "background:#ffffff;";
  header += "color:#C75C5C;";
  header += "}";
  header += "a{";
  header += "color:#ffffff;";
  header += "}";
  header += "</style>";
  header += "</head>";
  header += "<body>";
  header += "<div>";
  header += "<img src='data:image/svg+xml;utf8;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iaXNvLTg4NTktMSI/Pgo8IS0tIEdlbmVyYXRvcjogQWRvYmUgSWxsdXN0cmF0b3IgMTkuMC4wLCBTVkcgRXhwb3J0IFBsdWctSW4gLiBTVkcgVmVyc2lvbjogNi4wMCBCdWlsZCAwKSAgLS0+CjxzdmcgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiB4bWxuczp4bGluaz0iaHR0cDovL3d3dy53My5vcmcvMTk5OS94bGluayIgdmVyc2lvbj0iMS4xIiBpZD0iTGF5ZXJfMSIgeD0iMHB4IiB5PSIwcHgiIHZpZXdCb3g9IjAgMCA1MTIgNTEyIiBzdHlsZT0iZW5hYmxlLWJhY2tncm91bmQ6bmV3IDAgMCA1MTIgNTEyOyIgeG1sOnNwYWNlPSJwcmVzZXJ2ZSIgd2lkdGg9IjEyOHB4IiBoZWlnaHQ9IjEyOHB4Ij4KPGc+Cgk8Zz4KCQk8cGF0aCBkPSJNNDQyLjU1OCwzMDQuOTA2Yy0yNy43NTksMC02MC43NTktNi41MTgtNzAuMTMtMjQuODA5Yy01LjY0OC0xMS4wMjcsMC4wODktMjIuOTUzLDUuODk1LTMxLjAxNiAgICBjMjEuMTAxLTI5LjMwNCwzMi4yNTUtNjEuOTgyLDMyLjI1NS05NC41QzQxMC41NzksNjkuMzQ0LDM0MS4yMzUsMCwyNTYsMFMxMDEuNDIsNjkuMzQ0LDEwMS40MiwxNTQuNTc5ICAgIGMwLDMyLjUxOCwxMS4xNTQsNjUuMTk2LDMyLjI1NSw5NC41YzUuODA2LDguMDYzLDExLjU0MywxOS45ODksNS44OTUsMzEuMDE2Yy05LjM3LDE4LjI5Mi00Mi4zNywyNC44MDktNzAuMTMsMjQuODA5ICAgIGMtMjkuNjM4LDAtNTMuNzQ5LDI0LjExMS01My43NDksNTMuNzQ5czI0LjExMSw1My43NDksNTMuNzQ5LDUzLjc0OWMxNS4zODgsMCwzNi44NTYtMy43MzEsNTUuMTY4LTguNTQyICAgIGMtNC45MjEsMy43NDgtMTAuMTQ3LDcuNjkxLTE1LjAzNiwxMS4yMDVjLTIxLjA2OCwxNS4xNDUtMjcuNzc1LDM5LjUwNy0xNy41MDYsNjMuNTc3YzYuMDUyLDE0LjE4NSwxNy44MjYsMjUuMjYsMzIuMjk5LDMwLjM4NCAgICBjMTMuMzM2LDQuNzIsMjcuMzM0LDMuNzk1LDM5LjQxOC0yLjYwNWMzNi4wMy0xOS4wODUsNTkuNDE0LTUxLjE4Myw3Ni40OS03NC42MmM1LjAxMy02Ljg4MiwxMS41MDEtMTUuNzg3LDE1LjcyNi0yMC4xMjUgICAgYzQuMjI1LDQuMzM4LDEwLjcxMywxMy4yNDMsMTUuNzI2LDIwLjEyNWMxNy4wNzUsMjMuNDM4LDQwLjQ1OSw1NS41MzUsNzYuNDksNzQuNjJjNywzLjcwNywxNC42NDEsNS41NzgsMjIuNDI0LDUuNTc4ICAgIGM1LjY1NCwwLDExLjM4My0wLjk4NiwxNi45OTQtMi45NzNjMTQuNDc1LTUuMTI0LDI2LjI0Ny0xNi4xOTgsMzIuMjk5LTMwLjM4NGMxMC4yNjktMjQuMDcxLDMuNTYyLTQ4LjQzMi0xNy41MDYtNjMuNTc2ICAgIGMtNC44ODktMy41MTUtMTAuMTE0LTcuNDU3LTE1LjAzNi0xMS4yMDZjMTguMzExLDQuODExLDM5Ljc4LDguNTQyLDU1LjE2OCw4LjU0MmMyOS42MzgsMCw1My43NDktMjQuMTExLDUzLjc0OS01My43NDkgICAgUzQ3Mi4xOTcsMzA0LjkwNiw0NDIuNTU4LDMwNC45MDZ6IE00NDIuNTU4LDM4MS4wNTdjLTE5LjAyLDAtNTMuNjcyLTcuOTgxLTY4LjctMTQuNTE0Yy0xMy40NzMtNS44NTUtMjguNzk0LTAuODE3LTM1LjYzOCwxMS43MjMgICAgYy02Ljc3MywxMi40MS0yLjc0OCwyNi45MjksOS43ODgsMzUuMzA4YzMuNjU4LDIuNDQ1LDkuNzYzLDcuMTA5LDE2LjIyNywxMi4wNDZjNi4yMDQsNC43NDEsMTMuMjM5LDEwLjExMywxOS44OTMsMTQuODk4ICAgIGMxMS4zNzksOC4xNzksMTAuMDcyLDE4LjU1OCw2Ljk3MSwyNS44MjRjLTIuNTgyLDYuMDUyLTcuNzg4LDEwLjk2Mi0xMy45MjgsMTMuMTM1Yy0zLjUxNSwxLjI0NC04Ljg0OSwyLjEyMy0xNC4yODMtMC43NTUgICAgYy0yOS42MzEtMTUuNjk1LTUwLjU1Mi00NC40MS02NS44MjctNjUuMzc4Yy0xNC43MTktMjAuMjA0LTI1LjM1NC0zNC44MDItNDEuMDYyLTM0LjgwMmMtMTUuNzA5LDAtMjYuMzQzLDE0LjU5OC00MS4wNjIsMzQuODAyICAgIGMtMTUuMjc1LDIwLjk2Ni0zNi4xOTUsNDkuNjgyLTY1LjgyNyw2NS4zNzhjLTUuNDM2LDIuODgtMTAuNzY5LDItMTQuMjgzLDAuNzU1Yy02LjE0LTIuMTczLTExLjM0Ni03LjA4My0xMy45MjgtMTMuMTM1ICAgIGMtMy4xLTcuMjY2LTQuNDA3LTE3LjY0NSw2Ljk3MS0yNS44MjRjNi42NTUtNC43ODQsMTMuNjg5LTEwLjE1NywxOS44OTMtMTQuODk4YzYuNDY1LTQuOTM4LDEyLjU2OS05LjYwMSwxNi4yMjUtMTIuMDQ1ICAgIGMxMi41MzgtOC4zNzksMTYuNTY0LTIyLjg5OCw5Ljc5LTM1LjMwOGMtNi44NDUtMTIuNTQyLTIyLjE3LTE3LjU4Mi0zNS42MzgtMTEuNzIzYy0xNS4wMjgsNi41MzQtNDkuNjgsMTQuNTE0LTY4LjcsMTQuNTE0ICAgIGMtMTIuMzU0LDAtMjIuNDAzLTEwLjA1LTIyLjQwMy0yMi40MDNzMTAuMDUtMjIuNDAzLDIyLjQwMy0yMi40MDNjNDguNjcsMCw4NC40LTE1LjI1OSw5OC4wMjktNDEuODY1ICAgIGM5Ljg5NC0xOS4zMTgsNi44NDktNDIuNTA3LTguMzU2LTYzLjYyM2MtMTcuNDgzLTI0LjI3OS0yNi4zNDctNDkuOTEtMjYuMzQ3LTc2LjE4NEMxMzIuNzY2LDg2LjYyOCwxODguMDQ4LDMxLjM0NiwyNTYsMzEuMzQ2ICAgIHMxMjMuMjMzLDU1LjI4MiwxMjMuMjMzLDEyMy4yMzNjMCwyNi4yNzMtOC44NjQsNTEuOTA1LTI2LjM0Nyw3Ni4xODRjLTE1LjIwNSwyMS4xMTYtMTguMjUsNDQuMzA2LTguMzU2LDYzLjYyMyAgICBjMTMuNjI4LDI2LjYwNiw0OS4zNTksNDEuODY1LDk4LjAyOSw0MS44NjVjMTIuMzU0LDAsMjIuNDAzLDEwLjA1LDIyLjQwMywyMi40MDMgICAgQzQ2NC45NjIsMzcxLjAwNyw0NTQuOTEzLDM4MS4wNTcsNDQyLjU1OCwzODEuMDU3eiIgZmlsbD0iI2ZmZmZmZiIvPgoJPC9nPgo8L2c+CjxnPgoJPGc+CgkJPHBhdGggZD0iTTIwMy44MzgsMTUxLjU2OWMtMTEuMzQsMC0yMC41NjksOS4zMjMtMjAuNTY5LDIwLjgzNWMwLDExLjUxLDkuMjI4LDIwLjg0MiwyMC41NjksMjAuODQyICAgIGMxMS4zNTUsMCwyMC41ODItOS4zMzEsMjAuNTgyLTIwLjg0MkMyMjQuNDE5LDE2MC44OTEsMjE1LjE5MywxNTEuNTY5LDIwMy44MzgsMTUxLjU2OXoiIGZpbGw9IiNmZmZmZmYiLz4KCTwvZz4KPC9nPgo8Zz4KCTxnPgoJCTxwYXRoIGQ9Ik0zMDguMDA2LDE1MS40MDZjLTExLjQ1LDAtMjAuNzM4LDkuNDAxLTIwLjczOCwyMC45OThjMCwxMS41OTcsOS4yODgsMjAuOTk4LDIwLjczOCwyMC45OTggICAgYzExLjQzNiwwLDIwLjcyNC05LjQwMSwyMC43MjQtMjAuOTk4QzMyOC43MywxNjAuODA3LDMxOS40NDIsMTUxLjQwNiwzMDguMDA2LDE1MS40MDZ6IiBmaWxsPSIjZmZmZmZmIi8+Cgk8L2c+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPGc+CjwvZz4KPC9zdmc+Cg==' />";
  
  footer = "</div>";
  footer += "</body>";
  footer += "</html>";
}

void clearConfig(){
    Serial.println("Reset EEPROM");
    //reset des 297 premiers char de l'eeprom
    for (int i = 0; i < 297; ++i) { EEPROM.write(i, 0); }
    EEPROM.commit();
}

void loop() {
  if(hostMode){
    webSocket.loop();
    server.handleClient();
  }else{

    server.handleClient();
    if (digitalRead(MVT_PIN) == HIGH){
      start_presence = millis();
      presence = 1;     
    }
    if(millis()-start_presence>10000){
        presence=0;
    }
    if(millis()-last_send>3000)
     send_data();
  }
}

void red(){
  digitalWrite(RED_LED,HIGH);
  digitalWrite(GREEN_LED,LOW);
  digitalWrite(BLUE_LED,LOW);
}

void blue(){
  digitalWrite(RED_LED,LOW);
  digitalWrite(GREEN_LED,LOW);
  digitalWrite(BLUE_LED,HIGH);
}

void green(){
  digitalWrite(RED_LED,LOW);
  digitalWrite(GREEN_LED,HIGH);
  digitalWrite(BLUE_LED,LOW);
}

void white(){
  digitalWrite(RED_LED,HIGH);
  digitalWrite(GREEN_LED,HIGH);
  digitalWrite(BLUE_LED,HIGH);
}

void black(){
  digitalWrite(RED_LED,LOW);
  digitalWrite(GREEN_LED,LOW);
  digitalWrite(BLUE_LED,LOW);
}

void send_data(){
    if(enableLed)
      white();
    last_send = millis();
    //String url = "http://idleman.fr/yana/propise.php?light=962";
    String url = host;
    url.replace("{{LIGHT}}",String(analogRead(LIGHT_PIN)));
    url.replace("{{IP}}",WiFi.localIP().toString());

    dhtData = DHT.getTempAndHumidity();

    url.replace("{{HUMIDITY}}",String(dhtData.humidity, 2));
    url.replace("{{TEMPERATURE}}",String(dhtData.temperature, 2));
    url.replace("{{MOUVMENT}}",String(presence));

    
    url.trim();
    url.replace(" ","");
    Serial.println("Send data to "+url+"..." );
/*
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) { 
      String response = http.getString();
      Serial.println(response);
    }

    http.end();
    Serial.println("End request");
    delay(500);
    black();
    delay(500);
    */
}





