#ifndef SC21_DRIVER_INIT_H
#define SC21_DRIVER_INIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atmel_start_pins.h"
#include "hal_ext_irq.h"
#include "hal_sleep.h"

#ifndef __cplusplus
typedef unsigned int uint;
#endif

#define COMPILER_ALIGNED(a) __attribute__((__aligned__(a)))
#define COMPILER_PACK_RESET()
#define ASSERT(cond) ((void)0)
#define CONTAINER_OF(ptr, type, field_name) \
    ((type *)(((uint8_t *)(ptr)) - offsetof(type, field_name)))

/* Generic ASF-compatible I/O descriptor interface */
struct io_descriptor;
typedef int32_t (*io_write_t)(struct io_descriptor *const io_descr, const uint8_t *const buf, const uint16_t length);
typedef int32_t (*io_read_t)(struct io_descriptor *const io_descr, uint8_t *const buf, const uint16_t length);

struct io_descriptor {
    io_write_t write;
    io_read_t  read;
};

int32_t io_write(struct io_descriptor *const io_descr, const uint8_t *const buf, const uint16_t length);
int32_t io_read(struct io_descriptor *const io_descr, uint8_t *const buf, const uint16_t length);

/* SysTick, delays, and reset */
uint32_t SysTick_Config(uint32_t ticks);
void NVIC_SystemReset(void);
void delay_ms(const uint16_t ms);
void delay_us(const uint16_t us);

/* Periodic timer service (20 ms touch_process task) */
enum timer_task_mode {
    TIMER_TASK_ONE_SHOT = 0,
    TIMER_TASK_REPEAT   = 1,
};

struct timer_task {
    uint32_t             interval;
    void               (*cb)(const struct timer_task *const timer_task);
    enum timer_task_mode mode;
    uint32_t             next_due_ms;
    bool                 active;
};

struct timer_descriptor {
    struct timer_task *tasks[4];
    uint8_t            task_count;
    bool               running;
};

extern struct timer_descriptor Timer;

int32_t timer_add_task(struct timer_descriptor *const descr, struct timer_task *const task);
int32_t timer_start(struct timer_descriptor *const descr);

/* PWM & TCC0 RGB LED (D1: WO[2]=PA22 Red, WO[1]=PA21 Green, WO[3]=PA23 Blue, active-low) */
struct pwm_descriptor {
    uint32_t period;
    uint32_t duty;
    bool     enabled;
};

extern struct pwm_descriptor PWM_0;

int32_t pwm_enable(struct pwm_descriptor *const descr);
int32_t pwm_set_parameters(struct pwm_descriptor *const descr, const uint32_t period, const uint32_t duty_cycle);

#define TCC0 ((void *)0)
void hri_tcc_write_CC_reg(const void *const hw, uint8_t index, uint32_t data);

/* Hardware RNG (TRNG) */
struct rand_sync_desc {
    uint32_t state;
};

extern struct rand_sync_desc RAND_0;

int32_t  rand_sync_enable(struct rand_sync_desc *const desc);
uint32_t rand_sync_read32(struct rand_sync_desc *const desc);

/* SPI Master (SPI_1 -> ST25R95 NFC transceiver) */
struct spi_xfer {
    uint8_t *txbuf;
    uint8_t *rxbuf;
    uint32_t size;
};

struct spi_m_sync_descriptor {
    struct io_descriptor io;
    bool                 enabled;
};

extern struct spi_m_sync_descriptor SPI_1;

int32_t spi_m_sync_get_io_descriptor(struct spi_m_sync_descriptor *const spi, struct io_descriptor **io);
int32_t spi_m_sync_enable(struct spi_m_sync_descriptor *const spi);
int32_t spi_m_sync_transfer(struct spi_m_sync_descriptor *const spi, const struct spi_xfer *xfer);

/* I2C Master (I2C_0 -> Minibadge bus) */
#define I2C_M_SEVEN 0

struct i2c_m_sync_desc {
    struct io_descriptor io;
    int16_t              slave_addr;
    bool                 enabled;
};

extern struct i2c_m_sync_desc I2C_0;

int32_t i2c_m_sync_get_io_descriptor(struct i2c_m_sync_desc *const i2c, struct io_descriptor **io);
int32_t i2c_m_sync_enable(struct i2c_m_sync_desc *const i2c);
int32_t i2c_m_sync_set_slaveaddr(struct i2c_m_sync_desc *const i2c, int16_t addr, int32_t addr_len);

/* Quad-SPI external 8 MB flash (QUAD_SPI_0) */
enum qspi_access {
    QSPI_CMD_ACCESS      = 0,
    QSPI_READ_ACCESS     = 1,
    QSPI_READMEM_ACCESS  = 2,
    QSPI_WRITE_ACCESS    = 3,
    QSPI_WRITEMEM_ACCESS = 4,
};

enum qspi_bus_width {
    QSPI_INST1_ADDR1_DATA1 = 0,
    QSPI_INST1_ADDR1_DATA2 = 1,
    QSPI_INST1_ADDR1_DATA4 = 2,
    QSPI_INST1_ADDR2_DATA2 = 3,
    QSPI_INST1_ADDR4_DATA4 = 4,
    QSPI_INST2_ADDR2_DATA2 = 5,
    QSPI_INST4_ADDR4_DATA4 = 6,
};

