/*
 * Attadipa pedometer probe — QMI8658 on the Waveshare ESP32-S3-Touch-AMOLED-2.06.
 *
 * Answers H14(b): which QMI8658 datasheet describes the silicon on this board,
 * and then whether chapter 11's engine actually counts steps. OD-6 makes the
 * pedometer mandatory, so this is not a curiosity.
 *
 * Every register address, bit and parameter below is taken from the datasheet
 * this repository has actually read:
 *   "Document#: 13-52-27 ∙ QMI8658C Datasheet ∙ Rev A" (© 2022 QST),
 * Table 21 (register map, §5.2), Table 22 (CTRL1/2/7/8, §5.3), §5.10.4
 * (WCtrl9 protocol), Table 29 (CAL registers), Table 37 (pedometer
 * parameters) and Table 38 (§11.2, Configure Pedometer).
 *
 * Which document number names the Rev A part is DISPUTED in this tree and is
 * deferred to issue #341. What is in 13-52-25 is UNKNOWN here -- no record
 * shows anyone opening it -- so nothing below rests on that number.
 *
 * WHY THIS FILE CHANGED. Three bench runs on 2026-08-28 read steps=0 for 240,
 * 240 and 900 seconds. Two faults, not one: the board never moved, and the
 * probe never configured the engine. It set CTRL8.Pedo_EN and CTRL7.aEN and
 * stopped there, but §11.2 passes eight parameters through two CTRL9 0x0D
 * calls, and two of them decide the outcome — ped_time_cnt_entry discards
 * steps until that many consecutive ones are seen, and ped_sig_count means the
 * output registers move only every N steps. Unconfigured, steps=0 is a legal
 * result for a genuine walk, so the old probe could not tell "does not count"
 * from "was never asked to".
 *
 * WRITES: CTRL2, CTRL3, CTRL7, CTRL8, CTRL9 and CAL1_L..CAL4_H on the IMU at
 * 0x6B, and nothing else on any device. No PMU write, no rail change, no
 * display. The QMI8658 has no non-volatile configuration, so a power cycle
 * restores the defaults; this probe also restores CTRL2/CTRL3/CTRL7/CTRL8 to
 * the values it read at start-up. It says whether that took for CTRL2, CTRL7
 * and CTRL8 only: CTRL3 is written back but never read back, so the restore
 * line below is silent about it. CAL1..CAL4 are left as written —
 * they are scratch inputs to CTRL9 and mean nothing once Pedo_EN is clear.
 */
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#define MAIN_SDA GPIO_NUM_15 /* VERIFIED — HARDWARE_MATRIX main I2C row */
#define MAIN_SCL GPIO_NUM_14

#define QMI_ADDR 0x6B /* measured, not assumed: the bench probe's scan found */
                      /* 0x6B and no 0x6A — WAVESHARE_RUNNING_OUR_CODE §3.1 */

/* Register map — 13-52-27 Rev A, Table 21 and Table 29 */
#define REG_WHO_AM_I   0x00
#define REG_REVISION   0x01
#define REG_CTRL1      0x02
#define REG_CTRL2      0x03
#define REG_CTRL3      0x04
#define REG_CTRL7      0x08
#define REG_CTRL8      0x09
#define REG_CTRL9      0x0A
#define REG_CAL1_L     0x0B
#define REG_CAL1_H     0x0C
#define REG_CAL2_L     0x0D
#define REG_CAL2_H     0x0E
#define REG_CAL3_L     0x0F
#define REG_CAL3_H     0x10
#define REG_CAL4_L     0x11
#define REG_CAL4_H     0x12
#define REG_STATUSINT  0x2D
#define REG_STATUS1    0x2F
#define REG_AX_L       0x35
#define REG_STEP_LOW   0x5A

/* CTRL9 command list — Table 28 */
#define CMD_ACK                  0x00
#define CMD_CONFIGURE_PEDOMETER  0x0D
#define CMD_RESET_PEDOMETER      0x0F

