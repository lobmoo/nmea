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

#include "nmea.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

/**
 * @brief 判断字符能否打印 ',' '*' 除外
 * @param  c               
 * @return true 
 * @return false 
 */
static inline bool minmea_isfield(char c) {
    return isprint((unsigned char) c) && c != ',' && c != '*';
}



/**
 * @brief 进制转化
 * @param  c                
 * @return int 
 */
static int hex2int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/**
 * @brief 按照可变参数放入gps数据
 * @param  sentence        原始数据
 * @param  format          格式
 * @param  ...             c：类型 d：经纬度类型 f:分度 
 * @return true 
 * @return false 
 */
bool minmea_scan(const char *sentence, const char *format, ...)
{
    bool result = false;
    bool optional = false;
    va_list ap;
    va_start(ap, format);

    const char *field = sentence;
#define next_field() \
    do { \
        /* Progress to the next field. */ \
        while (minmea_isfield(*sentence)) \
            sentence++; \
        /* Make sure there is a field there. */ \
        if (*sentence == ',') { \
            sentence++; \
            field = sentence; \
        } else { \
            field = NULL; \
        } \
    } while (0)

    while (*format) {
        char type = *format++;

        if (type == ';') {
            // All further fields are optional.
            optional = true;
            continue;
        }

        if (!field && !optional) {
            // Field requested but we ran out if input. Bail out.
            goto parse_error;
        }

        switch (type) {
            case 'c': { // Single character field (char).
                char value = '\0';

                if (field && minmea_isfield(*field))
                    value = *field;

                *va_arg(ap, char *) = value;
            } break;

            case 'd': { // Single character direction field (int).
                int value = 0;

                if (field && minmea_isfield(*field)) {
                    switch (*field) {
                        case 'N':
                        case 'E':
                            value = 1;
                            break;
                        case 'S':
                        case 'W':
                            value = -1;
                            break;
                        default:
                            goto parse_error;
                    }
                }

                *va_arg(ap, int *) = value;
            } break;

            case 'f': { // Fractional value with scale (struct minmea_float).
                int sign = 0;
                int_least32_t value = -1;
                int_least32_t scale = 0;

                if (field) {
                    while (minmea_isfield(*field)) {
                        if (*field == '+' && !sign && value == -1) {
                            sign = 1;
                        } else if (*field == '-' && !sign && value == -1) {
                            sign = -1;
                        } else if (isdigit((unsigned char) *field)) {
                            int digit = *field - '0';
                            if (value == -1)
                                value = 0;
                            if (value > (INT_LEAST32_MAX-digit) / 10) {
                                /* we ran out of bits, what do we do? */
                                if (scale) {
                                    /* truncate extra precision */
                                    break;
                                } else {
                                    /* integer overflow. bail out. */
                                    goto parse_error;
                                }
                            }
                            value = (10 * value) + digit;
                            if (scale)
                                scale *= 10;
                        } else if (*field == '.' && scale == 0) {
                            scale = 1;
                        } else if (*field == ' ') {
                            /* Allow spaces at the start of the field. Not NMEA
                             * conformant, but some modules do this. */
                            if (sign != 0 || value != -1 || scale != 0)
                                goto parse_error;
                        } else {
                            goto parse_error;
                        }
                        field++;
                    }
                }

                if ((sign || scale) && value == -1)
                    goto parse_error;

                if (value == -1) {
                    /* No digits were scanned. */
                    value = 0;
                    scale = 0;
                } else if (scale == 0) {
                    /* No decimal point. */
                    scale = 1;
                }
                if (sign)
                    value *= sign;

                *va_arg(ap, struct minmea_float *) = (struct minmea_float) {value, scale};
            } break;

            case 'i': { // Integer value, default 0 (int).
                int value = 0;

                if (field) {
                    char *endptr;
                    value = strtol(field, &endptr, 10);
                    if (minmea_isfield(*endptr))
                        goto parse_error;
                }

                *va_arg(ap, int *) = value;
            } break;

            case 's': { // String value (char *).
                char *buf = va_arg(ap, char *);

                if (field) {
                    while (minmea_isfield(*field))
                        *buf++ = *field++;
                }

                *buf = '\0';
            } break;

            case 't': { // NMEA talker+sentence identifier (char *).
                // This field is always mandatory.
                if (!field)
                    goto parse_error;

                if (field[0] != '$')
                    goto parse_error;
                for (int f=0; f<5; f++)
                    if (!minmea_isfield(field[1+f]))
                        goto parse_error;

                char *buf = va_arg(ap, char *);
                memcpy(buf, field+1, 5);
                buf[5] = '\0';
            } break;

            case 'D': { // Date (int, int, int), -1 if empty.
                struct minmea_date *date = va_arg(ap, struct minmea_date *);

                int d = -1, m = -1, y = -1;

                if (field && minmea_isfield(*field)) {
                    // Always six digits.
                    for (int f=0; f<6; f++)
                        if (!isdigit((unsigned char) field[f]))
                            goto parse_error;

                    char dArr[] = {field[0], field[1], '\0'};
                    char mArr[] = {field[2], field[3], '\0'};
                    char yArr[] = {field[4], field[5], '\0'};
                    d = strtol(dArr, NULL, 10);
                    m = strtol(mArr, NULL, 10);
                    y = strtol(yArr, NULL, 10);
                }

                date->day = d;
                date->month = m;
                date->year = y;
            } break;

            case 'T': { // Time (int, int, int, int), -1 if empty.
                struct minmea_time *time_ = va_arg(ap, struct minmea_time *);

                int h = -1, i = -1, s = -1, u = -1;

                if (field && minmea_isfield(*field)) {
                    // Minimum required: integer time.
                    for (int f=0; f<6; f++)
                        if (!isdigit((unsigned char) field[f]))
                            goto parse_error;

                    char hArr[] = {field[0], field[1], '\0'};
                    char iArr[] = {field[2], field[3], '\0'};
                    char sArr[] = {field[4], field[5], '\0'};
                    h = strtol(hArr, NULL, 10);
                    i = strtol(iArr, NULL, 10);
                    s = strtol(sArr, NULL, 10);
                    field += 6;

                    // Extra: fractional time. Saved as microseconds.
                    if (*field++ == '.') {
                        uint32_t value = 0;
                        uint32_t scale = 1000000LU;
                        while (isdigit((unsigned char) *field) && scale > 1) {
                            value = (value * 10) + (*field++ - '0');
                            scale /= 10;
                        }
                        u = value * scale;
                    } else {
                        u = 0;
                    }
                }

                time_->hours = h;
                time_->minutes = i;
                time_->seconds = s;
                time_->microseconds = u;
            } break;

            case '_': { // Ignore the field.
            } break;

            default: { // Unknown.
                goto parse_error;
            }
        }

        next_field();
    }

    result = true;

parse_error:
    va_end(ap);
    return result;
}



