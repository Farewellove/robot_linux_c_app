/*
 * 协议解析状态机自动化测试（纯解析器单元测试，不涉及 socket）。
 *
 * 关键原则：直接调用生产代码 src/robot_protocol.c 里的
 *   recv_buffer_feed() / parse_uwb_frame() / parse_car_frame() / build_control_frame()
 * 绝不复制一份"测试用解析器"，否则测的就不是真正上线的代码。
 *
 * 覆盖场景见 test_plan.md；每个用例的结果都写入 CSV，便于报告引用：
 *   case_id,case_name,input_bytes,expected_frames,parsed_frames,passed,seed,notes
 *
 * 退出码：全部通过返回 0，否则返回 1（CI 可直接判定）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "robot_protocol.h"

/* ------------------------------------------------------------------ */
/* 测试框架：极简，零外部依赖                                          */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;
static FILE *g_csv = NULL;

typedef struct {
    ParsedFrame frames[60000];
    size_t      count;
} FrameList;

static FrameList g_list;

static void collect_cb(const ParsedFrame *frame)
{
    if (g_list.count < (sizeof(g_list.frames) / sizeof(g_list.frames[0]))) {
        g_list.frames[g_list.count++] = *frame;
    }
}

/* 清空收集器并喂入一段字节流，返回解析出的帧数 */
static size_t feed(const uint8_t *data, size_t len)
{
    RecvBuffer rb;
    recv_buffer_init(&rb);
    g_list.count = 0;
    recv_buffer_feed(&rb, data, len, collect_cb);
    return g_list.count;
}

/* 沿用同一个状态机实例连续喂入（用于半包场景） */
static size_t feed_more(RecvBuffer *rb, const uint8_t *data, size_t len)
{
    g_list.count = 0;
    recv_buffer_feed(rb, data, len, collect_cb);
    return g_list.count;
}

static void record(const char *id, const char *name, size_t in_bytes,
                   size_t expected, size_t got, int ok, unsigned seed,
                   const char *notes)
{
    if (ok) g_pass++; else g_fail++;

    printf("[%s] %-46s in=%zu expected=%zu got=%zu : %s\n",
           ok ? "PASS" : "FAIL", name, in_bytes, expected, got,
           ok ? "ok" : "MISMATCH");

    if (g_csv) {
        fprintf(g_csv, "%s,%s,%zu,%zu,%zu,%s,%u,%s\n",
                id, name, in_bytes, expected, got,
                ok ? "PASS" : "FAIL", seed, notes);
    }
}

/* ------------------------------------------------------------------ */
/* 帧构造工具：与 robot_protocol.c 的字段布局严格对应                  */
/* ------------------------------------------------------------------ */

static void put_i32(uint8_t *p, int32_t v)  { memcpy(p, &v, 4); }
static void put_f32(uint8_t *p, float v)    { memcpy(p, &v, 4); }
static void put_i16(uint8_t *p, int16_t v)  { memcpy(p, &v, 2); }

/* UWB 帧：'u' 'p' tagNum(int32) x y z (float) ... 0x55 0x56 */
static void make_uwb(uint8_t *b, int32_t tag, float x, float y, float z)
{
    memset(b, 0, STATUS_FRAME_LEN);
    b[0] = 'u';
    b[1] = 'p';
    put_i32(b + 2,  tag);
    put_f32(b + 6,  x);
    put_f32(b + 10, y);
    put_f32(b + 14, z);
    b[18] = 0x55;
    b[19] = 0x56;
}

/* 车体状态帧：'c' vx vy vz(int8) weld(int16) dAngle(int16) dDis(float)
              yaw roll pitch(int16) ... 0x55 0x56 */
static void make_car(uint8_t *b, int8_t vx, int8_t vy, int8_t vz,
                     int16_t weld, int16_t dangle, float ddis,
                     int16_t yaw, int16_t roll, int16_t pitch)
{
    memset(b, 0, STATUS_FRAME_LEN);
    b[0] = 'c';
    b[1] = (uint8_t)vx;
    b[2] = (uint8_t)vy;
    b[3] = (uint8_t)vz;
    put_i16(b + 4,  weld);
    put_i16(b + 6,  dangle);
    put_f32(b + 8,  ddis);
    put_i16(b + 12, yaw);
    put_i16(b + 14, roll);
    put_i16(b + 16, pitch);
    b[18] = 0x55;
    b[19] = 0x56;
}

static int feq(float a, float b) { return fabsf(a - b) < 1e-5f; }

/* ------------------------------------------------------------------ */
/* 用例                                                                */
/* ------------------------------------------------------------------ */

