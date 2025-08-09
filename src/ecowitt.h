/*
  ecowitt.h


  Based on Ecowitt API at
  https://osswww.ecowitt.net/uploads/20220407/WN1900%20GW1000,1100%20WH2680,2650%20telenet%20v1.6.4.pdf
*/

/*
Data exchange format：
Fixed header, CMD, SIZE, DATA1, DATA2, … , DATAn, CHECKSUM
Fixed header: 2 bytes, header is fixed as 0xffff
CMD: 1 byte, Command
SIZE: 1 byte, packet size，counted from CMD till CHECKSUM
DATA: n bytes, payloads，variable length
CHECKSUM: 1 byte, CHECKSUM=CMD+SIZE+DATA1+DATA2+…+DATAn
*/

typedef enum {
    CMD_WRITE_SSID                  = 0x11, // send SSID and Password to WIFI module
    CMD_BROADCAST                   = 0x12, // UDP cast for device echo，answer back data size is 2 Bytes
    CMD_READ_ECOWITT                = 0x1E, // read aw.net setting
    CMD_WRITE_ECOWITT               = 0x1F, // write back awt.net setting
    CMD_READ_WUNDERGROUND           = 0x20, // read Wunderground setting
    CMD_WRITE_WUNDERGROUND          = 0x21, // write back Wunderground setting
    CMD_READ_WOW                    = 0x22, // read WeatherObservationsWebsite setting
    CMD_WRITE_WOW                   = 0x23, // write back WeatherObservationsWebsite setting
    CMD_READ_WEATHERCLOUD           = 0x24, // read Weathercloud setting
    CMD_WRITE_WEATHERCLOUD          = 0x25, // write back Weathercloud setting
    CMD_READ_SATION_MAC             = 0x26, // read MAC address
    // the following command is only valid for GW1000, WH2650 and wn1900
    CMD_GW1000_LIVEDATA             = 0x27, // read current data，reply data size is 2bytes.
    CMD_GET_SOILHUMIAD              = 0x28, // read Soilmoisture Sensor calibration parameters
    CMD_SET_SOILHUMIAD              = 0x29, // write back Soilmoisture Sensor calibration parameters
    CMD_READ_CUSTOMIZED             = 0x2A, // read Customized sever setting
    CMD_WRITE_CUSTOMIZED            = 0x2B, // write back Customized sever setting
    CMD_GET_MulCH_OFFSET            = 0x2C, // read multi channel sensor offset value
    CMD_SET_MulCH_OFFSET            = 0x2D, // write back multi channel sensor OFFSET value
    CMD_GET_PM25_OFFSET             = 0x2E, // read PM2.5OFFSET calibration data
    CMD_SET_PM25_OFFSET             = 0x2F, // writeback PM2.5OFFSET calibration data
    CMD_READ_SSSS                   = 0x30, // read system info
    CMD_WRITE_SSSS                  = 0x31, // write back system info
    CMD_READ_RAINDATA               = 0x34, // read rain data
    CMD_WRITE_RAINDATA              = 0x35, // write back rain data
    CMD_READ_GAIN                   = 0x36, // read rain gain
    CMD_WRITE_GAIN                  = 0x37, // write back rain gain
    CMD_READ_CALIBRATION            = 0x38, // read sensor set offset calibration value
    CMD_WRITE_CALIBRATION           = 0x39, // write back sensor set offset value
    CMD_READ_SENSOR_ID              = 0x3A, // read Sensors ID
    CMD_WRITE_SENSOR_ID             = 0x3B, // write back Sensors ID
    CMD_READ_SENSOR_ID_NEW          = 0x3C, //// this is reserved for newly added sensors
    CMD_WRITE_REBOOT                = 0x40, // system restart
    CMD_WRITE_RESET                 = 0x41, // reset to default
    CMD_WRITE_UPDATE                = 0x43, // firmware upgrade
    CMD_READ_FIRMWARE_VERSION       = 0x50, // read current firmware version number
    CMD_READ_USR_PATH               = 0x51,
    CMD_WRITE_USR_PATH              = 0x52,
    CMD_GET_CO2_OFFSET              = 0x53, // read CO2 OFFSET
    CMD_SET_CO2_OFFSET              = 0x54, // write CO2 OFFSET
    CMD_READ_RSTRAIN_TIME           = 0x55, // read rain reset time
    CMD_WRITE_RSTRAIN_TIME          = 0x56, // write back rain reset time
} CMD_LT;



