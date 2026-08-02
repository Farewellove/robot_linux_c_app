#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stdio.h>

FILE *logger_open(const char *dir, const char *prefix);
void  logger_write_car(FILE *fp, double timestamp,
                       float vx, float vy, float vz,
                       int weld_state, float d_angle, float d_dis,
                       float yaw, float roll, float pitch);
void  logger_write_uwb(FILE *fp, double timestamp,
                       int32_t tag_num,
                       float x, float y, float z);
void  logger_close(FILE *fp);
double logger_timestamp_now(void);

#endif