static inline int_least32_t minmea_rescale(struct minmea_float *f, int_least32_t new_scale)
{
    if (f->scale == 0)
        return 0;
    if (f->scale == new_scale)
        return f->value;
    if (f->scale > new_scale)
        return (f->value + ((f->value > 0) - (f->value < 0)) * f->scale/new_scale/2) / (f->scale/new_scale);
    else
        return f->value * (new_scale/f->scale);
}

/**
 * Convert a fixed-point value to a floating-point value.
 * Returns NaN for "unknown" values.
 */
static inline float minmea_tofloat(struct minmea_float *f)
{
    if (f->scale == 0)
        return NAN;
    return (float) f->value / (float) f->scale;
}

/**
 * Convert a raw coordinate to a floating point DD.DDD... value.
 * Returns NaN for "unknown" values.
 */
static inline float minmea_tocoord(struct minmea_float *f)
{
    if (f->scale == 0)
        return NAN;
    int_least32_t degrees = f->value / (f->scale * 100);
    int_least32_t minutes = f->value % (f->scale * 100);
    return (float) degrees + (float) minutes / (60 * f->scale);
}


/**
 * @brief 校验数据完整性
 * @param  sentence         原始数据
 * @param  strict           是否为严格模式 该项设置为true后，校验和失败的数据则会直接丢失
 * @return true             false 
 */
