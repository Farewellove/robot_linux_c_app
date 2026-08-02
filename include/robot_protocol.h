#ifndef ROBOT_PROTOCOL_H
#define ROBOT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define CONTROL_FRAME_LEN  20
#define STATUS_FRAME_LEN   20

typedef struct {
    int32_t tag_num;
    float   x;
    float   y;
    float   z;
} UwbState;

typedef struct {
    float vx;
    float vy;
    float vz;
    int16_t weld_state;
    float d_angle;
    float d_dis;
    float yaw;
    float roll;
    float pitch;
} RobotState;

typedef enum {
    FRAME_TYPE_NONE = 0,
    FRAME_TYPE_UWB,
    FRAME_TYPE_CAR,
} FrameType;

typedef struct {
    uint8_t buf[STATUS_FRAME_LEN];
    size_t  wr;
} RecvBuffer;

typedef struct {
    FrameType type;
    union {
        UwbState   uwb;
        RobotState car;
    } data;
} ParsedFrame;

void recv_buffer_init(RecvBuffer *rb);
int  recv_buffer_feed(RecvBuffer *rb, const uint8_t *data, size_t len,
                      void (*on_frame)(const ParsedFrame *frame));

int  build_control_frame(uint8_t *out, size_t out_len,
                         uint8_t enable, uint8_t mode,
                         int8_t vx, int8_t vy, int8_t vz,
                         uint8_t seq);

int  parse_uwb_frame(const uint8_t *buf, size_t len, UwbState *out);
int  parse_car_frame(const uint8_t *buf, size_t len, RobotState *out);

const char *mode_name(int mode);

#endif
