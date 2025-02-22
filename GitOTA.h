#ifndef GITOTA_H
#define GITOTA_H
#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>
#include "ConfigSettings.h"

#define GIT_MAX_RELEASES 5
#define GIT_STATUS_READY 0
#define GIT_STATUS_CHECK 1
#define GIT_AWAITING_UPDATE 2
#define GIT_UPDATING 3
#define GIT_UPDATE_COMPLETE 4
#define GIT_UPDATE_CANCELLING 5
#define GIT_UPDATE_CANCELLED 6
class GitRelease {
  public:
    uint64_t id = 0;
    bool draft = false;
    bool preRelease = false;
    bool main = false;
    time_t releaseDate;
    char name[32] = "";   
    appver_t version;
    void setProperty(const char *key, const char *val);
    bool toJSON(JsonObject &obj);
};

class GitRepo {
  public:
    int16_t getReleases(uint8_t num = GIT_MAX_RELEASES);
    GitRelease releases[GIT_MAX_RELEASES + 1];
    bool toJSON(JsonObject &obj);
};
class GitUpdater {
  public:
    uint8_t status = 0;
    uint32_t lastCheck = 0;
    bool updateAvailable = false;
    appver_t latest;
    bool cancelled = false;
    int16_t error = 0;
    char targetRelease[32];
    char currentFile[64] = "";
    char baseUrl[128] = "";
    int partition = 0;
    void checkForUpdate();
    bool beginUpdate(const char *release);
    bool endUpdate();
    int8_t downloadFile();
    void setCurrentRelease(GitRepo &repo);
    void loop();
    void toJSON(JsonObject &obj);
    int checkInternet();
    void emitUpdateCheck(uint8_t num=255);
    void emitDownloadProgress(size_t total, size_t loaded, const char *evt = "updateProgress");
    void emitDownloadProgress(uint8_t num, size_t total, size_t loaded, const char *evt = "updateProgress");    
};
#endif