/* T01 单帧完整到达 */
static void t01_single_uwb(void)
{
    uint8_t f[STATUS_FRAME_LEN];
    make_uwb(f, 7, 1.25f, -2.5f, 3.75f);

    size_t n = feed(f, sizeof(f));
    int ok = (n == 1) &&
             g_list.frames[0].type == FRAME_TYPE_UWB &&
             g_list.frames[0].data.uwb.tag_num == 7 &&
             feq(g_list.frames[0].data.uwb.x, 1.25f) &&
             feq(g_list.frames[0].data.uwb.y, -2.5f) &&
             feq(g_list.frames[0].data.uwb.z, 3.75f);
    record("T01", "single_complete_uwb_frame", sizeof(f), 1, n, ok, 0,
           "single 20B UWB frame, field values verified");
}

/* T02 单帧完整到达 + 定点缩放正确性 */
static void t02_single_car(void)
{
    uint8_t f[STATUS_FRAME_LEN];
    /* vx=50 -> 0.50 ; dAngle=250 -> 2.50 ; yaw=16384 -> 90.0 ; roll=-16384 -> -90.0 */
    make_car(f, 50, -100, 127, 3, 250, 12.5f, 16384, -16384, 0);

    size_t n = feed(f, sizeof(f));
    const RobotState *c = &g_list.frames[0].data.car;
    int ok = (n == 1) &&
             g_list.frames[0].type == FRAME_TYPE_CAR &&
             feq(c->vx, 0.50f) && feq(c->vy, -1.00f) && feq(c->vz, 1.27f) &&
             c->weld_state == 3 &&
             feq(c->d_angle, 2.50f) &&
             feq(c->d_dis, 12.5f) &&
             feq(c->yaw, 90.0f) && feq(c->roll, -90.0f) && feq(c->pitch, 0.0f);
    record("T02", "single_complete_car_frame_scaling", sizeof(f), 1, n, ok, 0,
           "fixed-point scaling /100, /32768*180 verified");
}

/* T03 粘包：两帧同次输入 */
static void t03_sticky_two(void)
{
    uint8_t buf[STATUS_FRAME_LEN * 2];
    make_uwb(buf, 11, 0.5f, 1.5f, 2.5f);
    make_car(buf + STATUS_FRAME_LEN, 10, 20, 30, 1, 100, 1.0f, 0, 0, 0);

    size_t n = feed(buf, sizeof(buf));
    int ok = (n == 2) &&
             g_list.frames[0].type == FRAME_TYPE_UWB &&
             g_list.frames[0].data.uwb.tag_num == 11 &&
             g_list.frames[1].type == FRAME_TYPE_CAR &&
             g_list.frames[1].data.car.weld_state == 1;
    record("T03", "sticky_two_frames_one_input", sizeof(buf), 2, n, ok, 0,
           "order preserved: UWB then CAR");
}

/* T04 半包：单帧切成两段 */
static void t04_split_frame(void)
{
    uint8_t f[STATUS_FRAME_LEN];
    make_uwb(f, 42, 9.0f, 8.0f, 7.0f);

    RecvBuffer rb;
    recv_buffer_init(&rb);
    g_list.count = 0;

    size_t n1 = feed_more(&rb, f, 7);                 /* 前 7 字节 */
    size_t n2 = feed_more(&rb, f + 7, 13);            /* 后 13 字节 */

    int ok = (n1 == 0) && (n2 == 1) &&
             g_list.frames[0].type == FRAME_TYPE_UWB &&
             g_list.frames[0].data.uwb.tag_num == 42;
    record("T04", "half_packet_split_7_plus_13", sizeof(f), 1, n2, ok, 0,
           "no early emit, correct frame after completion");
}

