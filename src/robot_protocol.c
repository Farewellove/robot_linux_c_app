#include "robot_protocol.h"
#include <string.h>
#include <stdio.h>

void recv_buffer_init(RecvBuffer *rb)
{
    memset(rb->buf, 0, sizeof(rb->buf));
    rb->wr = 0;
}

static int is_valid_status_frame(const uint8_t *buf, size_t len)
{
    if (len < STATUS_FRAME_LEN) return 0;
    return buf[18] == 0x55 && buf[19] == 0x56;
}

int recv_buffer_feed(RecvBuffer *rb, const uint8_t *data, size_t len,
                     void (*on_frame)(const ParsedFrame *frame))
{
    for (size_t i = 0; i < len; i++) {
        rb->buf[rb->wr] = data[i];
        rb->wr++;

        if (rb->wr >= STATUS_FRAME_LEN) {
            if (is_valid_status_frame(rb->buf, STATUS_FRAME_LEN)) {
                ParsedFrame pf;
                memset(&pf, 0, sizeof(pf));

                if (rb->buf[0] == 'u') {
                    if (parse_uwb_frame(rb->buf, STATUS_FRAME_LEN, &pf.data.uwb) == 0) {
                        pf.type = FRAME_TYPE_UWB;
                        on_frame(&pf);
                    }
                } else if (rb->buf[0] == 'c') {
                    if (parse_car_frame(rb->buf, STATUS_FRAME_LEN, &pf.data.car) == 0) {
                        pf.type = FRAME_TYPE_CAR;
                        on_frame(&pf);
                    }
                }

                rb->wr = 0;
            } else {
                memmove(rb->buf, rb->buf + 1, STATUS_FRAME_LEN - 1);
                rb->wr = STATUS_FRAME_LEN - 1;
            }
        }
    }
    return 0;
}

int build_control_frame(uint8_t *out, size_t out_len,
                        uint8_t enable, uint8_t mode,
                        int8_t vx, int8_t vy, int8_t vz,
                        uint8_t seq)
{
    if (out_len < CONTROL_FRAME_LEN) return -1;

    memset(out, 0, CONTROL_FRAME_LEN);
    out[0]  = 0x77;
    out[1]  = 0x00;
    out[2]  = enable;
    out[3]  = mode;
    out[4]  = 0x00;
    out[5]  = (uint8_t)vx;
    out[6]  = (uint8_t)vy;
    out[7]  = (uint8_t)vz;
    out[18] = seq;
    out[19] = 0x0A;

    return 0;
}

int parse_uwb_frame(const uint8_t *buf, size_t len, UwbState *out)
{
    if (len < STATUS_FRAME_LEN) return -1;
    if (buf[0] != 'u' || buf[1] != 'p') return -1;

    memcpy(&out->tag_num, buf + 2,  4);
    memcpy(&out->x,       buf + 6,  4);
    memcpy(&out->y,       buf + 10, 4);
    memcpy(&out->z,       buf + 14, 4);

    return 0;
}

int parse_car_frame(const uint8_t *buf, size_t len, RobotState *out)
{
    if (len < STATUS_FRAME_LEN) return -1;
    if (buf[0] != 'c') return -1;

    int8_t vx_raw = (int8_t)buf[1];
    int8_t vy_raw = (int8_t)buf[2];
    int8_t vz_raw = (int8_t)buf[3];

    out->vx = vx_raw / 100.0f;
    out->vy = vy_raw / 100.0f;
    out->vz = vz_raw / 100.0f;

    int16_t weld_raw;
    memcpy(&weld_raw, buf + 4, 2);
    out->weld_state = weld_raw;

    int16_t d_angle_raw;
    memcpy(&d_angle_raw, buf + 6, 2);
    out->d_angle = d_angle_raw / 100.0f;

    memcpy(&out->d_dis, buf + 8, 4);

    int16_t yaw_raw;
    memcpy(&yaw_raw, buf + 12, 2);
    out->yaw = yaw_raw / 32768.0f * 180.0f;

    int16_t roll_raw;
    memcpy(&roll_raw, buf + 14, 2);
    out->roll = roll_raw / 32768.0f * 180.0f;

    int16_t pitch_raw;
    memcpy(&pitch_raw, buf + 16, 2);
    out->pitch = pitch_raw / 32768.0f * 180.0f;

    return 0;
}

const char *mode_name(int mode)
{
    switch (mode) {
    case 0: return "STOP";
    case 1: return "MANUAL";
    case 2: return "AUTO";
    default: return "UNKNOWN";
    }
}
