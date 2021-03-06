#include "WebServer.h"


class AsyncSDFileResponse: public AsyncAbstractResponse  {
  private:
    StreamFileAbstract* filePtr; 
  public:
    AsyncSDFileResponse(String path, bool download){
      if(path.endsWith("/")) path += "index.htm";

      filePtr = SdCard.open((char *)path.c_str());
      if (filePtr) {
        if(filePtr->isDirectory()){
          filePtr->close();
          path += "/index.htm";
          filePtr = SdCard.open((char *)path.c_str());
        }
        _code = 200;
        _contentLength = filePtr->size();
        if (download) {
          _contentType = "application/octet-stream";
        }
        else {
          if(path.endsWith(".htm")) _contentType = "text/html";
          else if(path.endsWith(".css")) _contentType = "text/css";
          else if(path.endsWith(".json")) _contentType = "text/json";
          else if(path.endsWith(".xml")) _contentType = "text/xml";
          else if(path.endsWith(".png")) _contentType = "image/png";
          else if(path.endsWith(".gif")) _contentType = "image/gif";
          else if(path.endsWith(".jpg")) _contentType = "image/jpeg";
          else if(path.endsWith(".ico")) _contentType = "image/x-icon";
          else if(path.endsWith(".js")) _contentType = "application/javascript";
          else if(path.endsWith(".pdf")) _contentType = "application/pdf";
          else if(path.endsWith(".zip")) _contentType = "application/zip";  
          else _contentType = "text/plain";
        }
      }
      else {
        _code = 404;
        _contentLength = 0;
        _contentType = "textPlain";
      }
    }
    
    ~AsyncSDFileResponse() {
      if (filePtr){
        filePtr->close();
      }
    }
    bool _sourceValid(){return !!(filePtr);}

    size_t _fillBuffer(uint8_t *data, size_t len) {
      len = filePtr->read(data, len);
      return len;
    }
};

WebServer::WebServer(TcpUartServer *tcpUartServerPtr, GCodePlayer *gcodePlayerPtr, WifiConnection* configPtr){
  this->tcpUartServerPtr = tcpUartServerPtr;
  this->gcodePlayerPtr = gcodePlayerPtr;
  this->configPtr = configPtr;
}

void WebServer::begin(uint16_t port){
  DEBUG_print("Initializing WEB server...");
  
  webServerPtr = new AsyncWebServer(port);
  
  webServerPtr->on("/gcode", HTTP_GET, [this](AsyncWebServerRequest *request) {listFileHandler(request);});
  webServerPtr->on("/gcode", HTTP_DELETE, [this](AsyncWebServerRequest *request) {deleteFileHandler(request);});
  webServerPtr->on("/gcode", HTTP_PUT, [this](AsyncWebServerRequest *request) {createDirHandler(request);});
  webServerPtr->on("/gcode", HTTP_POST, [this](AsyncWebServerRequest *request){request->send(200);}, \
                   [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){ \
                   uploadFileHandler(request, filename, index, data, len, final);});
  webServerPtr->onFileUpload([this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
                             uploadFileHandler(request, filename, index, data, len, final);});
  webServerPtr->on("/control", HTTP_GET, [this](AsyncWebServerRequest *request) {controlHandler(request);});
  webServerPtr->on("/connection", HTTP_GET, [this](AsyncWebServerRequest *request) {getConnectionHandler(request);});  
  webServerPtr->on("/connection", HTTP_POST, [this](AsyncWebServerRequest *request) {setConnectionHandler(request);});  
  webServerPtr->onNotFound([this](AsyncWebServerRequest *request) {defaultHandler(request);});
  
  webServerPtr->begin();

  DEBUG_println("done.");
}

void WebServer::defaultHandler(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET){
    String path = "Service/Web" + request->url();
    request->send(new AsyncSDFileResponse(path, request->hasArg("download")));
  }
  else{
    request->send(404);
  }
}