/* T05 随机大小分块（固定随机种子） */
static void t05_random_chunking(void)
{
    enum { N_FRAMES = 10000 };
    const unsigned seed = 20261011u;

    uint8_t *stream = (uint8_t *)malloc((size_t)N_FRAMES * STATUS_FRAME_LEN);
    uint8_t *expect_type = (uint8_t *)malloc(N_FRAMES);
    int32_t *expect_tag = (int32_t *)malloc(sizeof(int32_t) * N_FRAMES);
    if (!stream || !expect_type || !expect_tag) {
        record("T05", "random_chunking_10000_frames", 0, N_FRAMES, 0, 0, seed,
               "SKIPPED: allocation failed");
        free(stream); free(expect_type); free(expect_tag);
        return;
    }

    for (int i = 0; i < N_FRAMES; i++) {
        uint8_t *p = stream + (size_t)i * STATUS_FRAME_LEN;
        if (i % 2 == 0) {
            make_uwb(p, i, (float)i * 0.5f, (float)i * -0.25f, 1.0f);
            expect_type[i] = FRAME_TYPE_UWB;
            expect_tag[i] = i;
        } else {
            make_car(p, (int8_t)(i % 127), (int8_t)(-(i % 100)), 0,
                     (int16_t)(i % 1000), (int16_t)(i % 200), (float)i,
                     (int16_t)(i % 1000), 0, 0);
            expect_type[i] = FRAME_TYPE_CAR;
            expect_tag[i] = i % 1000;   /* 复用为 weld_state 期望值 */
        }
    }

    /* 用固定种子生成 1..17 字节的随机分块，模拟 TCP 任意切分 */
    srand(seed);
    RecvBuffer rb;
    recv_buffer_init(&rb);
    g_list.count = 0;

    size_t off = 0;
    size_t total = (size_t)N_FRAMES * STATUS_FRAME_LEN;
    while (off < total) {
        size_t chunk = (size_t)(rand() % 17) + 1;
        if (chunk > total - off) chunk = total - off;
        recv_buffer_feed(&rb, stream + off, chunk, collect_cb);
        off += chunk;
    }

    int ok = (g_list.count == (size_t)N_FRAMES);
    if (ok) {
        for (int i = 0; i < N_FRAMES; i++) {
            const ParsedFrame *f = &g_list.frames[i];
            if (f->type != expect_type[i]) { ok = 0; break; }
            if (f->type == FRAME_TYPE_UWB) {
                if (f->data.uwb.tag_num != expect_tag[i]) { ok = 0; break; }
            } else {
                if (f->data.car.weld_state != (int16_t)expect_tag[i]) { ok = 0; break; }
            }
        }
    }

    record("T05", "random_chunking_10000_frames", total, N_FRAMES, g_list.count,
           ok, seed, "fixed seed, chunk size 1..17B, content verified per frame");

    free(stream);
    free(expect_type);
    free(expect_tag);
}

/* T06 垃圾前缀 */
static void t06_garbage_prefix(void)
{
    uint8_t buf[13 + STATUS_FRAME_LEN];
    memset(buf, 0x00, 13);                 /* 0x00 垃圾，不含 0x55/0x56 */
    make_uwb(buf + 13, 5, 1.0f, 2.0f, 3.0f);

    size_t n = feed(buf, sizeof(buf));
    int ok = (n == 1) && g_list.frames[0].data.uwb.tag_num == 5;
    record("T06", "garbage_prefix_13_bytes", sizeof(buf), 1, n, ok, 0,
           "zero-prefix then valid frame, re-sync OK");

    /* 再来一组 0xFF 前缀 */
    uint8_t buf2[9 + STATUS_FRAME_LEN];
    memset(buf2, 0xFF, 9);
    make_uwb(buf2 + 9, 6, 1.0f, 2.0f, 3.0f);
    size_t n2 = feed(buf2, sizeof(buf2));
    int ok2 = (n2 == 1) && g_list.frames[0].data.uwb.tag_num == 6;
    record("T06b", "garbage_prefix_9x_ff", sizeof(buf2), 1, n2, ok2, 0,
           "0xFF prefix then valid frame, re-sync OK");
}

/* T07 错误帧尾 + 后续恢复 */
static void t07_bad_tail(void)
{
    uint8_t buf[STATUS_FRAME_LEN + STATUS_FRAME_LEN];
    memset(buf, 0x11, STATUS_FRAME_LEN);   /* 完整 20B 但帧尾错误 */
    buf[0] = 'c';
    make_uwb(buf + STATUS_FRAME_LEN, 77, 0.0f, 0.0f, 0.0f);

    size_t n = feed(buf, sizeof(buf));
    int ok = (n == 1) &&
             g_list.frames[0].type == FRAME_TYPE_UWB &&
             g_list.frames[0].data.uwb.tag_num == 77;
    record("T07", "bad_tail_then_recover", sizeof(buf), 1, n, ok, 0,
           "invalid tail dropped, following valid frame parsed");
}

/* T08 帧尾合法但帧头未知 -> 按当前实现静默丢弃 20 字节（记录真实行为） */
static void t08_unknown_head(void)
{
    uint8_t buf[STATUS_FRAME_LEN + STATUS_FRAME_LEN];
    memset(buf, 0x22, STATUS_FRAME_LEN);
    buf[0] = 'x';                          /* 既非 'u' 也非 'c' */
    buf[18] = 0x55;
    buf[19] = 0x56;
    make_uwb(buf + STATUS_FRAME_LEN, 88, 0.0f, 0.0f, 0.0f);

    size_t n = feed(buf, sizeof(buf));
    int ok = (n == 1) && g_list.frames[0].data.uwb.tag_num == 88;
    record("T08", "valid_tail_unknown_head_dropped", sizeof(buf), 1, n, ok, 0,
           "DOCUMENTED: valid tail + unknown head => 20B silently dropped, next frame OK");
}