bool minmea_check(const char *sentence, bool strict)
{
    uint8_t checksum = 0x00;

     /* Sequence length is limited*/
    if (strlen(sentence) > MINMEA_MAX_LENGTH + 3)
        return false;

    // A valid sentence starts with "$".
    if (*sentence++ != '$')
        return false;

    // The optional checksum is an XOR of all bytes between "$" and "*".
    while (*sentence && *sentence != '*' && isprint((unsigned char) *sentence))
        checksum ^= *sentence++;

    // If checksum is present...
    if (*sentence == '*') {
        // Extract checksum.
        sentence++;
        int upper = hex2int(*sentence++);
        if (upper == -1)
            return false;
        int lower = hex2int(*sentence++);
        if (lower == -1)
            return false;
        int expected = upper << 4 | lower;

        // Check for checksum mismatch.
        if (checksum != expected)
            return false;
    } else if (strict) {
        /*校验和失败的数据则会直接丢失*/   
        return false;
    }
  
    // The only stuff allowed at this point is a newline.
    if (*sentence && strcmp(sentence, "\n") && strcmp(sentence, "\r\n")){
        return false;
        }

    return true;
}


/**
 * @brief 获取解析id
 * @param  sentence       原始数据
 * @param  strict         是否为严格模式 该项设置为true后，校验和失败的数据则会直接丢失，建议false
 * @return enum minmea_sentence_id 
 */
enum minmea_sentence_id minmea_sentence_id(const char *sentence, bool strict)
{
    if (!minmea_check(sentence, strict))
        return MINMEA_INVALID;

    char type[6];
    if (!minmea_scan(sentence, "t", type))
        return MINMEA_INVALID;

    if (!strcmp(type+2, "RMC"))
        return MINMEA_SENTENCE_RMC;
    if (!strcmp(type+2, "GGA"))
        return MINMEA_SENTENCE_GGA;
    if (!strcmp(type+2, "GSA"))
        return MINMEA_SENTENCE_GSA;
    if (!strcmp(type+2, "GSV"))
        return MINMEA_SENTENCE_GSV;
    return MINMEA_UNKNOWN;
}

/**
 * @brief 解析rmc
 * @param  frame            rmc结构
 * @param  sentence         原始数据
 * @return true false
 */
bool minmea_parse_rmc(struct minmea_sentence_rmc *frame, const char *sentence)
{
    // $GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62
    char type[6];
    char validity;
    int latitude_direction;
    int longitude_direction;
    int variation_direction;
    if (!minmea_scan(sentence, "tTcfdfdffDfd",
            type,
            &frame->time,
            &validity,
            &frame->latitude, &latitude_direction,
            &frame->longitude, &longitude_direction,
            &frame->speed,
            &frame->course,
            &frame->date,
            &frame->variation, &variation_direction))
        return false;
    if (strcmp(type+2, "RMC"))
        return false;

    frame->valid = (validity == 'A');
    frame->latitude.value *= latitude_direction;
    frame->longitude.value *= longitude_direction;
    frame->variation.value *= variation_direction;

    return true;
}

/**
 * @brief 解析gga
 * @param  frame           gga结构
 * @param  sentence        原始数据
 * @return true 
 * @return false 
 */
bool minmea_parse_gga(struct minmea_sentence_gga *frame, const char *sentence)
{
    // $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
    char type[6];
    int latitude_direction;
    int longitude_direction;

    if (!minmea_scan(sentence, "tTfdfdiiffcfcf_",
            type,
            &frame->time,
            &frame->latitude, &latitude_direction,
            &frame->longitude, &longitude_direction,
            &frame->fix_quality,
            &frame->satellites_tracked,
            &frame->hdop,
            &frame->altitude, &frame->altitude_units,
            &frame->height, &frame->height_units,
            &frame->dgps_age))
        return false;
    if (strcmp(type+2, "GGA"))
        return false;

    frame->latitude.value *= latitude_direction;
    frame->longitude.value *= longitude_direction;

    return true;
}