void WebServer::getConnectionHandler(AsyncWebServerRequest *request) {
  String output = "{\"ssid\":\"";
  output += configPtr->getSsid();
  output += "\",\"pass\":\"";
  output += configPtr->getPass();
  output += "\",\"name\":\"";
  output += configPtr->getName();
  output += "\",\"ip\":\"";
  output += String(configPtr->getIp());
  output += "\",\"web_server_port\":\"";
  output += String(configPtr->getWebServerPort());
  output += "\",\"tcp_uart_port\":\"";
  output += String(configPtr->getTcpUartPort());
  output += "\",\"wifi_mode\":\"";
  if(configPtr->getWifiMode()) output += "ap";
  else output += "st";
  output += "\"}";
  request->send(200, "text/json", output);
}

void WebServer::setConnectionHandler(AsyncWebServerRequest *request) {
  if ((request->hasArg("ssid")) && (request->hasArg("pass")) && (request->hasArg("name")) && (request->hasArg("ip")) && (request->hasArg("web_server_port")) && (request->hasArg("tcp_uart_port")) && (request->hasArg("wifi_mode"))) {
    configPtr->setSsid(request->arg("ssid"));
    configPtr->setPass(request->arg("pass"));
    configPtr->setName(request->arg("name"));
    configPtr->setIp(atoi(request->arg("ip").c_str()));
    configPtr->setWebServerPort(atoi(request->arg("web_server_port").c_str()));
    configPtr->setTcpUartPort(atoi(request->arg("tcp_uart_port").c_str()));
    if (request->arg("wifi_mode") == "ap") configPtr->setWifiMode(true);
    else  configPtr->setWifiMode(false);
    configPtr->saveData();
    request->send(200, "text/plain", "");
  }
  else {
    request->send(500, "text/plain", "WRONG CMD\r\n");
  }
}

void WebServer::controlHandler(AsyncWebServerRequest *request) {
  if ((request->hasArg("cmd")) && (request->hasArg("value"))) {
    String cmd = request->arg("cmd");
    if (cmd == "uart") {
      if (request->arg("value") == "enable") {
        if (!gcodePlayerPtr->isBusy()) {
          gcodePlayerPtr->enable(false);
          tcpUartServerPtr->enable(true);
          request->send(200, "text/plain", "enable");
        }
        else {
          request->send(500, "text/plain", "Serial port busy");
        }
      }
      else {
        tcpUartServerPtr->enable(false);
        gcodePlayerPtr->enable(true);
        request->send(200, "text/plain", "disable");
      }
    }
    else if (cmd == "log"){
      byte buffer[LOG_BUFFER_SIZE];
      int logSize = SerialPort.getLogData(buffer, sizeof(buffer));
      String output = "";
      for (int i = 0; i < logSize; i++){
        if ((buffer[i] > 31) || ((buffer[i] == '\n'))){
          output += (char)buffer[i];
        }
      }
      request->send(200, "text/plain", output);
    }
    else if (cmd == "status"){
      String output = "{\"uart_bridge\":\"";
      output += tcpUartServerPtr->isEnable() ? "true" : "false";
      output += "\",\"print_server\":\"";
      output += gcodePlayerPtr->isBusy() ? "true" : "false";
      output += "\",\"file_name\":\"";
      output += gcodePlayerPtr->getFileName();
      output += "\"}";
      request->send(200, "text/plain", output);
    }
    else if (cmd == "gcode"){
      if (gcodePlayerPtr->isEnable() && !gcodePlayerPtr->isBusy()){
        String gCodeStr = request->arg("value");
        gcodePlayerPtr->sendGCode(gCodeStr);
        request->send(200, "text/plain", "");
      }
      else {
        request->send(500, "text/plain", "Serial port busy");
      }
    }
    else if (cmd == "run") {
      if (gcodePlayerPtr->isEnable() && !gcodePlayerPtr->isBusy()){
        String fileName = "Service/GCode/" + request->arg("value");
        gcodePlayerPtr->sendFile(fileName);
        request->send(200, "text/plain", "");
      }
      else {
        request->send(500, "text/plain", "Serial port busy");
      }
    }
    else if (cmd == "print") {
      if (gcodePlayerPtr->isEnable() && !gcodePlayerPtr->isBusy()){
        String fileName = "GCode/" + request->arg("value");
        gcodePlayerPtr->sendFile(fileName);
        request->send(200, "text/plain", "");
      }
      else {
        request->send(500, "text/plain", "Serial port busy");
      }
    }
    else if (cmd == "reset"){
      if (request->arg("value") == "1") rstFlag = true;
      request->send(200, "text/plain", "");
    }
    else {
      request->send(500, "text/plain", "WRONG CMD\r\n");
    }
  }
  else {
    request->send(500, "text/plain", "WRONG CMD\r\n");
  }
}

