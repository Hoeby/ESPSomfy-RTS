#include <Arduino.h>
#include <Preferences.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <SPI.h>
#include "Utils.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "Sockets.h"
#include "MQTT.h"

extern Preferences pref;
extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern MQTTClass mqtt;

#define SYMBOL 640
#if defined(ESP8266)
    #define RECEIVE_ATTR ICACHE_RAM_ATTR
#elif defined(ESP32)
    #define RECEIVE_ATTR IRAM_ATTR
#else
    #define RECEIVE_ATTR
#endif

static int interruptPin = 0;
static uint8_t bit_length = 56;
somfy_commands translateSomfyCommand(const String& string) {
    if (string.equalsIgnoreCase("My")) return somfy_commands::My;
    else if (string.equalsIgnoreCase("Up")) return somfy_commands::Up;
    else if (string.equalsIgnoreCase("MyUp")) return somfy_commands::MyUp;
    else if (string.equalsIgnoreCase("Down")) return somfy_commands::Down;
    else if (string.equalsIgnoreCase("MyDown")) return somfy_commands::MyDown;
    else if (string.equalsIgnoreCase("UpDown")) return somfy_commands::UpDown;
    else if (string.equalsIgnoreCase("Prog")) return somfy_commands::Prog;
    else if (string.equalsIgnoreCase("SunFlag")) return somfy_commands::SunFlag;
    else if (string.equalsIgnoreCase("Flag")) return somfy_commands::Flag;
    else if (string.startsWith("md") || string.startsWith("MD")) return somfy_commands::MyDown;
    else if (string.startsWith("ud") || string.startsWith("UD")) return somfy_commands::UpDown;
    else if (string.startsWith("mu") || string.startsWith("MU")) return somfy_commands::MyUp;
    else if (string.startsWith("p") || string.startsWith("P")) return somfy_commands::Prog;
    else if (string.startsWith("u") || string.startsWith("U")) return somfy_commands::Up;
    else if (string.startsWith("d") || string.startsWith("D")) return somfy_commands::Down;
    else if (string.startsWith("m") || string.startsWith("M")) return somfy_commands::My;
    else if (string.startsWith("f") || string.startsWith("F")) return somfy_commands::Flag;
    else if (string.startsWith("s") || string.startsWith("S")) return somfy_commands::SunFlag;
    else if (string.length() == 1) return static_cast<somfy_commands>(strtol(string.c_str(), nullptr, 16));
    else return somfy_commands::My;
}
String translateSomfyCommand(const somfy_commands cmd) {
    switch (cmd) {
    case somfy_commands::Up:
        return "Up";
    case somfy_commands::Down:
        return "Down";
    case somfy_commands::My:
        return "My";
    case somfy_commands::MyUp:
        return "My+Up";
    case somfy_commands::UpDown:
        return "Up+Down";
    case somfy_commands::MyDown:
        return "My+Down";
    case somfy_commands::Prog:
        return "Prog";
    case somfy_commands::SunFlag:
        return "Sun Flag";
    case somfy_commands::Flag:
        return "Flag";
    default:
        return "Unknown(" + String((uint8_t)cmd) + ")";
        return "";
    }
}
void somfy_frame_t::decodeFrame(byte* frame) {
    byte decoded[10];
    decoded[0] = frame[0];
    for (byte i = 1; i < 10; i++) {
        decoded[i] = frame[i] ^ frame[i - 1];
    }
    byte checksum = 0;
    // We only want the upper nibble for the command byte.
    for (byte i = 0; i < 10; i++) {
        if (i == 1) checksum = checksum ^ (decoded[i] >> 4);
        else checksum = checksum ^ decoded[i] ^ (decoded[i] >> 4);
    }
    checksum &= 0b1111;  // We keep the last 4 bits only

    this->checksum = decoded[1] & 0b1111;
    this->encKey = decoded[0];
    this->cmd = (somfy_commands)(decoded[1] >> 4);
    this->rollingCode = decoded[3] + (decoded[2] << 8);
    this->remoteAddress = (decoded[6] + (decoded[5] << 8) + (decoded[4] << 16));
    this->valid = this->checksum == checksum && this->remoteAddress < 16777215;
    if (this->valid) {
        // Check for valid command.
        switch (this->cmd) {
        case somfy_commands::My:
        case somfy_commands::Up:
        case somfy_commands::MyUp:
        case somfy_commands::Down:
        case somfy_commands::MyDown:
        case somfy_commands::UpDown:
        case somfy_commands::Prog:
        case somfy_commands::SunFlag:
        case somfy_commands::Flag:
            break;
        default:
            this->valid = false;
            break;
        }
    }

    if (this->valid) {
        /*
        Serial.println("    KEY  1   2   3   4   5   6  ");
        Serial.println("--------------------------------");
        Serial.print("ENC ");
        for(byte i = 0; i < 7; i++) {
          if(frame[i] < 10)
            Serial.print("  ");
          else if(frame[i] < 100)
            Serial.print(" ");
          Serial.print(frame[i]);
          Serial.print(" ");
        }
        Serial.println();
        Serial.print("DEC ");
        for(byte i = 0; i < 7; i++) {
          if(decoded[i] < 10)
            Serial.print("  ");
          else if(decoded[i] < 100)
            Serial.print(" ");
          Serial.print(decoded[i]);
          Serial.print(" ");
        }
        Serial.println();
        Serial.print("VALID:");
        Serial.print(this->valid ? "true" : "false");
        Serial.print(" ENCCS:");
        Serial.print(checksum);
        Serial.print(" DECCS:");
        Serial.println(this->checksum);
        */
        Serial.print("ADDR:");
        Serial.print(this->remoteAddress);
        Serial.print(" CMD:");
        Serial.print(translateSomfyCommand(this->cmd));
        Serial.print(" RCODE:");
        Serial.println(this->rollingCode);
    }
    else {
        Serial.print("INVALID FRAME ");
        Serial.print("ADDR:");
        Serial.print(this->remoteAddress);
        Serial.print(" CMD:");
        Serial.print(translateSomfyCommand(this->cmd));
        Serial.print(" RCODE:");
        Serial.println(this->rollingCode);
        Serial.println("    KEY  1   2   3   4   5   6  ");
        Serial.println("--------------------------------");
        Serial.print("ENC ");
        for (byte i = 0; i < 10; i++) {
            if (frame[i] < 10)
                Serial.print("  ");
            else if (frame[i] < 100)
                Serial.print(" ");
            Serial.print(frame[i]);
            Serial.print(" ");
        }
        Serial.println();
        Serial.print("DEC ");
        for (byte i = 0; i < 10; i++) {
            if (decoded[i] < 10)
                Serial.print("  ");
            else if (decoded[i] < 100)
                Serial.print(" ");
            Serial.print(decoded[i]);
            Serial.print(" ");
        }
        Serial.println();
    }
}
void somfy_frame_t::encodeFrame(const uint32_t address, const somfy_commands cmd, const uint16_t rcode, byte* frame) {
    const byte btn = static_cast<byte>(cmd);
    frame[0] = 0xA7;          // Encryption key. Doesn't matter much
    frame[1] = btn << 4;      // Which button did  you press? The 4 LSB will be the checksum
    frame[2] = rcode >> 8;    // Rolling code (big endian)
    frame[3] = rcode;         // Rolling code
    frame[4] = address >> 16; // Remote address
    frame[5] = address >> 8;  // Remote address
    frame[6] = address;       // Remote address
    byte checksum = 0;
    for (byte i = 0; i < 7; i++) {
        checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
    }
    checksum &= 0b1111;  // We keep the last 4 bits only
    // Checksum integration
    frame[1] |= checksum;
    // Obfuscation: a XOR of all the bytes
    for (byte i = 1; i < 7; i++) {
        frame[i] ^= frame[i - 1];
    }
}
void somfy_frame_t::print() {
    Serial.println("----------- Receiving -------------");
    Serial.print("RSSI:");
    Serial.print(this->rssi);
    Serial.print(" LQI:");
    Serial.println(this->lqi);
    Serial.print("CMD:");
    Serial.print(translateSomfyCommand(this->cmd));
    Serial.print(" ADDR:");
    Serial.print(this->remoteAddress);
    Serial.print(" RCODE:");
    Serial.println(this->rollingCode);
    Serial.print("KEY:");
    Serial.print(this->encKey, HEX);
    Serial.print(" CS:");
    Serial.println(this->checksum);
}