/**
 * @brief  解析gsa
 * @param  frame           gsa结构
 * @param  sentence        原始数据
 * @return true 
 * @return false 
 */
bool minmea_parse_gsa(struct minmea_sentence_gsa *frame, const char *sentence)
{
    // $GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39
    char type[6];

    if (!minmea_scan(sentence, "tciiiiiiiiiiiiifff",
            type,
            &frame->mode,
            &frame->fix_type,
            &frame->sats[0],
            &frame->sats[1],
            &frame->sats[2],
            &frame->sats[3],
            &frame->sats[4],
            &frame->sats[5],
            &frame->sats[6],
            &frame->sats[7],
            &frame->sats[8],
            &frame->sats[9],
            &frame->sats[10],
            &frame->sats[11],
            &frame->pdop,
            &frame->hdop,
            &frame->vdop))
        return false;
    if (strcmp(type+2, "GSA"))
        return false;

    return true;
}

/**
 * @brief 解析gsv
 * @param  frame          gsv结构
 * @param  sentence       原始数据
 * @return true 
 * @return false 
 */
bool minmea_parse_gsv(struct minmea_sentence_gsv *frame, const char *sentence)
{
    // $GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74
    char type[6];

    if (!minmea_scan(sentence, "tiii;iiiiiiiiiiiiiiii",
            type,
            &frame->total_msgs,
            &frame->msg_nr,
            &frame->total_sats,
            &frame->sats[0].nr,
            &frame->sats[0].elevation,
            &frame->sats[0].azimuth,
            &frame->sats[0].snr,
            &frame->sats[1].nr,
            &frame->sats[1].elevation,
            &frame->sats[1].azimuth,
            &frame->sats[1].snr,
            &frame->sats[2].nr,
            &frame->sats[2].elevation,
            &frame->sats[2].azimuth,
            &frame->sats[2].snr,
            &frame->sats[3].nr,
            &frame->sats[3].elevation,
            &frame->sats[3].azimuth,
            &frame->sats[3].snr
            )) {
        return false;
    }
    if (strcmp(type+2, "GSV"))
        return false;

    memcpy(frame->type,type,sizeof(type));
    return true;
}



#if 1
/**
 * @brief TEST DEMO 
 * @return int 
 */
int TEST_nmea(char *str)
{
    switch (minmea_sentence_id(str, false)) {
        case MINMEA_SENTENCE_RMC: {
            struct minmea_sentence_rmc frame;
            if (minmea_parse_rmc(&frame, str)) {
                printf("$xxRMC: raw coordinates and speed: (%d/%d,%d/%d) %d/%d\n",
                        frame.latitude.value, frame.latitude.scale,
                        frame.longitude.value, frame.longitude.scale,
                        frame.speed.value, frame.speed.scale);
                printf("$xxRMC fixed-point coordinates and speed scaled to three decimal places: (%d,%d) %d\n",
                        minmea_rescale(&frame.latitude, 1000),
                        minmea_rescale(&frame.longitude, 1000),
                        minmea_rescale(&frame.speed, 1000));
                printf("$xxRMC floating point degree coordinates and speed: (%f,%f) %f\n",
                        minmea_tocoord(&frame.latitude),
                        minmea_tocoord(&frame.longitude),
                        minmea_tofloat(&frame.speed));

            }
            else {
                printf("$xxRMC sentence is not parsed\n");
            }
        } break;
        case MINMEA_SENTENCE_GGA: {
            struct minmea_sentence_gga frame;
            if (minmea_parse_gga(&frame, str)) {
                printf("$xxGGA: fix quality: %d\n", frame.fix_quality);
            }
            else {
                printf("$xxGGA sentence is not parsed\n");
            }
        } break;

        case MINMEA_SENTENCE_GSV: {
            struct minmea_sentence_gsv frame;
            if (minmea_parse_gsv(&frame, str)) {
                printf("$xxGSV: message %d of %d\n", frame.msg_nr, frame.total_msgs);
                printf("$xxGSV: sattelites in view: %d\n", frame.total_sats);
                for (int i = 0; i < 4; i++)
                    printf("$xxGSV: sat nr %d, elevation: %d, azimuth: %d, snr: %d dbm\n",
                        frame.sats[i].nr,
                        frame.sats[i].elevation,
                        frame.sats[i].azimuth,
                        frame.sats[i].snr);
            }
            else {
                printf("$xxGSV sentence is not parsed\n");
            }
        } break;

        case MINMEA_SENTENCE_GSA:
        {
            struct minmea_sentence_gsa frame;
            if (minmea_parse_gsa(&frame, str)){

                 /*保留，若使用可自行添加*/

             }

        }break;

        default: {
            printf("$xxxxx sentence is not parsed\n");
        } break;
    }
    return 0;
}
#endif