/* T09 超长垃圾输入 + 缓冲区边界 */
static void t09_flood(void)
{
    enum { GARBAGE = 4096 };
    uint8_t *buf = (uint8_t *)malloc(GARBAGE + STATUS_FRAME_LEN);
    if (!buf) {
        record("T09", "garbage_flood_4096_then_frame", 0, 1, 0, 0, 0, "SKIPPED: alloc failed");
        return;
    }
    memset(buf, 0xAA, GARBAGE);
    make_uwb(buf + GARBAGE, 99, 4.0f, 5.0f, 6.0f);

    size_t n = feed(buf, GARBAGE + STATUS_FRAME_LEN);
    int ok = (n == 1) && g_list.frames[0].data.uwb.tag_num == 99;
    record("T09", "garbage_flood_4096_then_frame", GARBAGE + STATUS_FRAME_LEN,
           1, n, ok, 0, "no overflow, exactly one frame emitted");
    free(buf);
}

/* T10 连接中途关闭：只到了半帧就断开 -> 不得输出伪造完整帧 */
static void t10_mid_close(void)
{
    uint8_t f[STATUS_FRAME_LEN];
    make_car(f, 1, 2, 3, 4, 5, 6.0f, 7, 8, 9);

    RecvBuffer rb;
    recv_buffer_init(&rb);
    g_list.count = 0;
    feed_more(&rb, f, 12);                 /* 只到 12 字节就断 */
    size_t n = g_list.count;

    /* 再喂入与上一帧无关的 12 字节也不应产出帧 */
    uint8_t junk[12];
    memset(junk, 0x33, sizeof(junk));
    g_list.count = 0;
    feed_more(&rb, junk, sizeof(junk));
    size_t n2 = g_list.count;

    int ok = (n == 0) && (n2 == 0);
    record("T10", "connection_mid_close_partial_frame", 24, 0, n + n2, ok, 0,
           "partial frame buffered, never emitted as a fake complete frame");
}

/* T11 帧尾字节序列出现在 payload 中 */
static void t11_tail_bytes_in_payload(void)
{
    uint8_t f[STATUS_FRAME_LEN];
    make_uwb(f, 3, 1.0f, 2.0f, 3.0f);
    /* 把 z 的末两字节改成 0x55 0x56：位置 16/17，属于 payload，不是帧尾 */
    f[16] = 0x55;
    f[17] = 0x56;

    float z;
    memcpy(&z, f + 14, 4);

    size_t n = feed(f, sizeof(f));
    int ok = (n == 1) &&
             g_list.frames[0].type == FRAME_TYPE_UWB &&
             g_list.frames[0].data.uwb.tag_num == 3 &&
             feq(g_list.frames[0].data.uwb.z, z);
    record("T11", "tail_magic_inside_payload", sizeof(f), 1, n, ok, 0,
           "protocol uses tail at [18][19] only; same bytes at [16][17] are payload");
}

/* T12 长度不足 / 帧头不符时解析函数必须拒绝 */
static void t12_short_and_wrong_head(void)
{
    uint8_t f[STATUS_FRAME_LEN];
    make_uwb(f, 1, 0.0f, 0.0f, 0.0f);

    UwbState u;
    RobotState c;
    int r_short_uwb = parse_uwb_frame(f, STATUS_FRAME_LEN - 1, &u);
    int r_short_car;
    make_car(f, 0, 0, 0, 0, 0, 0.0f, 0, 0, 0);
    r_short_car = parse_car_frame(f, STATUS_FRAME_LEN - 1, &c);

    make_uwb(f, 1, 0.0f, 0.0f, 0.0f);
    f[1] = 'z';                            /* 第二字节非 'p' */
    int r_head_uwb = parse_uwb_frame(f, STATUS_FRAME_LEN, &u);

    make_car(f, 0, 0, 0, 0, 0, 0.0f, 0, 0, 0);
    f[0] = 'u';                            /* 车体帧头不符 */
    int r_head_car = parse_car_frame(f, STATUS_FRAME_LEN, &c);

    int ok = (r_short_uwb == -1) && (r_short_car == -1) &&
             (r_head_uwb == -1) && (r_head_car == -1);
    size_t got = (size_t)((r_short_uwb == -1) + (r_short_car == -1) +
                          (r_head_uwb == -1) + (r_head_car == -1));
    record("T12", "parser_rejects_short_and_bad_head", STATUS_FRAME_LEN, 4, got,
           ok, 0, "4 rejections: short UWB/CAR, wrong 2nd byte, wrong head");
}