void SomfyShadeController::end() { this->transceiver.disableReceive(); }
SomfyShadeController::SomfyShadeController() {
  memset(this->m_shadeIds, 255, sizeof(this->m_shadeIds));
  uint64_t mac = ESP.getEfuseMac();
  this->startingAddress = mac & 0xFFFF00;
}
SomfyShade *SomfyShadeController::findShadeByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getRemoteAddress() == address) return &shade;
    else {
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        if(shade.linkedRemotes[j].getRemoteAddress() == address) return &shade;
      }
    }
  }
  return nullptr;
}
bool SomfyShadeController::begin() {
  // Load up all the configuration data.
  pref.begin("Shades");
  pref.getBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
  pref.end();
  this->transceiver.begin();
  sortArray(this->m_shadeIds, sizeof(this->m_shadeIds));
  uint8_t id = 0;
  for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
    if(this->m_shadeIds[i] == id) this->m_shadeIds[i] = 255;
    id = this->m_shadeIds[i];
    SomfyShade *shade = &this->shades[i];
    shade->setShadeId(id);
    if(id == 255) continue;
    shade->load();
  }
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    Serial.print(this->shades[i].getShadeId());
    Serial.print(":");
    Serial.print(this->m_shadeIds[i]);
    if(i < SOMFY_MAX_SHADES - 1) Serial.print(",");
  }
  pref.begin("Shades");
  pref.putBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
  pref.end();
  Serial.println();
  this->transceiver.begin();
  return true;
}
SomfyShade * SomfyShadeController::getShadeById(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) return &this->shades[i];
  }
  return nullptr;
}
bool SomfyShade::linkRemote(uint32_t address, uint16_t rollingCode) {
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == 0) {
      char shadeKey[15];
      this->linkedRemotes[i].setRemoteAddress(address);
      this->linkedRemotes[i].setRollingCode(rollingCode);
      snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->getShadeId());
      pref.begin(shadeKey);
      uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
      memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
      uint8_t j = 0;
      for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
        SomfyLinkedRemote lremote = this->linkedRemotes[i];
        if(lremote.getRemoteAddress() != 0) linkedAddresses[j++] = lremote.getRemoteAddress();
      }
      pref.putBytes("linkedAddr", linkedAddresses, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
      pref.end();
      return true;
    }
  }
  return false;
}
bool SomfyShade::unlinkRemote(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) {
      char shadeKey[15];
      this->linkedRemotes[i].setRemoteAddress(0);
      snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->getShadeId());
      pref.begin(shadeKey);
      uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
      memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
      uint8_t j = 0;
      for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
        SomfyLinkedRemote lremote = this->linkedRemotes[i];
        if(lremote.getRemoteAddress() != 0) linkedAddresses[j++] = lremote.getRemoteAddress();
      }
      pref.putBytes("linkedAddr", linkedAddresses, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
      pref.end();
      return true;
    }
  }
  return false;
}