#if 1
/**
 * @brief TEST DEMO 
 * @return int 
 */
int main(void)
{
    char* str = "$GNRMC,074733.000,A,3011.29994,N,12012.34471,E,0.00,0.00,210422,,,A*7A";
    switch (minmea_sentence_id(str, false)) {
        case MINMEA_SENTENCE_RMC: {
            struct minmea_sentence_rmc frame;
            if (minmea_parse_rmc(&frame, str)) {
                printf("$xxRMC: raw coordinates and speed: (%d/%d,%d/%d) %d/%d\n",
                        frame.latitude.value, frame.latitude.scale,
                        frame.longitude.value, frame.longitude.scale,
                        frame.speed.value, frame.speed.scale);
                printf("$xxRMC fixed-point coordinates and speed scaled to three decimal places: (%d,%d) %d\n",
                        minmea_rescale(&frame.latitude, 1000),
                        minmea_rescale(&frame.longitude, 1000),
                        minmea_rescale(&frame.speed, 1000));
                printf("$xxRMC floating point degree coordinates and speed: (%f,%f) %f\n",
                        minmea_tocoord(&frame.latitude),
                        minmea_tocoord(&frame.longitude),
                        minmea_tofloat(&frame.speed));

            }
            else {
                printf("$xxRMC sentence is not parsed\n");
            }
        } break;
        case MINMEA_SENTENCE_GGA: {
            struct minmea_sentence_gga frame;
            if (minmea_parse_gga(&frame, str)) {
                printf("$xxGGA: fix quality: %d\n", frame.fix_quality);
            }
            else {
                printf("$xxGGA sentence is not parsed\n");
            }
        } break;

        case MINMEA_SENTENCE_GSV: {
            struct minmea_sentence_gsv frame;
            if (minmea_parse_gsv(&frame, str)) {
                printf("$xxGSV: message %d of %d\n", frame.msg_nr, frame.total_msgs);
                printf("$xxGSV: sattelites in view: %d\n", frame.total_sats);
                printf("$xxGSV: type: %s\n", frame.type);


                for (int i = 0; i < 4; i++)
                    printf("$xxGSV: sat nr %d, elevation: %d, azimuth: %d, snr: %d dbm\n",
                        frame.sats[i].nr,
                        frame.sats[i].elevation,
                        frame.sats[i].azimuth,
                        frame.sats[i].snr);
            }
            else {
                printf("$xxGSV sentence is not parsed\n");
            }
        } break;

        case MINMEA_SENTENCE_GSA:
        {
            struct minmea_sentence_gsa frame;
            if (minmea_parse_gsa(&frame, str)){

                 /*保留，若使用可自行添加*/

             }

        }break;

        default: {
            printf("$xxxxx sentence is not parsed\n");
        } break;
    }
    return 0;
}
#endif