/* SUPERSEDED. This is what the WALK run wrote, and it is kept so that archived
 * capture stays readable: walk.log:52-53 echoes exactly this set -- sample_cnt=62,
 * time_up=250, time_low=25, cnt_entry=10, sig_count=4, with the two mg thresholds
 * unscaled at 0x00CC/0x0066. Nothing below is scaled; the parameters this probe
 * writes are SensorLib's, sourced in the block above them. CTRL2 was
 * aFS = 010 (+/-8 g), aODR = 0111 (62.5 Hz, accel-only). QST's worked example
 * in §11.1 is written for 50 Hz and the part has no such rate — the ladder is
 * 250 / 125 / 62.5 / 31.25 — so 62.5 Hz was the nearest, and the sample-count
 * parameters of THAT run, the set named at the top of this comment, were scaled
 * by 62.5/50 = 1.25. Its two thresholds were amplitudes in mg and did not
 * scale. None of those defines is in this file; what this probe writes is the
 * 6DOF configuration in the paragraph below. */
/* SensorLib's working example runs the pedometer in 6DOF with BOTH accelerometer
 * and gyroscope enabled -- "matching vendor pedometer runtime path" -- and notes
 * that in 6DOF the ODR base follows the gyro, so the same aODR code means a
 * different frequency: code 0x06 is 125 Hz accel-only and 112.1 Hz in 6DOF.
 * That library is NOT among this repository's pinned upstreams and is not known
 * to ship on this board -- it is a lead at a recorded revision, not a source.
 * Three probe runs configured strictly
 * from chapter 11 -- accelerometer alone at 62.5 Hz -- counted nothing, so this
 * one follows the configuration with field evidence instead of the one with
 * only a datasheet. CTRL7=0x03 is also the state the probe FOUND on the board --
 * but that says nothing about the vendor, and nothing about Attadipa either:
 * ITS WRITER CANNOT BE IDENTIFIED AT ALL. T-166 replaced this unit's factory
 * image on 2026-08-25, so found state here is whatever the last program left.
 * VERIFIED_FACTS.md:1582-1586 records that attributing it to Attadipa's own
 * firmware "cannot be supported". Two earlier versions of this comment drew an
 * attribution -- first to the vendor, then to Attadipa -- and both were wrong;
 * the point is that no attribution is available, not that a different one is. */
#define CTRL2_VALUE 0x16           /* aFS=001 (+/-4 g), aODR=0x06 (112.1 Hz 6DOF) */
#define CTRL3_VALUE 0x36           /* gFS=011 (+/-128 dps), gODR=0x06 (112.1 Hz)  */
/* DERIVED, not asserted. Table 22's aFS ladder is +/-2/4/8/16 g at
 * 16384/8192/4096/2048 LSB/g, so sensitivity is 16384 >> aFS, and aFS is
 * CTRL2 bits [6:4]. A hand-kept constant here is exactly how shake.log:49 came
 * to print "+/-8 g" against a register that means +/-4 g: the label did not
 * follow CTRL2 when CTRL2 moved. This cannot -- raise the full scale for a
 * swinging arm and the divisor, and every printed mg, moves with it. */
#define ACCEL_LSB_PER_G (16384 >> ((CTRL2_VALUE >> 4) & 0x07))
/* CTRL7: aEN = 1 AND gEN = 1. §11.3 says the engine runs off the accelerometer
 * and that alone counted nothing here across three runs, so this follows
 * SensorLib and the state the board was found in. syncSmpl (bit 7) stays 0:
 * §11 says the pedometer runs in Non-SyncSample mode only, and STATUSINT.bit1
 * then mirrors INT1 rather than a data lock. */
#define CTRL7_VALUE 0x03   /* aEN | gEN -- 6DOF */
/* CTRL8: CTRL9_HandShake_Type = 1 (bit 7) so CmdDone is read from STATUSINT
 * rather than waited for on INT1, which this probe does not wire up. */
#define CTRL8_CONFIG 0x80          /* handshake only, Pedo_EN still 0 */
#define CTRL8_VALUE  0x90          /* handshake + Pedo_EN; the 0->1 edge on   */
                                   /* bit 4 also clears the count, §11.6      */