void SomfyShade::checkMovement() {
  int8_t currDir = this->direction;
  uint8_t currPos = this->position;
  if(this->direction > 0) {
    if(this->downTime == 0) {
      this->direction = 0;
      this->currentPos = 1;
    }
    else {
      // The shade is moving down so we need to calculate its position through the down position.
      // 10000ms from 0 to 100
      // The starting posion is a float value from 0-1 that indicates how much the shade is open. So
      // if we take the starting position * the total down time then this will tell us how many ms it
      // has moved in the down position.
      int32_t msFrom0 = (int32_t)floor(this->startPos * this->downTime);
      
      // So if the start position is .1 it is 10% closed so we have a 1000ms (1sec) of time to account for
      // before we add any more time.
      msFrom0 += (millis() - this->moveStart);
      // Now we should have the total number of ms that the shade moved from the top.  But just so we
      // don't have any rounding errors make sure that it is not greater than the max down time.
      msFrom0 = min((int32_t)this->downTime, msFrom0);
      if(msFrom0 >= this->downTime) {
        this->currentPos = 1.0;
        this->direction = 0;        
      }
      else {
        // So now we know how much time has elapsed from the 0 position to down.  The current position should be
        // a ratio of how much time has travelled over the total time to go 100%.
  
        // We should now have the number of ms it will take to reach the shade fully close.
        this->currentPos = min(max((float)0.0, (float)msFrom0 / (float)this->downTime), (float)1.0);
        // If the current position is >= 1 then we are at the bottom of the shade.
        if(this->currentPos >= 1) {
          this->direction = 0;
          this->currentPos = 1.0;
        }
      }
      this->position = floor(this->currentPos * 100);
    }
  }
  else if(this->direction < 0) {
    if(this->upTime == 0) {
      this->direction = 0;
      this->currentPos = 0;
    }
    else {
      // The shade is moving up so we need to calculate its position through the up position. Shades
      // often move slower in the up position so since we are using a relative position the up time
      // can be calculated.
      // 10000ms from 100 to 0;
      int32_t msFrom100 = (int32_t)this->upTime - (int32_t)floor(this->startPos * this->upTime);
      msFrom100 += (millis() - this->moveStart);
      msFrom100 = min((int32_t)this->upTime, msFrom100);
      if(msFrom100 >= this->upTime) {
        this->currentPos = 0.0;
        this->direction = 0;
      }
      // We should now have the number of ms it will take to reach the shade fully open.
      this->currentPos = (float)1.0 - min(max((float)0.0, (float)msFrom100 / (float)this->upTime), (float)1.0);
      // If we are at the top of the shade then set the movement to 0.
      if(this->currentPos <= 0.0) {
        this->direction = 0;
        this->currentPos = 0;
      }
    }
    this->position = floor(this->currentPos * 100);
  }
  if(currDir != this->direction || currPos != this->position) {
    // We need to emit on the socket that our state has changed.
    this->position = floor(this->currentPos * 100.0);
    this->emitState();
  }
  if(currDir != this->direction && this->direction == 0) {
    char shadeKey[15];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->shadeId);
    Serial.print("Writing current shade position:");
    Serial.println(this->currentPos);
    pref.begin(shadeKey);
    pref.putFloat("currentPos", this->currentPos);
    pref.end();
  }
}
void SomfyShade::load() {
    char shadeKey[15];
    uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->shadeId);
    // Now load up each of the shades into memory.
    Serial.print("key:");
    Serial.println(shadeKey);
    pref.begin(shadeKey);
    pref.getString("name", this->name, sizeof(this->name));
    this->paired = pref.getBool("paired", false);
    this->upTime = pref.getUShort("upTime", 10000);
    this->downTime = pref.getUShort("downTime", 10000);
    this->setRemoteAddress(pref.getULong("remoteAddress", 0));
    this->currentPos = pref.getFloat("currentPos", 0);
    this->position = (uint8_t)floor(this->currentPos * 100);
    pref.getBytes("linkedAddr", linkedAddresses, sizeof(linkedAddresses));
    pref.end();
    Serial.print("shadeId:");
    Serial.print(this->getShadeId());
    Serial.print(" name:");
    Serial.print(this->name);
    Serial.print(" address:");
    Serial.print(this->getRemoteAddress());
    Serial.print(" position:");
    Serial.println(this->position);
   
    for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
      SomfyLinkedRemote &lremote = this->linkedRemotes[j];
      lremote.setRemoteAddress(linkedAddresses[j]);
      pref.begin("ShadeCodes");
      lremote.lastRollingCode = pref.getUShort(lremote.getRemotePrefId(), 0);
      pref.end();
    }
  
}
void SomfyShade::emitConfig() {
  DynamicJsonDocument doc(256);
  JsonObject obj = doc.to<JsonObject>();
  this->toJSON(obj);
  sockEmit.sendToClients("somfyShade", obj);
}
void SomfyShade::publish() {
  if(mqtt.connected()) {
    char topic[32];
    snprintf(topic, sizeof(topic), "shades/%u/shadeId", this->shadeId);
    mqtt.publish(topic, this->shadeId);
    snprintf(topic, sizeof(topic), "shades/%u/name", this->shadeId);
    mqtt.publish(topic, this->name);
    snprintf(topic, sizeof(topic), "shades/%u/remoteAddress", this->shadeId);
    mqtt.publish(topic, this->getRemoteAddress());
    snprintf(topic, sizeof(topic), "shades/%u/position", this->shadeId);
    mqtt.publish(topic, this->position);
    snprintf(topic, sizeof(topic), "shades/%u/direction", this->shadeId);
    mqtt.publish(topic, this->direction);
    snprintf(topic, sizeof(topic), "shades/%u/target", this->shadeId);
    mqtt.publish(topic, this->target);
    snprintf(topic, sizeof(topic), "shades/%u/lastRollingCode", this->shadeId);
    mqtt.publish(topic, this->lastRollingCode);
  }
}
void SomfyShade::emitState() {
  char buf[200];
  char shadeKey[15];
  snprintf(shadeKey, sizeof(shadeKey), "Shade_%u", this->shadeId);
  sprintf(buf, "{\"shadeId\":%d, \"remoteAddress\":%d, \"name\":\"%s\", \"direction\":%d, \"position\":%d}", this->shadeId, this->getRemoteAddress(), this->name, this->direction, this->position);
  sockEmit.sendToClients("somfyShadeState", buf);
  if(mqtt.connected()) {
    char topic[32];
    snprintf(topic, sizeof(topic), "shades/%u/position", this->shadeId);
    mqtt.publish(topic, this->position);
    snprintf(topic, sizeof(topic), "shades/%u/direction", this->shadeId);
    mqtt.publish(topic, this->direction);
    snprintf(topic, sizeof(topic), "shades/%u/target", this->shadeId);
    mqtt.publish(topic, this->target);
    snprintf(topic, sizeof(topic), "shades/%u/lastRollingCode", this->shadeId);
    mqtt.publish(topic, this->lastRollingCode);
  }
}
void SomfyShade::processFrame(somfy_frame_t &frame) {
  bool hasRemote = this->getRemoteAddress() == frame.remoteAddress;
  if(!hasRemote) {
    for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
      if(this->linkedRemotes[i].getRemoteAddress() == frame.remoteAddress) {
        this->linkedRemotes[i].setRollingCode(frame.rollingCode);
        hasRemote = true;
        break;      
      }
    }
  }
  if(!hasRemote) return;
  int8_t dir = 0;
  // At this point we are not processing the combo buttons
  // will need to see what the shade does when you press both.
  switch(frame.cmd) {
    case somfy_commands::Up:
      dir = -1;
      break;
    case somfy_commands::Down:
      dir = 1;
      break;
    default:
      dir = 0;
      break;
  }
  this->setMovement(dir);
}
void SomfyShade::setMovement(int8_t dir) {
  int8_t currDir = this->direction;
  if(dir == 0) {
    // The shade is stopped.
    this->startPos = this->currentPos;
    this->moveStart = 0;
    this->direction = dir;
  }
  else if(this->direction != dir) {
    this->moveStart = millis();
    this->startPos = this->currentPos;
    this->direction = dir;
  }
  if(this->direction != currDir) {
    this->position = floor(this->currentPos * 100.0);
    this->emitState();
  }
}
bool SomfyShade::save() {
  char shadeKey[15];
  snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", this->getShadeId());
  pref.begin(shadeKey);
  pref.putString("name", this->name);
  pref.putBool("paired", this->paired);
  pref.putUShort("upTime", this->upTime);
  pref.putUShort("downTime", this->downTime);
  pref.putULong("remoteAddress", this->getRemoteAddress());
  pref.putFloat("currentPos", this->currentPos);
  uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
  memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
  uint8_t j = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    SomfyLinkedRemote lremote = this->linkedRemotes[i];
    if(lremote.getRemoteAddress() != 0) linkedAddresses[j++] = lremote.getRemoteAddress();
  }
  pref.putBytes("linkedAddr", linkedAddresses, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
  pref.end();
  return true;
}
bool SomfyShade::fromJSON(JsonObject &obj) {
  if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(obj.containsKey("upTime")) this->upTime = obj["upTime"];
  if(obj.containsKey("downTime")) this->downTime = obj["downTime"];
  if(obj.containsKey("remoteAddress")) this->setRemoteAddress(obj["remoteAddress"]);
  if(obj.containsKey("linkedAddresses")) {
    uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
    memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
    JsonArray arr = obj["linkedAddresses"];
    uint8_t i = 0;
    for(uint32_t addr : arr) {
      linkedAddresses[i++] = addr;
    }
    for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
      this->linkedRemotes[j].setRemoteAddress(linkedAddresses[j]);
    }
  }
  return true;
}
bool SomfyShade::toJSON(JsonObject &obj) {
  Serial.print("Serializing Shade:");
  Serial.print(this->getShadeId());
  Serial.print("  ");
  Serial.println(this->name);
  obj["shadeId"] = this->getShadeId();
  obj["name"] = this->name;
  obj["remoteAddress"] = this->m_remoteAddress;
  obj["upTime"] = this->upTime;
  obj["downTime"] = this->downTime;
  obj["paired"] = this->paired;
  obj["remotePrefId"] = this->getRemotePrefId();
  obj["lastRollingCode"] = this->lastRollingCode;
  obj["position"] = this->position;
  SomfyRemote::toJSON(obj);
  JsonArray arr = obj.createNestedArray("linkedRemotes");
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    SomfyLinkedRemote &lremote = this->linkedRemotes[i];
    if(lremote.getRemoteAddress() != 0) {
      JsonObject lro = arr.createNestedObject();
      lremote.toJSON(lro);
    }
  }
  return true;
}
bool SomfyRemote::toJSON(JsonObject &obj) {
  obj["remotePrefId"] = this->getRemotePrefId();
  obj["remoteAddress"] = this->getRemoteAddress();
  obj["lastRollingCode"] = this->lastRollingCode;
  return true;  
}
void SomfyRemote::setRemoteAddress(uint32_t address) { this->m_remoteAddress = address; snprintf(this->m_remotePrefId, sizeof(this->m_remotePrefId), "_%lu", (unsigned long)this->m_remoteAddress); }
uint32_t SomfyRemote::getRemoteAddress() { return this->m_remoteAddress; }
void SomfyShadeController::processFrame(somfy_frame_t &frame) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    this->shades[i].processFrame(frame);
}
void SomfyShadeController::publish() {
  StaticJsonDocument<128> doc;
  JsonArray arr = doc.to<JsonArray>();
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    arr.add(shade->getShadeId());
    shade->publish();
  }
  mqtt.publish("shades", arr);
}
uint8_t SomfyShadeController::getNextShadeId() {
  uint8_t maxId = 0;
  uint8_t lastId = 0;
  sortArray(this->m_shadeIds, sizeof(this->m_shadeIds));
  for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
    uint8_t id = this->m_shadeIds[i];
    if(id >= 255) continue;
    maxId = max(lastId, id);
  }
  return maxId + 1;
}
uint8_t SomfyShadeController::shadeCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) count++;
  }
  return count;
}
SomfyShade *SomfyShadeController::addShade() {
  uint8_t shadeId = getNextShadeId();
  SomfyShade *shade = nullptr;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == 255)
      shade = &this->shades[i];
  }
  if(shade) {
    shade->setShadeId(shadeId);
    for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
      if(this->m_shadeIds[i] == 255) this->m_shadeIds[i] = shadeId;
    }
    uint8_t id = 0;
    // Eliminate the duplicates.
    for(uint8_t i = 0; i < sizeof(this->m_shadeIds); i++) {
      if(id == this->m_shadeIds[i]) this->m_shadeIds[i] = 255;
    }
    sortArray(this->m_shadeIds, sizeof(this->m_shadeIds));
    pref.begin("Shades");
    pref.putBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
    pref.end();
  }
  return shade;
}
void SomfyRemote::sendCommand(somfy_commands cmd, uint8_t repeat) {
  somfy.transceiver.beginTransmit();
  uint16_t rcode = this->getNextRollingCode();
  Serial.println("------------- Sending -------------");
  Serial.print("CMD:");
  Serial.print(translateSomfyCommand(cmd));
  Serial.print(" ADDR:");
  Serial.print(this->getRemoteAddress());
  Serial.print(" RCODE:");
  Serial.println(rcode);
  
  byte frame[7];
  this->encodeFrame(frame, cmd, rcode);
  this->sendFrame(frame, 2);
  for(uint8_t i = 0; i < repeat; i++) {
    sendFrame(frame, 7);
  }
  somfy.transceiver.endTransmit();
  somfy_frame_t rx;
  this->decodeFrame(frame, &rx);
  //rx.print();
  somfy.processFrame(rx);
}
void SomfyRemote::decodeFrame(byte *frame, somfy_frame_t *rx) {
  byte decoded[7];
  decoded[0] = frame[0];
  for (byte i = 1; i < 7; i++) {
    decoded[i] = frame[i] ^ frame[i-1];
  }
  byte checksum = 0;
  // We only want the upper nibble for the command byte.
  for (byte i = 0; i < 7; i++) {
    if(i == 1) checksum = checksum ^ (decoded[i] >> 4);
    else checksum = checksum ^ decoded[i] ^ (decoded[i] >> 4);
  }
  checksum &= 0b1111;  // We keep the last 4 bits only
    
  rx->checksum = decoded[1] & 0b1111;
  rx->encKey = decoded[0];
  rx->cmd = (somfy_commands)(decoded[1] >> 4);
  rx->rollingCode = decoded[3] + (decoded[2] << 8);
  rx->remoteAddress = (decoded[6] + (decoded[5] << 8) + (decoded[4] << 16));
  rx->valid = rx->checksum == checksum;
  Serial.println("    KEY  1   2   3   4   5   6  ");
  Serial.println("--------------------------------");
  Serial.print("ENC ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] < 10)
      Serial.print("  ");
    else if(frame[i] < 100)
      Serial.print(" ");
    Serial.print(frame[i]);
    Serial.print(" ");
  }
  Serial.println();
  Serial.print("DEC ");
  for(byte i = 0; i < 7; i++) {
    if(decoded[i] < 10)
      Serial.print("  ");
    else if(decoded[i] < 100)
      Serial.print(" ");
    Serial.print(decoded[i]);
    Serial.print(" ");
  }
  Serial.println();
  Serial.print("VALID:");
  Serial.print(rx->valid ? "true" : "false");
  Serial.print(" ENCCS:");
  Serial.print(checksum);
  Serial.print(" DECCS:");
  Serial.println(rx->checksum);
}
void SomfyRemote::encodeFrame(byte *frame, somfy_commands cmd, uint16_t rcode) {
  const byte btn = static_cast<byte>(cmd);
  const uint32_t address = this->getRemoteAddress();
  frame[0] = 0xA7;          // Encryption key. Doesn't matter much
  frame[1] = btn << 4;      // Which button did  you press? The 4 LSB will be the checksum
  frame[2] = rcode >> 8;    // Rolling code (big endian)
  frame[3] = rcode;         // Rolling code
  frame[4] = address >> 16; // Remote address
  frame[5] = address >> 8;  // Remote address
  frame[6] = address;       // Remote address
  byte checksum = 0;
  for (byte i = 0; i < 7; i++) {
    checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
  }
  checksum &= 0b1111;  // We keep the last 4 bits only
  // Checksum integration
  frame[1] |= checksum;
  // Obfuscation: a XOR of all the bytes
  for (byte i = 1; i < 7; i++) {
    frame[i] ^= frame[i - 1];
  }  
}
void SomfyRemote::sendFrame(byte *frame, byte sync) {
  if (sync == 2) {  // Only with the first frame.
    // Wake-up pulse & Silence
    this->sendHigh(9415);
    this->sendLow(9565);
    delay(80);
  }
  // Hardware sync: two sync for the first frame, seven for the following ones.
  for (int i = 0; i < sync; i++) {
    this->sendHigh(4 * SYMBOL);
    this->sendLow(4 * SYMBOL);
  }
  // Software sync
  this->sendHigh(4550);
  this->sendLow(SYMBOL);
  // Data: bits are sent one by one, starting with the MSB.
  for (byte i = 0; i < bit_length; i++) {
    if (((frame[i / 8] >> (7 - (i % 8))) & 1) == 1) {
      this->sendLow(SYMBOL);
      this->sendHigh(SYMBOL);
    } else {
      this->sendHigh(SYMBOL);
      this->sendLow(SYMBOL);
    }
  }
  // Inter-frame silence
  this->sendLow(415);
  delay(30);  
}
void SomfyRemote::sendHigh(uint16_t durationInMicroseconds) {
  digitalWrite(somfy.transceiver.config.TXPin, HIGH);
  delayMicroseconds(durationInMicroseconds);
}
void SomfyRemote::sendLow(uint16_t durationInMicroseconds) {
  digitalWrite(somfy.transceiver.config.TXPin, LOW);
  delayMicroseconds(durationInMicroseconds);
}
bool SomfyShadeController::deleteShade(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) {
      this->shades[i].setShadeId(255);
    }
  }
  for(uint8_t i = 0; i < sizeof(this->m_shadeIds) - 1; i++) {
    if(this->m_shadeIds[i] == shadeId) {
      this->m_shadeIds[i] = 255;
    }
  }
  sortArray(this->m_shadeIds, sizeof(this->m_shadeIds));
  pref.begin("Shades");
  pref.putBytes("shadeIds", this->m_shadeIds, sizeof(this->m_shadeIds));
  pref.end();
  return true;
}