struct _qspi_command {
    union {
        struct {
            uint32_t width        : 3;
            uint32_t reserved0    : 1;
            uint32_t inst_en      : 1;
            uint32_t addr_en      : 1;
            uint32_t opt_en       : 1;
            uint32_t data_en      : 1;
            uint32_t opt_len      : 2;
            uint32_t addr_len     : 1;
            uint32_t reserved1    : 1;
            uint32_t tfr_type     : 2;
            uint32_t continues_read : 1;
            uint32_t double_rate  : 1;
            uint32_t dummy_cycles : 5;
            uint32_t reserved2    : 11;
        } bits;
        uint32_t word;
    } inst_frame;
    uint8_t     instruction;
    uint8_t     option;
    uint32_t    address;
    size_t      buf_len;
    const void *tx_buf;
    void       *rx_buf;
};

struct qspi_sync_descriptor {
    bool enabled;
};

extern struct qspi_sync_descriptor QUAD_SPI_0;

int32_t qspi_sync_enable(struct qspi_sync_descriptor *const descr);
int32_t qspi_sync_serial_run_command(struct qspi_sync_descriptor *const descr, const struct _qspi_command *cmd);

/* NVMCTRL SmartEEPROM (512 bytes) */
extern uint8_t sc21_seeprom_buf[512];
#define SEEPROM_ADDR (&sc21_seeprom_buf[0])

typedef struct {
    struct {
        struct {
            uint32_t CACHEDIS0 : 1;
        } bit;
        uint32_t reg;
    } CTRLA;
    struct {
        uint32_t reg;
    } CTRLB;
    struct {
        struct {
            uint32_t DONE : 1;
        } bit;
        uint32_t reg;
    } INTFLAG;
    struct {
        uint32_t reg;
    } ADDR;
    struct {
        struct {
            uint32_t SBLK  : 4;
            uint32_t PSZ   : 3;
            uint32_t LOCK  : 1;
            uint32_t BUSY  : 1;
            uint32_t RLOCK : 1;
        } bit;
        uint32_t reg;
    } SEESTAT;
    struct {
        uint32_t reg;
    } SEECFG;
} sc21_nvmctrl_regs_t;

extern sc21_nvmctrl_regs_t sc21_nvmctrl;
#define NVMCTRL (&sc21_nvmctrl)

#define NVMCTRL_CTRLB_CMD_EP       0x0000u
#define NVMCTRL_CTRLB_CMD_PBC      0x0015u
#define NVMCTRL_CTRLB_CMD_WQW      0x0004u
#define NVMCTRL_CTRLB_CMD_USEER    0x0032u
#define NVMCTRL_CTRLB_CMD_USEE     0x0030u
#define NVMCTRL_CTRLB_CMD_SEEFLUSH 0x0036u
#define NVMCTRL_CTRLB_CMDEX_KEY    0xA500u

/* AES-128-ECB & SHA-1 hardware crypto accelerators (CRYPTOGRAPHY_0, HASH_ALGORITHM_0) */
struct sha_context {
    uint8_t opaque[128];
};

struct sha_sync_descriptor {
    bool enabled;
};

extern struct sha_sync_descriptor HASH_ALGORITHM_0;

int32_t sha_sync_enable(struct sha_sync_descriptor *const desc);
int32_t sha_sync_sha1_compute(
    struct sha_sync_descriptor *const desc,
    struct sha_context *const         ctx,
    const void                       *buf,
    uint32_t                          len,
    uint8_t                           digest[20]);

enum aes_action {
    AES_DECRYPT = 0,
    AES_ENCRYPT = 1,
};

enum aes_keysize {
    AES_KEY_128 = 0,
    AES_KEY_192 = 1,
    AES_KEY_256 = 2,
};

struct aes_sync_descriptor {
    uint8_t key[16];
    bool    enabled;
};

extern struct aes_sync_descriptor CRYPTOGRAPHY_0;

int32_t aes_sync_enable(struct aes_sync_descriptor *const desc);
int32_t aes_sync_set_encrypt_key(
    struct aes_sync_descriptor *const desc,
    const uint8_t                    *key,
    const enum aes_keysize            size);
int32_t aes_sync_ecb_crypt(
    struct aes_sync_descriptor *const desc,
    const enum aes_action             enc,
    const uint8_t                    *input,
    uint8_t                          *output);

/* Clean first-party replacement for QTouch wheel + 4 capacitive key channels */
#define KEY_TOUCHED_MASK 0x80u

void     touch_process(void);
uint8_t  get_sensor_state(uint16_t sensor_node);
uint8_t  get_scroller_state(uint16_t sensor_node);
uint16_t get_scroller_position(uint16_t sensor_node);

/* USB CDC disabled stubs */
void    cdcd_loop(void);
int32_t cdcWrite(const char *const buf, const uint16_t length);

#endif /* SC21_DRIVER_INIT_H */