/* SensorLib's bring-up profile at 2b9e591f, examples/sensor/qmi8658_pedometer:
 *     imu.configPedometer(50, 80, 60, 400, 8, 1, 0, 1);
 * with the comment "Datasheet profile is conservative and designed to reject
 * non-step vibration. For handheld bring-up, use a lower entry count and lower
 * thresholds." Somebody could not make the datasheet numbers trigger on real
 * hardware and loosened them; three runs here could not either. The two that
 * matter are entry_count and sig_count, both 1: the register then moves on the
 * FIRST step instead of holding nine back and then counting in fours. */
#define PED_SAMPLE_CNT     50      /* calculation window                        */
#define PED_FIX_PEAK2PEAK  80      /* ~78 mg in u6.10, against the datasheet 200 */
#define PED_FIX_PEAK       60      /* ~59 mg, against the datasheet 100          */
#define PED_TIME_UP        400     /* slowest step accepted                      */
#define PED_TIME_LOW       8       /* fastest step accepted                      */
#define PED_TIME_CNT_ENTRY 1       /* count from the first step, do not hold back */
#define PED_FIX_PRECISION  0       /* "0 is recommended" -- Table 37             */
#define PED_SIG_COUNT      1       /* move the registers every single step       */

/* u6.10 -> mg, once. The header used to print this and the OVER column used to
 * compare against a hard-coded 200 -- the datasheet's bar, not the configured
 * one -- so a reader could not tell which threshold a mark meant. */
#define PED_MG(u6_10)      (((u6_10) * 1000 + 512) / 1024)

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t imu;

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(imu, &reg, 1, out, n, 200);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(imu, buf, 2, 200);
}

static uint8_t rd1(uint8_t reg)
{
    uint8_t v = 0xFF;
    return rd(reg, &v, 1) == ESP_OK ? v : 0xFF;
}

/* §5.10.4. Write the command, wait for STATUSINT.bit7 (CmdDone), acknowledge
 * with CTRL_CMD_ACK, and confirm the device cleared the bit. Returns false on
 * any step, because a command that was not acknowledged is a command whose
 * parameters the engine may or may not be holding. */