uint16_t SomfyRemote::getNextRollingCode() {
  pref.begin("ShadeCodes");
  uint16_t code = pref.getUShort(this->m_remotePrefId, 0);
  code++;
  pref.putUShort(this->m_remotePrefId, code);
  pref.end();
  this->lastRollingCode = code;
  return code;
}
uint16_t SomfyRemote::setRollingCode(uint16_t code) {
  pref.begin("ShadeCodes");
  pref.putUShort(this->m_remotePrefId, code);
  pref.end();  
  this->lastRollingCode = code;
  return code;
}
bool SomfyShadeController::toJSON(DynamicJsonDocument &doc) {
  Serial.println("Setting Controller Json");
  doc["maxShades"] = SOMFY_MAX_SHADES;
  doc["maxLinkedRemotes"] = SOMFY_MAX_LINKED_REMOTES;
  doc["startingAddress"] = this->startingAddress;
  JsonObject objRadio = doc.createNestedObject("transceiver");
  this->transceiver.toJSON(objRadio);
  JsonArray arr = doc.createNestedArray("shades");
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() != 255) {
      JsonObject oshade = arr.createNestedObject();
      shade->toJSON(oshade);
    }
  }
  return true;
}
bool SomfyShadeController::toJSON(JsonObject &obj) {
  Serial.print("Setting Transceiver Json ");
  obj["maxShades"] = SOMFY_MAX_SHADES;
  obj["maxLinkedRemotes"] = SOMFY_MAX_LINKED_REMOTES;
  obj["startingAddress"] = this->startingAddress;
  JsonObject oradio = obj.createNestedObject("transceiver");
  this->transceiver.toJSON(oradio);
  JsonArray arr = obj.createNestedArray("shades");
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getShadeId() != 255) {
      JsonObject oshade = arr.createNestedObject();
      shade.toJSON(oshade);
    }
  }
  return true;
}
void SomfyShadeController::loop() { 
  this->transceiver.loop(); 
  for(uint8_t i; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) this->shades[i].checkMovement();
  }
}
SomfyLinkedRemote::SomfyLinkedRemote() {}