/* T13 build_control_frame 布局与缓冲区下界 */
static void t13_build_control(void)
{
    uint8_t f[CONTROL_FRAME_LEN];
    memset(f, 0xEE, sizeof(f));
    int r = build_control_frame(f, sizeof(f), 1, 2, -5, 6, -7, 200);

    int layout_ok = (r == 0) &&
                    f[0] == 0x77 && f[1] == 0x00 && f[2] == 1 && f[3] == 2 &&
                    f[4] == 0x00 &&
                    f[5] == (uint8_t)(int8_t)-5 &&
                    f[6] == (uint8_t)6 &&
                    f[7] == (uint8_t)(int8_t)-7 &&
                    f[18] == 200 && f[19] == 0x0A;
    for (int i = 8; i <= 17; i++) if (f[i] != 0x00) layout_ok = 0;

    uint8_t small[CONTROL_FRAME_LEN - 1];
    int r_small = build_control_frame(small, sizeof(small), 0, 0, 0, 0, 0, 0);

    int ok = layout_ok && (r_small == -1);
    record("T13", "build_control_frame_layout_and_bounds", CONTROL_FRAME_LEN,
           2, (size_t)(layout_ok ? 1 : 0) + (size_t)(r_small == -1 ? 1 : 0),
           ok, 0, "0x77 head, 0x0A tail, reserved bytes zero, short buffer rejected");
}

/* T14 mode_name 映射 */
static void t14_mode_name(void)
{
    int ok = (strcmp(mode_name(0), "STOP") == 0) &&
             (strcmp(mode_name(1), "MANUAL") == 0) &&
             (strcmp(mode_name(2), "AUTO") == 0) &&
             (strcmp(mode_name(9), "UNKNOWN") == 0);
    record("T14", "mode_name_mapping", 0, 4,
           (size_t)((strcmp(mode_name(0), "STOP") == 0) +
                    (strcmp(mode_name(1), "MANUAL") == 0) +
                    (strcmp(mode_name(2), "AUTO") == 0) +
                    (strcmp(mode_name(9), "UNKNOWN") == 0)),
           ok, 0, "0/1/2/other");
}

/* T15 空输入 */
static void t15_empty_input(void)
{
    uint8_t dummy = 0;
    size_t n = feed(&dummy, 0);
    int ok = (n == 0);
    record("T15", "empty_input_zero_frames", 0, 0, n, ok, 0, "len=0 -> no frame, no crash");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static int ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(dir, 0755);
}

int main(int argc, char *argv[])
{
    const char *csv_path = (argc > 1) ? argv[1]
                                      : "performance_evaluation/results/03_parser_tests.csv";

    /* 保证父目录存在（只建一层，路径固定） */
    char dirbuf[512];
    snprintf(dirbuf, sizeof(dirbuf), "%s", csv_path);
    char *slash = strrchr(dirbuf, '/');
    if (slash) { *slash = '\0'; ensure_dir(dirbuf); }

    g_csv = fopen(csv_path, "w");
    if (!g_csv) {
        fprintf(stderr, "WARNING: cannot open %s: %s (continuing without CSV)\n",
                csv_path, strerror(errno));
    } else {
        fprintf(g_csv, "case_id,case_name,input_bytes,expected_frames,"
                       "parsed_frames,passed,seed,notes\n");
    }

    printf("=== robot_protocol parser state machine tests ===\n");
    printf("build: %s %s\n", __DATE__, __TIME__);

    t01_single_uwb();
    t02_single_car();
    t03_sticky_two();
    t04_split_frame();
    t05_random_chunking();
    t06_garbage_prefix();
    t07_bad_tail();
    t08_unknown_head();
    t09_flood();
    t10_mid_close();
    t11_tail_bytes_in_payload();
    t12_short_and_wrong_head();
    t13_build_control();
    t14_mode_name();
    t15_empty_input();

    printf("=== summary: %d passed, %d failed ===\n", g_pass, g_fail);

    if (g_csv) {
        fprintf(g_csv, "TOTAL,,,%d,%d,%s,,\n", g_pass + g_fail, g_pass,
                g_fail == 0 ? "PASS" : "FAIL");
        fclose(g_csv);
        printf("results CSV: %s\n", csv_path);
    }

    return g_fail == 0 ? 0 : 1;
}
