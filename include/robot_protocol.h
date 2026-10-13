#ifndef ROBOT_PROTOCOL_H
#define ROBOT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* 上下行帧长度均为 20 字节定长 */
#define CONTROL_FRAME_LEN  20
#define STATUS_FRAME_LEN   20

/* UWB 定位帧解析结果（帧头 'u' 'p'） */
typedef struct {
    int32_t tag_num;   /* 标签号 */
    float   x;         /* 定位坐标 x */
    float   y;         /* 定位坐标 y */
    float   z;         /* 定位坐标 z */
} UwbState;

/* 车体状态帧解析结果，定点字段已在解析时还原为物理量 */
typedef struct {
    float   vx;         /* 速度分量（raw / 100） */
    float   vy;
    float   vz;
    int16_t weld_state; /* 焊接状态原始值 */
    float   d_angle;    /* 角度增量（raw / 100，度） */
    float   d_dis;      /* 距离增量 */
    float   yaw;        /* 姿态角（raw / 32768 * 180，度） */
    float   roll;
    float   pitch;
} RobotState;

/*
 * ICM20608 六轴 + 温度帧解析结果（真实传感器，非模拟数据）。
 * 线上只传 int16 原始值，物理量在解析侧换算，换算系数即协议的一部分：
 *   accel  raw / 2048.0            → g    （±16g 量程）
 *   gyro   raw / 16.4              → °/s  （±2000°/s 量程）
 *   temp   raw / 326.8 + 25.0      → ℃
 * 这些系数与板端 IIO 驱动（icm20608.ko）的量程配置一致。
 */
#define ICM_ACCEL_LSB_PER_G   2048.0f
#define ICM_GYRO_LSB_PER_DPS  16.4f
#define ICM_TEMP_LSB_PER_C    326.8f
#define ICM_TEMP_OFFSET_C     25.0f

/* IMU 原始值数组下标：ax, ay, az, gx, gy, gz, temp */
#define ICM_RAW_LEN 7

typedef struct {
    float    accel_g[3];    /* 加速度 x/y/z，单位 g */
    float    gyro_dps[3];   /* 角速度 x/y/z，单位 °/s */
    float    temp_c;        /* 片内温度，单位 ℃ */
    uint16_t seq;           /* 帧序号（uint16，溢出回绕） */
} ImuState;

/*
 * AP3216C 环境光/接近/红外帧解析结果。
 * 板端驱动当前 scale 恒为 1（见 ap3216c.c 的 IIO_CHAN_INFO_SCALE 分支），
 * 因此帧里直接放原始计数，由上层按需换算，不在协议里写死系数。
 */
typedef struct {
    int16_t  als;           /* 环境光原始计数 */
    int16_t  ps;            /* 接近检测原始计数 */
    int16_t  ir;            /* 红外原始计数 */
    uint16_t seq;           /* 帧序号 */
} ApState;

/* 回调上报的帧类型，与 ParsedFrame.data 的联合体成员一一对应 */
typedef enum {
    FRAME_TYPE_NONE = 0,
    FRAME_TYPE_UWB,
    FRAME_TYPE_CAR,
    FRAME_TYPE_IMU,
    FRAME_TYPE_AP,
} FrameType;

/*
 * 粘包状态机缓冲：buf 恰好一帧大小，wr 为待判字节数。
 * wr 达到 STATUS_FRAME_LEN 时触发一次校验，随后或被清零或被复位为 19。
 */
typedef struct {
    uint8_t buf[STATUS_FRAME_LEN];
    size_t  wr;
} RecvBuffer;

/* 一帧解析结果：type 决定 data 联合体中哪个成员有效 */
typedef struct {
    FrameType type;
    union {
        UwbState   uwb;
        RobotState car;
        ImuState   imu;
        ApState    ap;
    } data;
} ParsedFrame;

/* 清空接收缓冲，准备接收新数据 */
void recv_buffer_init(RecvBuffer *rb);

/*
 * 喂入任意长度字节流（通常来自一次 recv），内部按滑动窗口找帧边界；
 * 每解析出一帧就调用一次 on_frame 回调（同步回调，勿在回调里阻塞）。
 * 返回 0 表示已处理完本次输入（不含"解析出几帧"的语义）。
 */
int  recv_buffer_feed(RecvBuffer *rb, const uint8_t *data, size_t len,
                      void (*on_frame)(const ParsedFrame *frame));

/*
 * 组包下行控制帧到 out（至少 CONTROL_FRAME_LEN 字节）；
 * enable 使能位，mode 见 mode_name()，vx/vy/vz 为 int8 速度，seq 自增序列号。
 * 成功返回 0，缓冲区不足返回 -1。
 */
int  build_control_frame(uint8_t *out, size_t out_len,
                         uint8_t enable, uint8_t mode,
                         int8_t vx, int8_t vy, int8_t vz,
                         uint8_t seq);

/* 解析单帧：失败（长度不足/帧头不符）返回 -1，成功返回 0 */
int  parse_uwb_frame(const uint8_t *buf, size_t len, UwbState *out);
int  parse_car_frame(const uint8_t *buf, size_t len, RobotState *out);
int  parse_imu_frame(const uint8_t *buf, size_t len, ImuState *out);
int  parse_ap_frame(const uint8_t *buf, size_t len, ApState *out);

/*
 * 组包上行状态帧（板端真实传感器数据源用，与解析函数一一对应）：
 *   build_imu_frame：raw[7] = accel xyz / gyro xyz / temp，均为 int16 原始值
 *   build_ap_frame ：AP3216C 的 als / ps / ir 原始计数
 * seq 为 uint16 帧序号，帧尾统一 0x55 0x56。
 * 成功返回 0，输出缓冲区不足返回 -1。
 */
int  build_imu_frame(uint8_t *out, size_t out_len, const int16_t *raw, uint16_t seq);
int  build_ap_frame(uint8_t *out, size_t out_len,
                    int16_t als, int16_t ps, int16_t ir, uint16_t seq);

/* IMU int16 原始值 → 物理量（换算系数见文件上方宏定义） */
void imu_raw_to_physical(const int16_t *raw, ImuState *out);

/* 控制模式名（0=STOP / 1=MANUAL / 2=AUTO），未知返回 "UNKNOWN" */
const char *mode_name(int mode);

#endif