//********************************************************************************************
#define SOIL_CH_MAX     8
#define WH31_CHANNEL    8
#define PM25_CH_MAX     4
#define LEAK_CH_MAX     4
#define LEAF_CH_MAX     8
typedef enum
{
    //eWH24_SENSOR                  = 0,
    eWH65_SENSOR                    = 0,
    //eWH69_SENSOR,
    eWH68_SENSOR                    = 1,
    eWH80_SENSOR                    = 2, //80H
    eWH40_SENSOR                    = 3,
    eWH25_SENSOR                    = 4,
    eWH26_SENSOR                    = 5,
    eWH31_SENSORCH1                 = 6,
    eWH31_SENSORCH2                 = 7,
    eWH31_SENSORCH3                 = 8,
    eWH31_SENSORCH4                 = 9,
    eWH31_SENSORCH5                 = 10,
    eWH31_SENSORCH6                 = 11,
    eWH31_SENSORCH7                 = 12,
    eWH31_SENSORCH8                 = 13,
    eWH51_SENSORCH1                 = 14,
    eWH51_SENSORCH2                 = 15,
    eWH51_SENSORCH3                 = 16,
    eWH51_SENSORCH4                 = 17,
    eWH51_SENSORCH5                 = 18,
    eWH51_SENSORCH6                 = 19,
    eWH51_SENSORCH7                 = 20,
    eWH51_SENSORCH8                 = 21,
    eWH41_SENSORCH1                 = 22,
    eWH41_SENSORCH2                 = 23,
    eWH41_SENSORCH3                 = 24,
    eWH41_SENSORCH4                 = 25,
    //
    eWH57_SENSOR                    = 26,
    eWH55_SENSORCH1                 = 27,
    eWH55_SENSORCH2                 = 28,
    eWH55_SENSORCH3                 = 29,
    eWH55_SENSORCH4                 = 30,
    eWH34_SENSORCH1                 = 31,
    eWH34_SENSORCH2                 = 32,
    eWH34_SENSORCH3                 = 33,
    eWH34_SENSORCH4                 = 34,
    eWH34_SENSORCH5                 = 35,
    eWH34_SENSORCH6                 = 36,
    eWH34_SENSORCH7                 = 37,
    eWH34_SENSORCH8                 = 38,
    eWH45_SENSOR                    = 39,
    // GW1000 Firmware V1.5.6
    eWH35_SENSORCH1                 = 40,
    eWH35_SENSORCH2                 = 41,
    eWH35_SENSORCH3                 = 42,
    eWH35_SENSORCH4                 = 43,
    eWH35_SENSORCH5                 = 44,
    eWH35_SENSORCH6                 = 45,
    eWH35_SENSORCH7                 = 46,
    eWH35_SENSORCH8                 = 47,
    eWH90_SENSOR                    = 48,
    // the sensor sequence can not be altered!!
    //
    eMAX_SENSOR
} SENSOR_IDT;
//
#define ITEM_INTEMP             0x01//Indoor Temperature (℃) 2
#define ITEM_OUTTEMP            0x02//Outdoor Temperature (℃) 2
#define ITEM_DEWPOINT           0x03//Dew point (℃) 2
#define ITEM_WINDCHILL          0x04//Wind chill (℃) 2
#define ITEM_HEATINDEX          0x05//Heat index (℃) 2
#define ITEM_INHUMI             0x06//Indoor Humidity (%) 1
#define ITEM_OUTHUMI            0x07//Outdoor Humidity (%) 1
#define ITEM_ABSBARO            0x08//Absolutely Barometric (hpa) 2
#define ITEM_RELBARO            0x09//Relative Barometric (hpa) 2
#define ITEM_WINDDIRECTION      0x0A//Wind Direction (360°) 2
#define ITEM_WINDSPEED          0x0B//Wind Speed (m/s) 2
#define ITEM_GUSTSPEED          0x0C//Gust Speed (m/s) 2
#define ITEM_RAINEVENT          0x0D//Rain Event (mm) 2
#define ITEM_RAINRATE           0x0E//Rain Rate (mm/h) 2
#define ITEM_RAINHOUR           0x0F//Rain hour (mm) 2
#define ITEM_RAINDAY            0x10//Rain Day (mm) 2
#define ITEM_RAINWEEK           0x11//Rain Week (mm) 2
#define ITEM_RAINMONTH          0x12//Rain Month (mm) 4
#define ITEM_RAINYEAR           0x13//Rain Year (mm) 4
#define ITEM_RAINTOTALS         0x14//Rain Totals (mm) 4
#define ITEM_LIGHT              0x15//Light (lux) 4
#define ITEM_UV                 0x16//UV (uW/m2) 2
#define ITEM_UVI                0x17//UVI (0-15 index) 1
#define ITEM_TIME               0x18//Date and time 6
#define ITEM_DAYLWINDMAX        0X19//Day max wind(m/s) 2
#define ITEM_TEMP1              0x1A//Temperature 1(℃) 2
#define ITEM_TEMP2              0x1B//Temperature 2(℃) 2
#define ITEM_TEMP3              0x1C//Temperature 3(℃) 2
#define ITEM_TEMP4              0x1D//Temperature 4(℃) 2
#define ITEM_TEMP5              0x1E//Temperature 5(℃) 2
#define ITEM_TEMP6              0x1F//Temperature 6(℃) 2
#define ITEM_TEMP7              0x20//Temperature 7(℃) 2
#define ITEM_TEMP8              0x21//Temperature 8(℃) 2
#define ITEM_HUMI1              0x22//Humidity 1, 0-100% 1
#define ITEM_HUMI2              0x23//Humidity 2, 0-100% 1
#define ITEM_HUMI3              0x24//Humidity 3, 0-100% 1
#define ITEM_HUMI4              0x25//Humidity 4, 0-100% 1
#define ITEM_HUMI5              0x26//Humidity 5, 0-100% 1
#define ITEM_HUMI6              0x27//Humidity 6, 0-100% 1
#define ITEM_HUMI7              0x28//Humidity 7, 0-100% 1
#define ITEM_HUMI8              0x29//Humidity 8, 0-100% 1
#define ITEM_PM25_CH1           0x2A//PM2.5 Air Quality Sensor(μg/m3) 2
#define ITEM_SOILTEMP1          0x2B//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE1      0x2C//Soil Moisture(%) 1
#define ITEM_SOILTEMP2          0x2D//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE2      0x2E//Soil Moisture(%) 1
#define ITEM_SOILTEMP3          0x2F//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE3      0x30//Soil Moisture(%) 1
#define ITEM_SOILTEMP4          0x31//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE4      0x32//Soil Moisture(%) 1
#define ITEM_SOILTEMP5          0x33//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE5      0x34//Soil Moisture(%) 1
#define ITEM_SOILTEMP6          0x35//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE6      0x36//Soil Moisture(%) 1
#define ITEM_SOILTEMP7          0x37//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE7      0x38//Soil Moisture(%) 1
#define ITEM_SOILTEMP8          0x39//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE8      0x3A//Soil Moisture(%) 1
#define ITEM_SOILTEMP9          0x3B//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE9      0x3C//Soil Moisture(%) 1
#define ITEM_SOILTEMP10         0x3D//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE10     0x3E//Soil Moisture(%) 1
#define ITEM_SOILTEMP11         0x3F//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE11     0x40//Soil Moisture(%) 1
#define ITEM_SOILTEMP12         0x41//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE12     0x42//Soil Moisture(%) 1
#define ITEM_SOILTEMP13         0x43//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE13     0x44//Soil Moisture(%) 1
#define ITEM_SOILTEMP14         0x45//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE14     0x46//Soil Moisture(%) 1
#define ITEM_SOILTEMP15         0x47//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE15     0x48//Soil Moisture(%) 1
#define ITEM_SOILTEMP16         0x49//Soil Temperature(℃) 2
#define ITEM_SOILMOISTURE16     0x4A//Soil Moisture(%) 1
#define ITEM_LOWBATT            0x4C//All sensor lowbatt 16 char 16
#define ITEM_PM25_24HAVG1       0x4D//for pm25_ch1 2
#define ITEM_PM25_24HAVG2       0x4E//for pm25_ch2 2
#define ITEM_PM25_24HAVG3       0x4F//for pm25_ch3 2
#define ITEM_PM25_24HAVG4       0x50//for pm25_ch4 2
#define ITEM_PM25_CH2           0x51//PM2.5 Air Quality Sensor(μg/m3) 2
#define ITEM_PM25_CH3           0x52//PM2.5 Air Quality Sensor(μg/m3) 2
#define ITEM_PM25_CH4           0x53//PM2.5 Air Quality Sensor(μg/m3) 2
#define ITEM_LEAK_CH1           0x58//for Leak_ch1 1
#define ITEM_LEAK_CH2           0x59//for Leak_ch2 1
#define ITEM_LEAK_CH3           0x5A//for Leak_ch3 1
#define ITEM_LEAK_CH4           0x5B//for Leak_ch4 1
#define ITEM_LIGHTNING          0x60 // lightning distance （1~40KM） 1
#define ITEM_LIGHTNING_TIME     0x61// lightning happened time(UTC) 4
#define ITEM_LIGHTNING_POWER    0x62// lightning counter for the ay 4
#define ITEM_TF_USR1            0x63//Temperature(℃) 3
#define ITEM_TF_USR2            0x64//Temperature(℃) 3
#define ITEM_TF_USR3            0x65//Temperature(℃) 3
#define ITEM_TF_USR4            0x66//Temperature(℃) 3
#define ITEM_TF_USR5            0x67//Temperature(℃) 3
#define ITEM_TF_USR6            0x68//Temperature(℃) 3
#define ITEM_TF_USR7            0x69//Temperature(℃) 3
#define ITEM_TF_USR8            0x6A//Temperature(℃) 3
//the data packet in this sequence, and it should be followed strictly this data sequence
#define ITEM_SENSOR_CO2         0x70  //16
/* CO2 record
    0 tf_co2 short C x10
    2 humi_co2 unsigned char %
    3 pm10_co2 unsigned short ug/m3 x10
    5 pm10_24h_co2 unsigned short ug/m3 x10
    7 pm25_co2 unsigned short ug/m3 x10
    9 pm25_24h_co2 unsigned short ug/m3 x10
    11 co2 unsigned short ppm
    13 co2_24h unsigned short ppm
    15 co2_batt u8 (0~5)
*/
#define ITEM_PM25_AQI           0x71 //only for amb
//
// ITEM_PM25_AQI length(n*2)(1byte) 1-aqi_pm25 2-aqi_pm25_24h ........ n-aqi
/*
aqi_pm25 aqi_pm25_24h aqi_pm25_in aqi_pm25_in_24h aqi_pm25_aqin aqi_pm25_24h_aqin AQI derived from PM25 int
AQI derived from PM25, 24 hour running average int
AQI derived from PM25 IN int
AQI derived from PM25 IN, 24 hour running average int
AQI derived from PM25, AQIN sensor int
AQI derived from PM25, 24 hour running average, AQIN sensor int
.... n
*/
//
#define ITEM_LEAF_WETNESS_CH1   0x72// 1
#define ITEM_LEAF_WETNESS_CH2   0x73// 1
#define ITEM_LEAF_WETNESS_CH3   0x74// 1
#define ITEM_LEAF_WETNESS_CH4   0x75// 1
#define ITEM_LEAF_WETNESS_CH5   0x76// 1
#define ITEM_LEAF_WETNESS_CH6   0x77// 1
#define ITEM_LEAF_WETNESS_CH7   0x78// 1
#define ITEM_LEAF_WETNESS_CH8   0x79// 1
#define ITEM_Piezo_Rain_Rate    0x80// 2
#define ITEM_Piezo_Event_Rain   0x81// 2
#define ITEM_Piezo_Hourly_Rain  0x82// 2
#define ITEM_Piezo_Daily_Rain   0x83// 4
#define ITEM_Piezo_Weekly_Rain  0x84// 4
#define ITEM_Piezo_Monthly_Rain 0x85// 4
#define ITEM_Piezo_yearly_Rain  0x86// 4
#define ITEM_Piezo_Gain10       0x87// 2*10
#define ITEM_RST_RainTime       0x88// 3

