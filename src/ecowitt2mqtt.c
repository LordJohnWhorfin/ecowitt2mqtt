/*
 * ecowitt2mqtt.c
 *
 * Weather station daemon for Debian
 * Queries binary socket API, parses 344-byte frame,
 * publishes per-sensor MQTT messages + global summary.
 *
 * Supports config file: /etc/ecowitt2mqtt.conf
 * Supports foreground mode: --foreground
 *
 * Logs to syslog as daemon, stderr as foreground.
 *
 * References used: https://osswww.ecowitt.net/uploads/20220407/WN1900%20GW1000,1100%20WH2680,2650%20telenet%20v1.6.4.pdf
 * https://blog.meteodrenthe.nl/2023/02/03/how-to-use-the-ecowitt-gateway-gw1000-gw1100-local-api/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <getopt.h>
#include <mosquitto.h>

#include "ecowitt.h"

#define MQTT_QOS                     1
#define MQTT_TIMEOUT                 10000L

#define MQTT_MESSAGE_MAXLEN          32
#define MESSAGE_EXPIRATION_SECONDS   60

#define RECEIVE_BUFFER_OK            0
#define INVALID_HEADER              -1
#define INVALID_CHECKSUM            -2
#define INVALID_LENGTH              -3

#define TOPIC_ALL_DATA_REQUEST       "all_data/request"
#define MSG_ALL_DATA_JSON            "json"
#define MSG_ALL_DATA_RAW             "raw"
#define TOPIC_ALL_DATA_RAW           "all_data/raw"
#define TOPIC_ALL_DATA_JSON          "all_data/json"

char weather_host[64] = "127.0.0.1";
int weather_port = 45000;
int interval = 30;
bool verbose = false;
bool foreground = false;
char mqtt_broker_host[128] = "localhost";
int mqtt_broker_port       = 1883;
char mqtt_clientid[64]     = "ecowitt2mqtt";
char mqtt_base_topic[64]   = "ecowitt";

unsigned char data_buffer[1024];
int data_buffer_len = 0;
time_t data_buffer_last_update = 0;


#pragma mark -

typedef enum {
    TAG_TYPE_BYTE_LEAVE_ALONE,
    TAG_TYPE_SHORT_LEAVE_ALONE,
    TAG_TYPE_3_BYTES_LEAVE_ALONE,
    TAG_TYPE_INT_LEAVE_ALONE,
    TAG_TYPE_SHORT_DIVIDE_BY_10_UNSIGNED,
    TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED,
    TAG_TYPE_3_BYTES_TEMP_AND_BATT,
    TAG_TYPE_3_BYTES_TIME,
    TAG_TYPE_6_BYTES_TIME,
    TAG_TYPE_16_BYTES_BITMASK,
    TAG_TYPE_16_BYTES_CO2,
    TAG_TYPE_20_BYTES_PIEZO_GAIN,
    TAG_TYPE_PM25_AQI,
    
} TAG_PROCESSING_TYPE;

int tagTypeDataLength(TAG_PROCESSING_TYPE tagType) {
    switch (tagType) {
        case TAG_TYPE_BYTE_LEAVE_ALONE:
            return 1;
        case TAG_TYPE_SHORT_LEAVE_ALONE:
        case TAG_TYPE_SHORT_DIVIDE_BY_10_UNSIGNED:
        case TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED:
            return 2;
        case TAG_TYPE_3_BYTES_LEAVE_ALONE:
        case TAG_TYPE_3_BYTES_TEMP_AND_BATT:
        case TAG_TYPE_3_BYTES_TIME:
            return 3;
        case TAG_TYPE_INT_LEAVE_ALONE:
            return 4;
        case TAG_TYPE_6_BYTES_TIME:
            return 6;
        case TAG_TYPE_16_BYTES_BITMASK:
        case TAG_TYPE_16_BYTES_CO2:
            return 16;
        case TAG_TYPE_20_BYTES_PIEZO_GAIN:
            return 20;
        case TAG_TYPE_PM25_AQI:
            return 0;
        default:
            return 0;
    }
}

typedef struct {
    unsigned char           tag;
    TAG_PROCESSING_TYPE     type;
    char*                   topic;
    char                    lastMessage[MQTT_MESSAGE_MAXLEN];
    time_t                  lastMessageTimestamp;
} TagSpec;

TagSpec tagData[] = {
    { .tag = ITEM_INTEMP                , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/indoors"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_OUTTEMP               , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/outdoors"   , .lastMessageTimestamp = 0 },
    { .tag = ITEM_DEWPOINT              , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "dew_point"              , .lastMessageTimestamp = 0 },
    { .tag = ITEM_WINDCHILL             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "wind_chill"             , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HEATINDEX             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "heat_index"             , .lastMessageTimestamp = 0 },
    { .tag = ITEM_INHUMI                , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/indoors"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_OUTHUMI               , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/outdoors"      , .lastMessageTimestamp = 0 },
    { .tag = ITEM_ABSBARO               , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_UNSIGNED  , .topic = "barometric/absolute"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RELBARO               , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_UNSIGNED  , .topic = "barometric/relative"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_WINDDIRECTION         , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "wind/direction"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_WINDSPEED             , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "wind/speed"             , .lastMessageTimestamp = 0 },
    { .tag = ITEM_GUSTSPEED             , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "wind/gust_speed"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINEVENT             , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/event"             , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINRATE              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/rate"              , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINHOUR              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/hour"              , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINDAY               , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/day"               , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINWEEK              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/week"              , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINMONTH             , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/month"             , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINYEAR              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/year"              , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RAINTOTALS            , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/totals"            , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LIGHT                 , .type = TAG_TYPE_INT_LEAVE_ALONE              , .topic = "light"                  , .lastMessageTimestamp = 0 },
    { .tag = ITEM_UV                    , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "uv/intensity"           , .lastMessageTimestamp = 0 },
    { .tag = ITEM_UVI                   , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "uv/index"               , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TIME                  , .type = TAG_TYPE_6_BYTES_TIME                 , .topic = "date_and_time"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_DAYLWINDMAX           , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "wind/day_max"           , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP1                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_1"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP2                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_2"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP3                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_3"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP4                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_4"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP5                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_5"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP6                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_6"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP7                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_7"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TEMP8                 , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/th_8"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI1                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_1"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI2                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_2"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI3                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_3"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI4                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_4"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI5                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_5"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI6                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_6"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI7                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_7"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_HUMI8                 , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "humidity/th_8"          , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_CH1              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "air_quality"            , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP1             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_1"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE1         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_1"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP2             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_2"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE2         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_2"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP3             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_3"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE3         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_3"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP4             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_4"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE4         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_4"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP5             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_5"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE5         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_5"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP6             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_6"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE6         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_6"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP7             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_7"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE7         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_7"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP8             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_8"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE8         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_8"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP9             , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_9"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE9         , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_9"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP10            , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_10"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE10        , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_10"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP11            , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_11"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE11        , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_11"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP12            , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_12"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE12        , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_12"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP13            , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_13"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE13        , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_13"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP14            , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_14"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE14        , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_14"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP15            , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_15"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE15        , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_15"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILTEMP16            , .type = TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED    , .topic = "temperature/soil_16"    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SOILMOISTURE16        , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "moisture/soil_16"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LOWBATT               , .type = TAG_TYPE_16_BYTES_BITMASK             , .topic = "all_sensor_low_battery" , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_24HAVG1          , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "pm25/ch1"               , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_24HAVG2          , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "pm25/ch2"               , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_24HAVG3          , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "pm25/ch3"               , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_24HAVG4          , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "pm25/ch4"               , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_CH2              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "aqs/2"                  , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_CH3              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "aqs/3"                  , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_CH4              , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "aqs/4"                  , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAK_CH1              , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leak/1"                 , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAK_CH2              , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leak/2"                 , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAK_CH3              , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leak/3"                 , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAK_CH4              , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leak/4"                 , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LIGHTNING             , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "lightning/distance"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LIGHTNING_TIME        , .type = TAG_TYPE_INT_LEAVE_ALONE              , .topic = "lightning/time"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LIGHTNING_POWER       , .type = TAG_TYPE_INT_LEAVE_ALONE              , .topic = "lightning/day_counter"  , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR1               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t1"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR2               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t2"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR3               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t3"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR4               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t4"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR5               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t5"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR6               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t6"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR7               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t7"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_TF_USR8               , .type = TAG_TYPE_3_BYTES_TEMP_AND_BATT        , .topic = "temperature/t8"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_SENSOR_CO2            , .type = TAG_TYPE_16_BYTES_CO2                 , .topic = "co2"                    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_PM25_AQI              , .type = TAG_TYPE_PM25_AQI                     , .topic = "aqi"                    , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH1      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/1"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH2      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/2"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH3      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/3"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH4      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/4"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH5      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/5"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH6      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/6"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH7      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/7"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_LEAF_WETNESS_CH8      , .type = TAG_TYPE_BYTE_LEAVE_ALONE             , .topic = "leaf_wetness/8"         , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_Rain_Rate       , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/piezo/rate"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_Event_Rain      , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/piezo/event"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_Hourly_Rain     , .type = TAG_TYPE_SHORT_LEAVE_ALONE            , .topic = "rain/piezo/hourly"      , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_Daily_Rain      , .type = TAG_TYPE_INT_LEAVE_ALONE              , .topic = "rain/piezo/daily"       , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_Weekly_Rain     , .type = TAG_TYPE_INT_LEAVE_ALONE              , .topic = "rain/piezo/weekly"      , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_Monthly_Rain    , .type = TAG_TYPE_INT_LEAVE_ALONE              , .topic = "rain/piezo/monthly"     , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_yearly_Rain     , .type = TAG_TYPE_INT_LEAVE_ALONE              , .topic = "rain/piezo/yearly"      , .lastMessageTimestamp = 0 },
    { .tag = ITEM_Piezo_Gain10          , .type = TAG_TYPE_20_BYTES_PIEZO_GAIN          , .topic = "rain/piezo/gain"        , .lastMessageTimestamp = 0 },
    { .tag = ITEM_RST_RainTime          , .type = TAG_TYPE_3_BYTES_TIME                 , .topic = "rain/rst/time"          , .lastMessageTimestamp = 0 },
};

#pragma mark -

int tag_count() {
    return sizeof(tagData) / sizeof(tagData[0]);
}

int tag_index(int tag) {
    for (int i = tag_count() -1; i >= 0; i--) {
        if (tag == tagData[i].tag) {
            return i;
        }
    }
    return -1;
}




#pragma mark -
void load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "host")) sscanf(line, "host = %63s", weather_host);
        if (strstr(line, "port")) sscanf(line, "port = %d", &weather_port);
        if (strstr(line, "interval")) sscanf(line, "interval = %d", &interval);
        if (strstr(line, "broker_host")) sscanf(line, "broker = %127s", mqtt_broker_host);
        if (strstr(line, "broker_port")) sscanf(line, "broker = %d", &mqtt_broker_port);
        if (strstr(line, "clientid")) sscanf(line, "clientid = %63s", mqtt_clientid);
        if (strstr(line, "base_topic")) sscanf(line, "base_topic = %63s", mqtt_base_topic);
    }
    fclose(f);
}

#pragma mark -

void mqtt_publish_data(struct mosquitto *mosq, const char *topic_suffix, const void *payload, int payload_len) {
    char full_topic[128];
    snprintf(full_topic, sizeof(full_topic), "%s/%s", mqtt_base_topic, topic_suffix);
    if (foreground && verbose) {
        printf("Publishing on topic %s\n", full_topic);
    }
    int rc = mosquitto_publish(mosq, NULL, full_topic, payload_len, payload, 0, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error publishing message: %s\n", mosquitto_strerror(rc));
    }
}

void mqtt_publish(struct mosquitto *mosq, const char *topic_suffix, const char *payload) {
    mqtt_publish_data(mosq, topic_suffix, payload, strlen(payload));
}

void mqtt_subscribe(struct mosquitto *mosq, const char *topic_suffix) {
    char full_topic[128];
    snprintf(full_topic, sizeof(full_topic), "%s/%s", mqtt_base_topic, topic_suffix);
    if (foreground && verbose) {
        printf("Subscribing to topic %s\n", full_topic);
    }
    int rc = mosquitto_subscribe(mosq, NULL, full_topic, 0);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error subscribing to topic %s: %s\n", full_topic, mosquitto_strerror(rc));
    }
}

void publish_raw(struct mosquitto *mosq) {
    time_t now;
    time(&now);
    if ((now - data_buffer_last_update) > MESSAGE_EXPIRATION_SECONDS) {
        fprintf(stderr, "Can't publish data, it's stale. Haven't received an update in %ld seconds\n", now - data_buffer_last_update);
    }
    else {
        if (data_buffer_len == 0) {
            fprintf(stderr, "Can't publish data, there isn't any\n");
        }
        else {
            mqtt_publish_data(mosq, TOPIC_ALL_DATA_RAW, data_buffer, data_buffer_len);
        }
    }
}

void publish_json(struct mosquitto *mosq) {
    time_t now;
    time(&now);
    char json_buffer[1024];
    char strbuf[128];
    bool firstTopic = true;
    strcpy(json_buffer, "{\n");
    for (int ti = tag_count() -1; ti >= 0; ti--) {
        if (tagData[ti].lastMessage[0] && ((now - tagData[ti].lastMessageTimestamp) <= MESSAGE_EXPIRATION_SECONDS)) {
            if (firstTopic) {
                firstTopic = false;
            }
            else {
                strcat(json_buffer, ",\n");
            }
            sprintf(strbuf, "\"%s\": \"%s\"", tagData[ti].topic, tagData[ti].lastMessage);
            strcat(json_buffer, strbuf);
        }
    }
    strcat(json_buffer, "\n}");
    if (firstTopic) {
        fprintf(stderr, "No recent data to publish\n");
    }
    else {
        mqtt_publish(mosq, TOPIC_ALL_DATA_JSON, json_buffer);
    }
}


#pragma mark - MQTT Callbacks

// Callback function for when a connection is established or fails
void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if (foreground) {
        if (rc == 0) {
            printf("Connected to MQTT broker successfully.\n");
        } else {
            fprintf(stderr, "Connection failed: %s\n", mosquitto_connack_string(rc));
        }
    }
}

// Callback function for when a connection is established or fails
void on_disconnect(struct mosquitto *mosq, void *obj, int rc) {
    if (foreground) {
        if (rc == 0) {
            printf("Disconnected from MQTT broker successfully.\n");
        } else {
            fprintf(stderr, "Disconnection failed: %s\n", mosquitto_connack_string(rc));
        }
    }
}

// Callback function for when a message is published
void on_publish(struct mosquitto *mosq, void *obj, int mid) {
    if (foreground) {
        printf("Message published with mid: %d\n", mid);
    }
}

// Callback function for when a message is published
void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos) {
    if (foreground) {
        printf("Topic subscribed with mid: %d\n", mid);
    }
}

// Callback function for when a message is received on a subscribed topic
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message) {
    char payload[128];
    strncpy(payload, message->payload, message->payloadlen);
    payload[message->payloadlen] = 0;
    if (foreground) {
        printf("Message received for %s: %s\n", message->topic, payload);
    }
    char full_topic[128];
    snprintf(full_topic, sizeof(full_topic), "%s/%s", mqtt_base_topic, TOPIC_ALL_DATA_REQUEST);
    if (strcmp(message->topic, full_topic) == 0) {
        if (strcmp(payload, MSG_ALL_DATA_JSON) == 0) {
            publish_json(mosq);
        }
        else if (strcmp(payload, MSG_ALL_DATA_RAW) == 0) {
            publish_raw(mosq);
        }
        else {
            fprintf(stderr, "Data type not supported for message %s: %s\n", message->topic, payload);
        }
    }
    else {
        fprintf(stderr, "Missing topic handler for subscribed topic: %s\n", message->topic);
    }
}


#pragma mark - Parsing

int process_tag(unsigned char *buf, struct mosquitto *mosq) {
    int ti = tag_index(buf[0]);
    if (ti >= 0) {
        char* subtopic = tagData[ti].topic;
        int tagType = tagData[ti].type;
        int length = tagTypeDataLength(tagType);
        if (foreground && verbose) {
            printf("Processing tag 0x%02X index is %d type:%d length = %d subtopic = %s\n", buf[0], ti, tagType, length, subtopic);
        }
        char batttopic[256];
        char payload[256];
        payload[0] = 0;
        int tmpInt;
        switch (tagType) {
            case TAG_TYPE_BYTE_LEAVE_ALONE:
                snprintf(payload, sizeof(payload), "%d", buf[1]);
                break;
            case TAG_TYPE_SHORT_LEAVE_ALONE:
                tmpInt = buf[1];
                tmpInt = (tmpInt << 8) + buf[2];
                snprintf(payload, sizeof(payload), "%d", tmpInt);
                break;
            case TAG_TYPE_3_BYTES_LEAVE_ALONE:
                tmpInt = buf[1];
                tmpInt = (tmpInt << 8) + buf[2];
                tmpInt = (tmpInt << 8) + buf[3];
                snprintf(payload, sizeof(payload), "%d", tmpInt);
                break;
            case TAG_TYPE_INT_LEAVE_ALONE:
                tmpInt = buf[1];
                tmpInt = (tmpInt << 8) + buf[2];
                tmpInt = (tmpInt << 8) + buf[3];
                tmpInt = (tmpInt << 8) + buf[4];
                snprintf(payload, sizeof(payload), "%d", tmpInt);
                break;
            case TAG_TYPE_SHORT_DIVIDE_BY_10_UNSIGNED:
                tmpInt = buf[1];
                tmpInt = (tmpInt << 8) + buf[2];
                snprintf(payload, sizeof(payload), "%.1f", tmpInt / 10.0);
                break;
            case TAG_TYPE_SHORT_DIVIDE_BY_10_SIGNED:
                tmpInt = buf[1];
                tmpInt = (tmpInt << 8) + buf[2];
                if (buf[1] & 0x80) { // if highest bit of short is set it's a negative number
                    tmpInt = tmpInt - 0xFFFF;
                }
                snprintf(payload, sizeof(payload), "%.1f", tmpInt / 10.0);
                break;
            case TAG_TYPE_3_BYTES_TEMP_AND_BATT:
                tmpInt = buf[1];
                tmpInt = (tmpInt << 8) + buf[2];
                if (buf[1] & 0x80) { // if highest bit of short is set it's a negative number
                    tmpInt = tmpInt - 0xFFFF;
                }
                char* sensor = strrchr(subtopic, '/');
                strcpy(batttopic, "battery");
                strcat(batttopic, sensor);
                snprintf(payload, sizeof(payload), "%.2f", buf[3] * 0.02);
                mqtt_publish(mosq, batttopic, payload);
                
                snprintf(payload, sizeof(payload), "%.1f", tmpInt / 10.0);
                break;
            case TAG_TYPE_3_BYTES_TIME:
                payload[0] = 0;
                break;
            case TAG_TYPE_6_BYTES_TIME:
                payload[0] = 0;
                break;
            case TAG_TYPE_16_BYTES_BITMASK:
                for (int i = 0; i < 16; i++) {
                    for (int b = 0; b < 8; b++) {
                        payload[(8*i) + (7 - b)] = (buf[1 + i] & (1 << b)) ? '1' : '0';
                    }
                }
                payload[128] = 0;
                break;
            case TAG_TYPE_16_BYTES_CO2:
            case TAG_TYPE_20_BYTES_PIEZO_GAIN:
                payload[0] = 0;
                break;
            case TAG_TYPE_PM25_AQI:
                payload[0] = 0;
                return -1;
                break;
        }
        if (payload[0]) {
            mqtt_publish(mosq, subtopic, payload);
            strncpy(tagData[ti].lastMessage, payload, MQTT_MESSAGE_MAXLEN);
            time(&tagData[ti].lastMessageTimestamp);
        }
        else {
            fprintf(stderr, "No payload to publish\n");
        }
        return 1 + length;
    }
    else {
        return -1;
    }
}

void parse_and_publish(unsigned char *buf, struct mosquitto *mosq) {
    if (foreground && verbose) printf("Parse and publish buffer starts\n");
    // skip 0xFFFF header
    buf += 2;
    int readBytes = 0;
    // skip command
    buf++; readBytes++;
    // skip length
    int length = buf[0];
    length = (length << 8) + buf[1];
    buf += 2;
    readBytes += 2;
    
    data_buffer_len = length;
    memcpy(data_buffer, buf, data_buffer_len);
    time(&data_buffer_last_update);
    
    while (readBytes < length) {
        int tagChunkSize = process_tag(buf, mosq);
        if (tagChunkSize > 0) {
            readBytes += tagChunkSize;
            buf += tagChunkSize;
        }
        else {
            break;
        }
    }
}

int check_receive_buffer(unsigned char* receive_buffer) {
    if ((receive_buffer[0] != 0xFF)||(receive_buffer[1] != 0xFF))
        return INVALID_HEADER;
    int length = receive_buffer[3];
    length = (length << 8) + receive_buffer[4];
    int checksum = 0;
    for (int i = 2; i <= length; i++) {
        checksum += receive_buffer[i];
    }
    checksum = checksum % 256;
    if (checksum != receive_buffer[length + 1])
        return INVALID_CHECKSUM;
    return RECEIVE_BUFFER_OK;
}


#pragma mark -

int prepare_command_buffer(unsigned char* command_buffer, unsigned char cmd, unsigned char* payload, unsigned int length) {
    if (length >= (0xFF - 3)) {
        fprintf(stderr, "Attempting to write a payload longer than allowed (%d, max is %d\n", length, 0xFF - 3);
        return -1;
    }
    if ((length > 0) && (payload != NULL)) {
        fprintf(stderr, "Attempting to write a payload length %d but payload pointer is null\n", length);
        return -1;
    }
    command_buffer[0] = 0xFF;
    command_buffer[1] = 0xFF;
    command_buffer[2] = cmd;
    command_buffer[3] = 3 + length; // size excludes 2 byte fixed header, so it's 3 bytes for cmd, size, and payload, + length of payload
    if (length) {
        memcpy(&command_buffer[4], payload, length);
    }
    unsigned int checksum = 0;
    for (int i = 2; i <= 3 + length; i++) {
        checksum += command_buffer[i];
    }
    command_buffer[3 + length + 1] = 0xFF & (checksum % 256);
    return 5 + length;
}



#pragma mark -

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--foreground") == 0) foreground = true;
        if (strcmp(argv[i], "--verbose") == 0) verbose = true;
    }
    load_config("/etc/ecowitt2mqtt.conf");
    if (!foreground) daemon(0,0);
    if (foreground) {
        printf("Starting in foreground\n");
        printf("Ecowitt host:%s port %d\n", weather_host, weather_port);
        printf("MQTT host:%s port %d\n", mqtt_broker_host, mqtt_broker_port);
    }
    else {
        openlog("ecowitt2mqtt", LOG_PID, LOG_DAEMON);
    }
    
    unsigned char COMMAND_BUFFER[260]; // enough for max size (255) + 2 bytes header
    unsigned char RECEIVE_BUFFER[1024];
    struct mosquitto *mosq = NULL;
    int returnCode = 0;
    
    mosquitto_lib_init();
    mosq = mosquitto_new(mqtt_clientid, true, NULL);
    if (mosq) {
        mosquitto_connect_callback_set(mosq, on_connect);
        mosquitto_disconnect_callback_set(mosq, on_disconnect);
        mosquitto_publish_callback_set(mosq, on_publish);
        mosquitto_subscribe_callback_set(mosq, on_subscribe);
        mosquitto_message_callback_set(mosq, on_message);
        
        int rc = mosquitto_connect(mosq, mqtt_broker_host, mqtt_broker_port, 10); // Keepalive of 60 seconds
        if (rc == MOSQ_ERR_SUCCESS) {
            mosquitto_loop_start(mosq);
            
            mqtt_subscribe(mosq, TOPIC_ALL_DATA_REQUEST);
            
            int query_length = prepare_command_buffer(COMMAND_BUFFER, CMD_GW1000_LIVEDATA, NULL, 0);
            
            while (1) {
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in addr = {0};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(weather_port);
                inet_aton(weather_host, &addr.sin_addr);
                
                if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                    if (foreground) perror("connect"); else syslog(LOG_ERR, "connect failed");
                    close(sock);
                    sleep(interval);
                    continue;
                }
                
                send(sock, COMMAND_BUFFER, query_length, 0);
                ssize_t n = recv(sock, RECEIVE_BUFFER, sizeof(RECEIVE_BUFFER), 0);
                switch (check_receive_buffer(RECEIVE_BUFFER)) {
                    case RECEIVE_BUFFER_OK:
                        if (foreground && verbose) {
                            printf("Received %ld bytes buffer:\n", n);
                            int i = 0;
                            while (i < n) {
                                fprintf(stderr, "     ");
                                for (int c = 0; c < 16; c++, i++) {
                                    if (i >= n) break;
                                    fprintf(stderr, "%02X ", RECEIVE_BUFFER[i]);
                                }
                                fprintf(stderr, "\n");
                            }
                        }
                        parse_and_publish(RECEIVE_BUFFER, mosq);
                        break;
                    case INVALID_HEADER:
                        fprintf(stderr, "invalid header returned: 0x%02X%02X\n", RECEIVE_BUFFER[0], RECEIVE_BUFFER[1]);
                        break;
                    case INVALID_CHECKSUM:
                        fprintf(stderr, "invalid checksum\n");
                        break;
                    case INVALID_LENGTH:
                        fprintf(stderr, "invalid length\n");
                        break;
                        
                }
                
                close(sock);
                sleep(interval);
            }
            mosquitto_disconnect(mosq);
            mosquitto_loop_stop(mosq, true);
        }
        else {
            fprintf(stderr, "Could not connect to MQTT server\n");
            returnCode = 1;
        }
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return returnCode;
    }
    else {
        fprintf(stderr, "Could not create mosquitto object\n");
        return 1;
    }
}

