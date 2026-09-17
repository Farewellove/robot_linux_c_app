#include "robot_protocol.h"
#include <string.h>
#include <stdio.h>

/*
 * 机器人通信协议实现（20 字节定长帧，二进制字段小端存放）
 *
 *   下行 控制帧：0x77 开头，0x0A 结尾，见 build_control_frame()
 *   上行 状态帧：0x55 0x56 结尾，buf[0] 为 'u'(UWB) 或 'c'(车体状态)
 *
 * 上行的两种帧长度相同、帧头不同，因此统一用「帧尾魔数」做边界判定，
 * 由 recv_buffer_feed() 的滑动窗口状态机完成 TCP 粘包/半包处理。
 * 二进制字段一律 memcpy 搬运（不保证 4 字节对齐），且未做字节序转换
 * —— 假定设备端与本机同为小端。
 */

void recv_buffer_init(RecvBuffer *rb)
{
    memset(rb->buf, 0, sizeof(rb->buf));
    rb->wr = 0;
}

/* 校验帧尾魔数 {0x55, 0x56}；长度不足视为非法 */
static int is_valid_status_frame(const uint8_t *buf, size_t len)
{
    if (len < STATUS_FRAME_LEN) return 0;
    return buf[18] == 0x55 && buf[19] == 0x56;
}

/*
 * 状态帧接收：基于帧尾魔数的滑动窗口粘包状态机。
 *
 * TCP 是字节流，recv 一次可能收到半帧、多帧或帧的错位片段，需要自己找帧边界。
 * 这里的做法是逐字节填缓冲区，凑满 20 字节后只看帧尾两位：
 *
 *   帧尾 == {0x55, 0x56} → 一帧到齐，按 buf[0] 分派解析并回调上报，缓冲清零，
 *                          从下一个字节开始重新找下一帧；
 *   帧尾 不匹配         → 说明当前这 20 字节里混进了前导垃圾（或帧还没对齐），
 *                          丢弃最早的 1 个字节（memmove 左移一位，wr 退到 19），
 *                          等新字节补满再判。最坏情况下丢 19 字节即可重新对齐。
 *
 * 为什么用帧尾而不是帧头同步：UWB 帧帧头是 'u'、车体状态帧帧头是 'c'，
 * 帧头不固定；而帧尾两位对两种帧统一，用它可以不区分帧类型就找到边界。
 */
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

                /* buf[0] 只是一级分派，parse_xxx_frame() 内部还会做二级校验 */
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
                /* 注意：帧尾合法但帧头既非 'u' 也非 'c' 时，这 20 字节被静默丢弃 */

                rb->wr = 0;
            } else {
                /* 未对齐：左移一位丢弃最早字节，wr 保持 19 以便下一个字节正好补满 */
                memmove(rb->buf, rb->buf + 1, STATUS_FRAME_LEN - 1);
                rb->wr = STATUS_FRAME_LEN - 1;
            }
        }
    }
    return 0;
}

/*
 * 构建下行控制帧（20 字节定长，发送线程周期性调用）：
 *
 *   [0]      0x77        帧头
 *   [1]      0x00        保留
 *   [2]      enable      使能位（0 失能 / 1 使能）
 *   [3]      mode        模式（0=STOP / 1=MANUAL / 2=AUTO）
 *   [4]      0x00        保留
 *   [5..7]   vx, vy, vz  int8 速度分量
 *   [8..17]  0x00        保留
 *   [18]     seq         自增序列号，便于对端判丢包/乱序
 *   [19]     0x0A        帧尾
 *
 * 返回 0 成功；-1 表示输出缓冲区不足（不写任何数据）。
 */
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

/*
 * 解析 UWB 定位帧：
 *
 *   [0..1]   'u', 'p'    帧标识（两级校验：这里再核对一次）
 *   [2..5]   tag_num     int32  标签号
 *   [6..9]   x           float
 *   [10..13] y           float
 *   [14..17] z           float
 *
 * 设备端按本机字节序打包，故直接 memcpy 到目标字段，不做字节序转换。
 */
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

/*
 * 解析车体状态帧（数值按定点数缩放还原为物理量）：
 *
 *   [0]      'c'          帧标识
 *   [1..3]   vx,vy,vz     int8，放大 100 倍  → 实际值 = raw / 100.0
 *   [4..5]   weld_state   int16 原始值，不做缩放
 *   [6..7]   d_angle      int16，放大 100 倍  → raw / 100.0（度）
 *   [8..11]  d_dis        float 原始值，不做缩放
 *   [12..13] yaw          int16 归一化角      → raw / 32768.0 * 180（度，±180°）
 *   [14..15] roll         同上
 *   [16..17] pitch        同上
 *
 * int16 满量程对应 ±180°，故除以 32768 再乘 180；int8/int16 小端字段
 * 同样用 memcpy 取出，避免未对齐访问。
 */
int parse_car_frame(const uint8_t *buf, size_t len, RobotState *out)
{
    if (len < STATUS_FRAME_LEN) return -1;
    if (buf[0] != 'c') return -1;

    /* 速度：int8 定点，放大 100 倍 */
    int8_t vx_raw = (int8_t)buf[1];
    int8_t vy_raw = (int8_t)buf[2];
    int8_t vz_raw = (int8_t)buf[3];

    out->vx = vx_raw / 100.0f;
    out->vy = vy_raw / 100.0f;
    out->vz = vz_raw / 100.0f;

    /* 焊接状态：枚举/标志位语义，保持原值 */
    int16_t weld_raw;
    memcpy(&weld_raw, buf + 4, 2);
    out->weld_state = weld_raw;

    /* 角度增量：int16 定点，放大 100 倍 */
    int16_t d_angle_raw;
    memcpy(&d_angle_raw, buf + 6, 2);
    out->d_angle = d_angle_raw / 100.0f;

    /* 距离增量：本来就是 float，直接搬 4 字节 */
    memcpy(&out->d_dis, buf + 8, 4);

    /* 姿态角：int16 归一化，满量程 ±180° */
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

/* 控制模式枚举 → 可读字符串，用于日志/控制台打印 */
const char *mode_name(int mode)
{
    switch (mode) {
    case 0: return "STOP";
    case 1: return "MANUAL";
    case 2: return "AUTO";
    default: return "UNKNOWN";
    }
}