// Transceiver Implementation
#define TOLERANCE_MIN 0.7
#define TOLERANCE_MAX 1.3

static const  uint32_t tempo_wakeup_pulse = 9415;
static const  uint32_t tempo_wakeup_silence = 89565;
static const  uint32_t tempo_synchro_hw_min = SYMBOL * 4 * TOLERANCE_MIN;
static const  uint32_t tempo_synchro_hw_max = SYMBOL * 4 * TOLERANCE_MAX;
static const  uint32_t tempo_synchro_sw_min = 4550 * TOLERANCE_MIN;
static const  uint32_t tempo_synchro_sw_max = 4550 * TOLERANCE_MAX;
static const  uint32_t tempo_half_symbol_min = SYMBOL * TOLERANCE_MIN;
static const  uint32_t tempo_half_symbol_max = SYMBOL * TOLERANCE_MAX;
static const  uint32_t tempo_symbol_min = SYMBOL * 2 * TOLERANCE_MIN;
static const  uint32_t tempo_symbol_max = SYMBOL * 2 * TOLERANCE_MAX;
static const  uint32_t tempo_inter_frame_gap = 30415;

static int16_t  bitMin = SYMBOL * TOLERANCE_MIN;
typedef enum {
    waiting_synchro = 0,
    receiving_data = 1,
    complete = 2
} t_status;

static struct somfy_rx_t
{
    t_status status;
    uint8_t cpt_synchro_hw;
    uint8_t cpt_bits;
    uint8_t previous_bit;
    bool waiting_half_symbol;
    uint8_t payload[10];
} somfy_rx;
uint8_t receive_buffer[10]; // 80 bits
bool packet_received = false;