void WebServer::uploadFileHandler(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if ((request->method() == HTTP_POST) && (request->url() == "/gcode")){
    if (0 == index){
      filename = "GCode/" + filename;
      if(SdCard.exists((char *)filename.c_str())) SdCard.remove((char *)filename.c_str());
      uploadFilePtr = SdCard.open((char *)filename.c_str(), FILE_WRITE);
    }
    if (uploadFilePtr){
      if (0 != len){
        while (len > 512) {
          uploadFilePtr->write(data, 512);
          data += 512;
          len -= 512;
        }
        uploadFilePtr->write(data, len);
      }
      if (final) uploadFilePtr->close();
    }
  }
}

void WebServer::deleteFileHandler(AsyncWebServerRequest *request){
  if(request->args() != 0){
    String path = "GCode/" + request->arg(0);
    if ((path != "GCode/") && (SdCard.exists((char *)path.c_str()))) {
      deleteRecursive(path);
      request->send(200, "text/plain", "");
    }
  }
  request->send(500, "text/plain", "BAD PATH\r\n");
}

void WebServer::deleteRecursive(String path){
  StreamFileAbstract* filePtr = SdCard.open((char *)path.c_str());
  if(!filePtr->isDirectory()){
    filePtr->close();
    SdCard.remove((char *)path.c_str());
    return;
  }

  filePtr->rewindDirectory();
  while(true) {
    StreamFileAbstract* entry = filePtr->openNextFile();
    if (!entry) break;
    String entryPath = path + "/" + entry->name();
    if(entry->isDirectory()){
      entry->close();
      deleteRecursive(entryPath);
    } else {
      entry->close();
      SdCard.remove((char *)entryPath.c_str());
    }
  }
  filePtr->close();
  
  SdCard.rmdir((char *)path.c_str());
}

void WebServer::createDirHandler(AsyncWebServerRequest *request){
  if(request->args() != 0){
    String dirName = "GCode/" + request->arg(0);
    if(SdCard.exists((char *)dirName.c_str())) {
      request->send(500, "text/plain", "ALREADY EXISTS\r\n");
    }
    else {
      SdCard.mkdir((char *)dirName.c_str());
      request->send(200, "text/plain", "");
    }
  }
  request->send(500, "text/plain", "BAD PATH\r\n");
}

void WebServer::listFileHandler(AsyncWebServerRequest *request){
  String path = "GCode";
  if (request->hasArg("path")){
    path += "/" + request->arg("path");
  }
  
  StreamFileAbstract* dirPtr = SdCard.open((char *)path.c_str());
  if (dirPtr){
    dirPtr->rewindDirectory();
    String output = "[";
    StreamFileAbstract* entry = dirPtr->openNextFile();
    while (entry) {
      output += "{\"name\":\"";
      output += entry->name();
      output += "\",\"size\":\"";
      if (entry->isDirectory()) output += "<DIR>";
      else output += entry->size();
      output += "\", \"date\":\"";
      output += "1-10-2016";
      output += "\"}";
      entry->close();
      entry = dirPtr->openNextFile();
      if (entry) output += ",";
    }
    output += "]";
    request->send(200, "text/json", output);
    dirPtr->close();
  }
  else{
    request->send(500, "text/plain", "BAD PATH\r\n");
  }
}

void WebServer::handleRst(){
  if (rstFlag){
    delay(1000);
    ESP.reset();
  }
}

