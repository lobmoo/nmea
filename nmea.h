/**
 * @file nema.c
 * @brief 
 * @author wwk (1162431386@qq.com)
 * @version 1.0
 * @date 2022-03-31
 * 
 * @copyright Copyright (c) 2022  Hangzhou Hikvision
 * 
 * @par 修改日志:移植nema
 */
#ifndef NMEA_H
#define NMEA_H


#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <math.h>


#define MINMEA_MAX_LENGTH 80      /*nema1803标准长度限制，请勿更改*/


enum minmea_sentence_id {
    MINMEA_INVALID = -1,   /*无效*/
    MINMEA_UNKNOWN = 0,    /*未知*/
    MINMEA_SENTENCE_RMC,
    MINMEA_SENTENCE_GGA,
    MINMEA_SENTENCE_GSA,
    MINMEA_SENTENCE_GSV,
};



struct minmea_float {
    int_least32_t value;
    int_least32_t scale;
};

struct minmea_date {
    int day;
    int month;
    int year;
};

struct minmea_time {
    int hours;
    int minutes;
    int seconds;
    int microseconds;
};



struct minmea_sat_info {
    int nr;
    int elevation;
    int azimuth;
    int snr;
};


enum minmea_gll_status {
    MINMEA_GLL_STATUS_DATA_VALID = 'A',
    MINMEA_GLL_STATUS_DATA_NOT_VALID = 'V',
};


/**
 * @brief rmc数据结构
 */
struct minmea_sentence_rmc {
    struct minmea_time time;
    bool valid;
    struct minmea_float latitude;
    struct minmea_float longitude;
    struct minmea_float speed;
    struct minmea_float course;
    struct minmea_date date;
    struct minmea_float variation;
};

/**
 * @brief gga数据结构
 */
struct minmea_sentence_gga {
    struct minmea_time time;
    struct minmea_float latitude;
    struct minmea_float longitude;
    int fix_quality;
    int satellites_tracked;
    struct minmea_float hdop;
    struct minmea_float altitude; char altitude_units;
    struct minmea_float height; char height_units;
    struct minmea_float dgps_age;
};

/**
 * @brief gsa数据结构
 */
struct minmea_sentence_gsa {
    char mode;
    int fix_type;
    int sats[12];
    struct minmea_float pdop;
    struct minmea_float hdop;
    struct minmea_float vdop;
};

/**
 * @brief gsv数据结构
 */
struct minmea_sentence_gsv {
    char type[6];    /*类型*/
    int total_msgs;
    int msg_nr;
    int total_sats;
    struct minmea_sat_info sats[4];
};



int TEST_nmea(char *str);

#ifdef __cplusplus
}
#endif

#endif 