void RECEIVE_ATTR Transceiver::handleReceive() {
    static unsigned long last_time = 0;
    const long time = micros();
    const unsigned int duration = time - last_time;
    if (duration < bitMin) {
        // The incoming bit is < 448us so it is probably a glitch so blow it off.
        // We need to ignore this bit.
        return;
    }
    last_time = time;
    switch (somfy_rx.status) {
    case waiting_synchro:
        if (duration > tempo_synchro_hw_min && duration < tempo_synchro_hw_max) {
            // We have found a hardware sync bit.  There should be at least 4 of these.


            // The original code sets gpio 2 on and off but only when in debug mode.  I suspect this was to flash an LED
            // to see the tempo.
            //SET_TP1 WAIT CLR_TP1 WAIT SET_TP1
            ++somfy_rx.cpt_synchro_hw;
            //CLR_TP1
        }
        else if (duration > tempo_synchro_sw_min && duration < tempo_synchro_sw_max && somfy_rx.cpt_synchro_hw >= 4) {
            // If we have a full hardware sync then we should look for the software sync.  If we have a software sync
            // bit and enough hardware sync bits then we should start receiving data.

            //SET_TP1 //WAIT CLR_TP1 WAIT SET_TP1 WAIT CLR_TP1 WAIT SET_TP1 WAIT CLR_TP1 WAIT SET_TP1
            memset(&somfy_rx, 0x00, sizeof(somfy_rx));
            somfy_rx.status = receiving_data;
        }
        else {
            // Reset and start looking for hardware sync again.
            somfy_rx.cpt_synchro_hw = 0;
        }
        break;
    case receiving_data:
        // We should be receiving data at this point.
        if (duration > tempo_symbol_min && duration < tempo_symbol_max && !somfy_rx.waiting_half_symbol) {
            //SET_TP1
            somfy_rx.previous_bit = 1 - somfy_rx.previous_bit;
            // Bits come in high order bit first.
            somfy_rx.payload[somfy_rx.cpt_bits / 8] += somfy_rx.previous_bit << (7 - somfy_rx.cpt_bits % 8);
            ++somfy_rx.cpt_bits;
        }
        else if (duration > tempo_half_symbol_min && duration < tempo_half_symbol_max) {
            //SET_TP1 WAIT CLR_TP1 WAIT SET_TP1 WAIT CLR_TP1 WAIT SET_TP1
            if (somfy_rx.waiting_half_symbol) {
                somfy_rx.waiting_half_symbol = false;
                somfy_rx.payload[somfy_rx.cpt_bits / 8] += somfy_rx.previous_bit << (7 - somfy_rx.cpt_bits % 8);
                ++somfy_rx.cpt_bits;
            }
            else {
                somfy_rx.waiting_half_symbol = true;
            }
        }
        else {
            // Start over we are not within our parameters for bit timing.
            somfy_rx.cpt_synchro_hw = 0;
            somfy_rx.status = waiting_synchro;
        }
        //CLR_TP1
        break;
    default:
        break;

    }
    if (somfy_rx.status == receiving_data && somfy_rx.cpt_bits == bit_length) {
        // The original code posted the task so that it would pick it up immediately.
        // we don't need any of that so we will simply catch it on the next loop.
        //task_post_high(DataReady_taskid, (task_param_t)0);
        memcpy(receive_buffer, somfy_rx.payload, sizeof(receive_buffer));
        packet_received = true;
        somfy_rx.status = waiting_synchro;
    }
}
bool Transceiver::receive() {
    if (packet_received) {
        packet_received = false;
        this->frame.decodeFrame(receive_buffer);
        if (!this->frame.valid) this->clearReceived();
        return this->frame.valid;
    }
    return false;
}
void Transceiver::clearReceived(void) {
    packet_received = false;
    memset(receive_buffer, 0x00, sizeof(receive_buffer));
    attachInterrupt(interruptPin, handleReceive, CHANGE);
}
void Transceiver::enableReceive(void) {
    Serial.print("Enabling receive on Pin #");
    Serial.println(this->config.RXPin);
    pinMode(this->config.RXPin, INPUT);
    interruptPin = digitalPinToInterrupt(this->config.RXPin);
    ELECHOUSE_cc1101.SetRx();
    attachInterrupt(interruptPin, handleReceive, CHANGE);
    Serial.print("Hooked interrupt #");
    Serial.println(interruptPin);
}
void Transceiver::disableReceive(void) { detachInterrupt(interruptPin); }
bool Transceiver::toJSON(JsonObject& obj) {
    Serial.println("Setting Transceiver Json");
    JsonObject objConfig = obj.createNestedObject("config");
    this->config.toJSON(objConfig);
    return true;
}
bool Transceiver::fromJSON(JsonObject& obj) {
    
    if (obj.containsKey("config")) {
      JsonObject objConfig = obj["config"];
      this->config.fromJSON(objConfig);
    }
    return true;
}
bool Transceiver::save() {
    this->config.save();
    ELECHOUSE_cc1101.setRxBW(this->config.rxBandwidth);              // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.setPA(this->config.txPower);                    // Set TxPower. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12) Default is max!
    ELECHOUSE_cc1101.setDeviation(this->config.deviation);
    return true;
}
bool Transceiver::end() {
    this->disableReceive();
    return true;
}
void transceiver_config_t::fromJSON(JsonObject& obj) {
    Serial.print("Deserialize Radio JSON ");
    Serial.printf("SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
    if(obj.containsKey("type")) this->type = obj["type"];
    if (obj.containsKey("internalCCMode")) this->internalCCMode = obj["internalCCMode"];
    if (obj.containsKey("modulationMode")) this->modulationMode = obj["modulationMode"];
    if (obj.containsKey("frequency")) this->frequency = obj["frequency"];  // float
    if (obj.containsKey("deviation")) this->deviation = obj["deviation"];  // float
    if (obj.containsKey("channel")) this->channel = obj["channel"];
    if (obj.containsKey("channelSpacing")) this->channelSpacing = obj["channelSpacing"]; // float
    if (obj.containsKey("rxBandwidth")) this->rxBandwidth = obj["rxBandwidth"]; // float
    if (obj.containsKey("dataRate")) this->dataRate = obj["dataRate"]; // float
    if (obj.containsKey("txPower")) this->txPower = obj["txPower"];
    if (obj.containsKey("syncMode")) this->syncMode = obj["syncMode"];
    if (obj.containsKey("syncWordHigh")) this->syncWordHigh = obj["syncWordHigh"];
    if (obj.containsKey("syncWordLow")) this->syncWordLow = obj["syncWordLow"];
    if (obj.containsKey("addrCheckMode")) this->addrCheckMode = obj["addrCheckMode"];
    if (obj.containsKey("checkAddr")) this->checkAddr = obj["checkAddr"];
    if (obj.containsKey("dataWhitening")) this->dataWhitening = obj["dataWhitening"];
    if (obj.containsKey("pktFormat")) this->pktFormat = obj["pktFormat"];
    if (obj.containsKey("pktLengthMode")) this->pktLengthMode = obj["pktLengthMode"];
    if (obj.containsKey("pktLength")) this->pktLength = obj["pktLength"];
    if (obj.containsKey("useCRC")) this->useCRC = obj["useCRC"];
    if (obj.containsKey("autoFlushCRC")) this->autoFlushCRC = obj["autoFlushCRC"];
    if (obj.containsKey("disableDCFilter")) this->disableDCFilter = obj["disableCRCFilter"];
    if (obj.containsKey("enableManchester")) this->enableManchester = obj["enableManchester"];
    if (obj.containsKey("enableFEC")) this->enableFEC = obj["enableFEC"];
    if (obj.containsKey("minPreambleBytes")) this->minPreambleBytes = obj["minPreambleBytes"];
    if (obj.containsKey("pqtThreshold")) this->pqtThreshold = obj["pqtThreshold"];
    if (obj.containsKey("appendStatus")) this->appendStatus = obj["appendStatus"];
    if (obj.containsKey("printBuffer")) this->printBuffer = obj["printBuffer"];
}
void transceiver_config_t::toJSON(JsonObject& obj) {
    obj["type"] = this->type;
    obj["TXPin"] = this->TXPin;
    obj["RXPin"] = this->RXPin;
    obj["SCKPin"] = this->SCKPin;
    obj["MOSIPin"] = this->MOSIPin;
    obj["MISOPin"] = this->MISOPin;
    obj["CSNPin"] = this->CSNPin;
    obj["internalCCMode"] = this->internalCCMode;
    obj["modulationMode"] = this->modulationMode;
    obj["frequency"] = this->frequency;  // float
    obj["deviation"] = this->deviation;  // float
    obj["channel"] = this->channel;
    obj["channelSpacing"] = this->channelSpacing; // float
    obj["rxBandwidth"] = this->rxBandwidth; // float
    obj["dataRate"] = this->dataRate; // float
    obj["txPower"] = this->txPower;
    obj["syncMode"] = this->syncMode;
    obj["syncWordHigh"] = this->syncWordHigh;
    obj["syncWordLow"] = this->syncWordLow;
    obj["addrCheckMode"] = this->addrCheckMode;
    obj["checkAddr"] = this->checkAddr;
    obj["dataWhitening"] = this->dataWhitening;
    obj["pktFormat"] = this->pktFormat;
    obj["pktLengthMode"] = this->pktLengthMode;
    obj["pktLength"] = this->pktLength;
    obj["useCRC"] = this->useCRC;
    obj["autoFlushCRC"] = this->autoFlushCRC;
    obj["disableDCFilter"] = this->disableDCFilter;
    obj["enableManchester"] = this->enableManchester;
    obj["enableFEC"] = this->enableFEC;
    obj["minPreambleBytes"] = this->minPreambleBytes;
    obj["pqtThreshold"] = this->pqtThreshold;
    obj["appendStatus"] = this->appendStatus;
    obj["printBuffer"] = somfy.transceiver.printBuffer;
    Serial.print("Serialize Radio JSON ");
    Serial.printf("SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}
void transceiver_config_t::save() {
    pref.begin("CC1101");
    pref.putUChar("type", this->type);
    pref.putUChar("TXPin", this->TXPin);
    pref.putUChar("RXPin", this->RXPin);
    pref.putUChar("SCKPin", this->SCKPin);
    pref.putUChar("MOSIPin", this->MOSIPin);
    pref.putUChar("MISOPin", this->MISOPin);
    pref.putUChar("CSNPin", this->CSNPin);
    pref.putBool("internalCCMode", this->internalCCMode);
    pref.putUChar("modulationMode", this->modulationMode);
    pref.putFloat("frequency", this->frequency);  // float
    pref.putFloat("deviation", this->deviation);  // float
    pref.putUChar("channel", this->channel);
    pref.putFloat("channelSpacing", this->channelSpacing); // float
    pref.putFloat("rxBandwidth", this->rxBandwidth); // float
    pref.putFloat("dataRate", this->dataRate); // float
    pref.putChar("txPower", this->txPower);
    pref.putUChar("syncMode", this->syncMode);
    pref.putUShort("syncWordHigh", this->syncWordHigh);
    pref.putUShort("syncWordLow", this->syncWordLow);
    pref.putUChar("addrCheckMode", this->addrCheckMode);
    pref.putUChar("checkAddr", this->checkAddr);
    pref.putBool("dataWhitening", this->dataWhitening);
    pref.putUChar("pktFormat", this->pktFormat);
    pref.putUChar("pktLengthMode", this->pktLengthMode);
    pref.putUChar("pktLength", this->pktLength);
    pref.putBool("useCRC", this->useCRC);
    pref.putBool("autoFlushCRC", this->autoFlushCRC);
    pref.putBool("disableDCFilter", this->disableDCFilter);
    pref.putBool("enableManchester", this->enableManchester);
    pref.putBool("enableFEC", this->enableFEC);
    pref.putUChar("minPreambleBytes", this->minPreambleBytes);
    pref.putUChar("pqtThreshold", this->pqtThreshold);
    pref.putBool("appendStatus", this->appendStatus);
    pref.end();
    Serial.print("Save Radio Settings ");
    Serial.printf("SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}
void transceiver_config_t::load() {
    pref.begin("CC1101");
    this->type = pref.getUChar("type", 56);
    this->TXPin = pref.getUChar("TXPin", this->TXPin);
    this->RXPin = pref.getUChar("RXPin", this->RXPin);
    this->SCKPin = pref.getUChar("SCKPin", this->SCKPin);
    this->MOSIPin = pref.getUChar("MOSIPin", this->MOSIPin);
    this->MISOPin = pref.getUChar("MISOPin", this->MISOPin);
    this->CSNPin = pref.getUChar("CSNPin", this->CSNPin);
    this->internalCCMode = pref.getBool("internalCCMode", this->internalCCMode);
    this->modulationMode = pref.getUChar("modulationMode", this->modulationMode);
    this->frequency = pref.getFloat("frequency", this->frequency);  // float
    this->deviation = pref.getFloat("deviation", this->deviation);  // float
    this->channel = pref.getUChar("channel", this->channel);
    this->channelSpacing = pref.getFloat("channelSpacing", this->channelSpacing); // float
    this->rxBandwidth = pref.getFloat("rxBandwidth", this->rxBandwidth); // float
    this->dataRate = pref.getFloat("dataRate", this->dataRate); // float
    this->txPower = pref.getChar("txPower", this->txPower);
    this->syncMode = pref.getUChar("syncMode", this->syncMode);
    this->syncWordHigh = pref.getUShort("syncWordHigh", this->syncWordHigh);
    this->syncWordLow = pref.getUShort("syncWordLow", this->syncWordLow);
    this->addrCheckMode = pref.getUChar("addrCheckMode", this->addrCheckMode);
    this->checkAddr = pref.getUChar("checkAddr", this->checkAddr);
    this->dataWhitening = pref.getBool("dataWhitening", this->dataWhitening);
    this->pktFormat = pref.getUChar("pktFormat", this->pktFormat);
    this->pktLengthMode = pref.getUChar("pktLengthMode", this->pktLengthMode);
    this->pktLength = pref.getUChar("pktLength", this->pktLength);
    this->useCRC = pref.getBool("useCRC", this->useCRC);
    this->autoFlushCRC = pref.getBool("autoFlushCRC", this->autoFlushCRC);
    this->disableDCFilter = pref.getBool("disableDCFilter", this->disableDCFilter);
    this->enableManchester = pref.getBool("enableManchester", this->enableManchester);
    this->enableFEC = pref.getBool("enableFEC", this->enableFEC);
    this->minPreambleBytes = pref.getUChar("minPreambleBytes", this->minPreambleBytes);
    this->pqtThreshold = pref.getUChar("pqtThreshold", this->pqtThreshold);
    this->appendStatus = pref.getBool("appendStatus", this->appendStatus);
    pref.end();
    this->printBuffer = somfy.transceiver.printBuffer;
}
void transceiver_config_t::apply() {
    somfy.transceiver.disableReceive();
    bit_length = this->type;    
    Serial.print("Applying radio settings ");
    Serial.printf("SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u\n", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
    ELECHOUSE_cc1101.setGDO(this->RXPin, this->TXPin);
    ELECHOUSE_cc1101.setSpiPin(this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin);
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(this->frequency);                 // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
    ELECHOUSE_cc1101.setRxBW(this->rxBandwidth);              // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.setPA(this->txPower);                    // Set TxPower. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12) Default is max!
    //ELECHOUSE_cc1101.setCCMode(this->internalCCMode);         // set config for internal transmission mode.
    ELECHOUSE_cc1101.setModulation(this->modulationMode);     // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    /*
    ELECHOUSE_cc1101.setDeviation(this->deviation);           // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
    ELECHOUSE_cc1101.setChannel(this->channel);               // Set the Channelnumber from 0 to 255. Default is cahnnel 0.
    ELECHOUSE_cc1101.setChsp(this->channelSpacing);           // The channel spacing is multiplied by the channel number CHAN and added to the base frequency in kHz. Value from 25.39 to 405.45. Default is 199.95 kHz.
    ELECHOUSE_cc1101.setDRate(this->dataRate);                // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setSyncMode(this->syncMode);             // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.
    ELECHOUSE_cc1101.setSyncWord(this->syncWordHigh, this->syncWordLow); // Set sync word. Must be the same for the transmitter and receiver. (Syncword high, Syncword low)
    ELECHOUSE_cc1101.setAdrChk(this->addrCheckMode);          // Controls address check configuration of received packages. 0 = No address check. 1 = Address check, no broadcast. 2 = Address check and 0 (0x00) broadcast. 3 = Address check and 0 (0x00) and 255 (0xFF) broadcast.
    ELECHOUSE_cc1101.setAddr(this->checkAddr);                // Address used for packet filtration. Optional broadcast addresses are 0 (0x00) and 255 (0xFF).
    ELECHOUSE_cc1101.setWhiteData(this->dataWhitening);       // Turn data whitening on / off. 0 = Whitening off. 1 = Whitening on.
    ELECHOUSE_cc1101.setPktFormat(this->pktFormat);           // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX. 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX. 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
    ELECHOUSE_cc1101.setLengthConfig(this->pktLengthMode);    // 0 = Fixed packet length mode. 1 = Variable packet length mode. 2 = Infinite packet length mode. 3 = Reserved
    ELECHOUSE_cc1101.setPacketLength(this->pktLength);        // Indicates the packet length when fixed packet length mode is enabled. If variable packet length mode is used, this value indicates the maximum packet length allowed.
    ELECHOUSE_cc1101.setCrc(this->useCRC);                    // 1 = CRC calculation in TX and CRC check in RX enabled. 0 = CRC disabled for TX and RX.
    ELECHOUSE_cc1101.setCRC_AF(this->autoFlushCRC);           // Enable automatic flush of RX FIFO when CRC is not OK. This requires that only one packet is in the RXIFIFO and that packet length is limited to the RX FIFO size.
    ELECHOUSE_cc1101.setDcFilterOff(this->disableDCFilter);   // Disable digital DC blocking filter before demodulator. Only for data rates â‰¤ 250 kBaud The recommended IF frequency changes when the DC blocking is disabled. 1 = Disable (current optimized). 0 = Enable (better sensitivity).
    ELECHOUSE_cc1101.setManchester(this->enableManchester);   // Enables Manchester encoding/decoding. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setFEC(this->enableFEC);                 // Enable Forward Error Correction (FEC) with interleaving for packet payload (Only supported for fixed packet length mode. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setPRE(this->minPreambleBytes);          // Sets the minimum number of preamble bytes to be transmitted. Values: 0 : 2, 1 : 3, 2 : 4, 3 : 6, 4 : 8, 5 : 12, 6 : 16, 7 : 24
    ELECHOUSE_cc1101.setPQT(this->pqtThreshold);              // Preamble quality estimator threshold. The preamble quality estimator increases an internal counter by one each time a bit is received that is different from the previous bit, and decreases the counter by 8 each time a bit is received that is the same as the last bit. A threshold of 4âˆ™PQT for this counter is used to gate sync word detection. When PQT=0 a sync word is always accepted.
    ELECHOUSE_cc1101.setAppendStatus(this->appendStatus);     // When enabled, two status bytes will be appended to the payload of the packet. The status bytes contain RSSI and LQI values, as well as CRC OK.
    */
    //somfy.transceiver.printBuffer = this->printBuffer;
    if (!ELECHOUSE_cc1101.getCC1101()) {
        Serial.println("Error setting up the radio");
    }
    else {
        Serial.println("Successfully set up the radio");
        somfy.transceiver.enableReceive();
    }
}
bool Transceiver::begin() {
    this->config.load();
    this->config.apply();
    return true;
}
void Transceiver::loop() {
    if (this->receive()) {
        this->clearReceived();
        somfy.processFrame(this->frame);
        char buf[128];
        sprintf(buf, "{\"address\":%d, \"rcode\":%d, \"command\":\"%s\"}", this->frame.remoteAddress, this->frame.rollingCode, translateSomfyCommand(this->frame.cmd));
        sockEmit.sendToClients("remoteFrame", buf);
    }
}
somfy_frame_t& Transceiver::lastFrame() { return this->frame; }
void Transceiver::beginTransmit() {
    this->disableReceive();
    ELECHOUSE_cc1101.SetTx();
}
void Transceiver::endTransmit() {
    ELECHOUSE_cc1101.setSidle();
    delay(100);
    this->enableReceive();
}