static bool ctrl9(uint8_t cmd)
{
    if (wr(REG_CTRL9, cmd) != ESP_OK) return false;
    for (int i = 0; i < 100; i++) {              /* 1 s ceiling */
        if (rd1(REG_STATUSINT) & 0x80) {
            if (wr(REG_CTRL9, CMD_ACK) != ESP_OK) return false;
            vTaskDelay(pdMS_TO_TICKS(10));
            return (rd1(REG_STATUSINT) & 0x80) == 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

/* Table 38. The two calls carry different meanings in the same six registers;
 * CAL4_H selects which set the engine is being handed. */
static bool configure_pedometer(void)
{
    wr(REG_CAL1_L, PED_SAMPLE_CNT & 0xFF);
    wr(REG_CAL1_H, PED_SAMPLE_CNT >> 8);
    wr(REG_CAL2_L, PED_FIX_PEAK2PEAK & 0xFF);
    wr(REG_CAL2_H, PED_FIX_PEAK2PEAK >> 8);
    wr(REG_CAL3_L, PED_FIX_PEAK & 0xFF);
    wr(REG_CAL3_H, PED_FIX_PEAK >> 8);
    /* Table 38 calls CAL4_L "NA" for both command sets. SensorLib writes 0x02
     * to it in both, and has since its first commit -- but this probe writes
     * 0x02 and then 0x00 over it, so the engine reads 0x00 at both CTRL9 0x0D
     * calls, in both sets. That is what the archived captures ran, and no
     * capture echoes the byte, so its effect is UNKNOWN either way. Left as it
     * ran rather than reduced to the single SensorLib write: that would be a
     * behaviour change with no bench run behind it. */
    wr(REG_CAL4_L, 0x02);
    wr(REG_CAL4_L, 0x00);
    wr(REG_CAL4_H, 0x01);
    if (!ctrl9(CMD_CONFIGURE_PEDOMETER)) return false;

    wr(REG_CAL1_L, PED_TIME_UP & 0xFF);
    wr(REG_CAL1_H, PED_TIME_UP >> 8);
    wr(REG_CAL2_L, PED_TIME_LOW);
    wr(REG_CAL2_H, PED_TIME_CNT_ENTRY);
    wr(REG_CAL3_L, PED_FIX_PRECISION);
    wr(REG_CAL3_H, PED_SIG_COUNT);
    wr(REG_CAL4_L, 0x02);
    wr(REG_CAL4_L, 0x00);
    wr(REG_CAL4_H, 0x02);
    return ctrl9(CMD_CONFIGURE_PEDOMETER);
}

/* CTRL1.ADDR_AI defaults to 0 (address non-increment), so a burst read of the
 * data registers is not safe until it is set. This probe reads byte by byte
 * instead of writing CTRL1. */
static int16_t axis(uint8_t low_reg)
{
    uint8_t lo = rd1(low_reg), hi = rd1(low_reg + 1);
    return (int16_t)((uint16_t)hi << 8 | lo);
}

static uint32_t steps(void)
{
    return (uint32_t)rd1(REG_STEP_LOW) | ((uint32_t)rd1(REG_STEP_LOW + 1) << 8) |
           ((uint32_t)rd1(REG_STEP_LOW + 2) << 16);
}

void app_main(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = MAIN_SDA,
        .scl_io_num = MAIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &imu));

    printf("\n######## ATTADIPA PEDOMETER PROBE ########\n");
    printf("Writes CTRL2/CTRL3/CTRL7/CTRL8/CTRL9 and CAL1..CAL4 on the IMU at 0x%02X.\n"
           "Nothing else, on any device. No PMU write, no rail change, no display.\n\n",
           QMI_ADDR);

    uint8_t who = rd1(REG_WHO_AM_I), rev = rd1(REG_REVISION);
    printf("WHO_AM_I  = 0x%02X   (0x05 in both candidate datasheets)\n", who);
    printf("REVISION  = 0x%02X   (0x7C is the Rev A part; which document number"
           " names it is disputed -- see #341. 0x79 = Rev 0.6)\n\n",
           rev);
    if (who != 0x05) {
        printf("Not a QMI8658 signature. Stopping rather than writing anything.\n");
        return;
    }

    /* CAPTURED, NOT ASSUMED. An earlier version ended by writing 0x00/0x00/0x00
     * and calling that "restoring defaults". It is not: the QMI8658 holds none
     * of this in non-volatile memory, so what is here is whatever the last
     * program left, and what comes back has to be what was there. It varies:
     * three runs on 2026-08-28 found CTRL2=0x24 CTRL7=0x03 CTRL8=0x00, and the
     * run after the abandoned walk found CTRL2=0x27 CTRL7=0x01 CTRL8=0x90 --
     * the walk probe's own armed state, still set half an hour later.
     *
     * READ THE COUNT FIRST. It is an accumulating register, and configuring
     * destroys it: the 0->1 edge on CTRL8 bit 4 clears it (11.6). A walk done
     * with the cable out leaves its number here and nowhere else, so a probe
     * that arms before reading throws away the only evidence the walk produced.
     * That happened once already, on 2026-08-28. */
    const uint8_t was_c2 = rd1(REG_CTRL2);
    const uint8_t was_c7 = rd1(REG_CTRL7);
    const uint8_t was_c8 = rd1(REG_CTRL8);
    const uint8_t was_c3 = rd1(REG_CTRL3);
    printf("--- before ---   CTRL2=0x%02X CTRL7=0x%02X CTRL8=0x%02X"
           "  step count found = %" PRIu32 "\n"
           "  (that count is about to be cleared by arming; if a walk happened\n"
           "   with the cable out, this line is its only record)\n\n",
           was_c2, was_c7, was_c8, steps());

    /* §11.2: "Configuration should be done when accelerometer and gyroscope are
     * disabled (CTRL7.aEN = CTRL7.gEN = 0)." Every state this probe has found
     * so far had at least aEN set, so this is a real precondition, not a
     * formality. */
    printf("CTRL7 = 0x00 (aEN=gEN=0, required by 11.2 before configuring) -> %s\n",
           wr(REG_CTRL7, 0x00) == ESP_OK ? "ACK" : "NAK");
    printf("CTRL8 = 0x%02X (CTRL9 handshake on STATUSINT, Pedo_EN still 0)  -> %s\n",
           CTRL8_CONFIG, wr(REG_CTRL8, CTRL8_CONFIG) == ESP_OK ? "ACK" : "NAK");
    printf("CTRL2 = 0x%02X (+/-4 g, 112.1 Hz in 6DOF -- gyro sets the rate) -> %s\n",
           CTRL2_VALUE, wr(REG_CTRL2, CTRL2_VALUE) == ESP_OK ? "ACK" : "NAK");
    printf("CTRL3 = 0x%02X (+/-128 dps, 112.1 Hz -- 6DOF needs the gyro up)   -> %s\n",
           CTRL3_VALUE, wr(REG_CTRL3, CTRL3_VALUE) == ESP_OK ? "ACK" : "NAK");
    vTaskDelay(pdMS_TO_TICKS(50));

    const bool configured = configure_pedometer();
    printf("\ntwo CTRL9 0x0D calls (Table 38): %s\n",
           configured ? "both acknowledged -- CmdDone set and cleared each time"
                      : "FAILED -- a step count read after this means nothing");
    printf("  sample_cnt=%d  peak2peak=0x%04X  peak=0x%04X  time_up=%d\n"
           "  time_low=%d  cnt_entry=%d  precision=%d  sig_count=%d\n"
           "  accel scale: %d LSB/g -- the divisor every p2p mg below uses\n",
           PED_SAMPLE_CNT, PED_FIX_PEAK2PEAK, PED_FIX_PEAK, PED_TIME_UP,
           PED_TIME_LOW, PED_TIME_CNT_ENTRY, PED_FIX_PRECISION, PED_SIG_COUNT,
           ACCEL_LSB_PER_G);
    if (!configured) {
        printf("\nRestoring and stopping: configuring failed, so nothing this probe\n"
               "could print afterwards would answer the question it was built for.\n");
        wr(REG_CTRL8, was_c8);
        wr(REG_CTRL7, was_c7);
        wr(REG_CTRL2, was_c2);
        wr(REG_CTRL3, was_c3);
        printf("--- restored --- CTRL2=0x%02X CTRL7=0x%02X CTRL8=0x%02X\n",
               rd1(REG_CTRL2), rd1(REG_CTRL7), rd1(REG_CTRL8));
        printf("######## END ########\n");
        return;
    }

    printf("\nCTRL8 = 0x%02X (Pedo_EN; the 0->1 edge clears the count, 11.6) -> %s\n",
           CTRL8_VALUE, wr(REG_CTRL8, CTRL8_VALUE) == ESP_OK ? "ACK" : "NAK");
    printf("CTRL7 = 0x%02X (aEN; the engine starts counting here, 11.3)     -> %s\n",
           CTRL7_VALUE, wr(REG_CTRL7, CTRL7_VALUE) == ESP_OK ? "ACK" : "NAK");
    printf("CTRL9 0x0F (reset step count): %s\n",
           ctrl9(CMD_RESET_PEDOMETER) ? "acknowledged" : "not acknowledged");
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t c2 = rd1(REG_CTRL2), c7 = rd1(REG_CTRL7), c8 = rd1(REG_CTRL8);
    printf("--- armed  ---   CTRL2=0x%02X CTRL7=0x%02X CTRL8=0x%02X  step count = %"
           PRIu32 "\n\n", c2, c7, c8, steps());
    printf("CTRL8 readback: %s\n\n",
           c8 == CTRL8_VALUE
               ? "accepted -- the register is writable, which the C Rev 0.6\n"
                 "  document does not describe (it calls CTRL8 Reserved: Not Used)"
               : "did NOT read back as written");

    printf("WHAT TO EXPECT, written down before the run so it cannot be fitted\n"
           "afterwards. This is SensorLib's bring-up profile, not the datasheet's:\n"
           "entry_count=1 and sig_count=1, so the register moves on the FIRST step\n"
           "and on every step after it. No nine held back, no counting in fours.\n"
           "Shaking the board in a walking rhythm should therefore make the count\n"
           "climb roughly one per shake, visibly, within a few seconds. A count\n"
           "that climbs at all -- by any amount, at any rate -- is the engine\n"
           "working. A steady 0 while the p2p column reads OVER for many seconds\n"
           "in a row is the engine not counting, and this time that is not\n"
           "ambiguous, because the threshold it is being held to is printed next\n"
           "to the motion it was given.\n"
           "STATUS1 bit 4 is the pedometer event (11.4); it should stop being 0.\n\n");

    /* The column that makes a zero mean something. Three earlier runs read 0 and
     * could not distinguish "the engine does not count" from "nothing moved" --
     * once because the board genuinely sat still, once because the walk happened
     * with the cable out and nothing recorded the motion. So sample fast and
     * report the peak-to-peak the engine was actually offered, in the same mg
     * the threshold is written in, and a second whose p2p clears the bar is a
     * second the engine had no excuse. The bar is PRINTED FROM THE CONSTANTS,
     * not quoted: this header used to say the datasheet's 200/100 while the
     * probe had been loosened to SensorLib's 80/60, so the log invited the
     * reader to hold the run to a threshold it was never configured with. */
    printf("p2p is peak-to-peak per axis over each second, in mg. The engine's\n"
           "threshold is ped_fix_peak2peak = %d (~%d mg in u6.10); ped_fix_peak\n"
           "= %d (~%d mg) is measured against the running average, which this\n"
           "cannot see.\n\n",
           PED_FIX_PEAK2PEAK, PED_MG(PED_FIX_PEAK2PEAK),
           PED_FIX_PEAK, PED_MG(PED_FIX_PEAK));

    uint32_t first = steps();
    for (int i = 0; i < 1200; i++) {
        int16_t lo[3] = {32767, 32767, 32767}, hi[3] = {-32768, -32768, -32768};
        int16_t last[3] = {0, 0, 0};
        for (int k = 0; k < 20; k++) {           /* 20 x 50 ms = one second */
            for (int a = 0; a < 3; a++) {
                int16_t v = axis(REG_AX_L + 2 * a);
                last[a] = v;
                if (v < lo[a]) lo[a] = v;
                if (v > hi[a]) hi[a] = v;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        int p2p[3];
        for (int a = 0; a < 3; a++) p2p[a] = (hi[a] - lo[a]) * 1000 / ACCEL_LSB_PER_G;
        int worst = p2p[0] > p2p[1] ? p2p[0] : p2p[1];
        if (p2p[2] > worst) worst = p2p[2];
        uint32_t st = steps();
        printf("t=%4ds  ax=%6d ay=%6d az=%6d  p2p mg: x=%4d y=%4d z=%4d  %s"
               "  steps=%" PRIu32 " (+%" PRIu32 ")  STATUS1=0x%02X\n",
               i, last[0], last[1], last[2], p2p[0], p2p[1], p2p[2],
               worst >= PED_MG(PED_FIX_PEAK2PEAK) ? "OVER " : "under",
               st, st - first, rd1(REG_STATUS1));
    }

    /* Restore what was read, and SAY whether it took. A cleanup that is not
     * verified is a cleanup that is believed. Pedo_EN in particular has to be
     * cleared deliberately: booting the vendor firmware rewrites CTRL2 and
     * CTRL7 but never touches CTRL8, so a probe that leaves bit 4 set leaves
     * the pedometer running under firmware that does not know it exists. */
    printf("\nfinal step count = %" PRIu32 "\n", steps());
    printf("restoring what was found: CTRL8=0x%02X CTRL7=0x%02X CTRL2=0x%02X\n",
           was_c8, was_c7, was_c2);
    wr(REG_CTRL8, was_c8);
    wr(REG_CTRL7, was_c7);
    wr(REG_CTRL2, was_c2);
    wr(REG_CTRL3, was_c3);
    vTaskDelay(pdMS_TO_TICKS(50));
    const uint8_t back_c2 = rd1(REG_CTRL2);
    const uint8_t back_c7 = rd1(REG_CTRL7);
    const uint8_t back_c8 = rd1(REG_CTRL8);
    printf("--- restored --- CTRL2=0x%02X CTRL7=0x%02X CTRL8=0x%02X  %s\n",
           back_c2, back_c7, back_c8,
           (back_c2 == was_c2 && back_c7 == was_c7 && back_c8 == was_c8)
               ? "verified: identical to the state this probe found"
               : "MISMATCH -- the IMU is NOT as it was found; power-cycle the unit");
    printf("######## END ########\n");
}
