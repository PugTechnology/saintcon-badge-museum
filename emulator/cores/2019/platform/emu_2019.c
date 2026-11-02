/*
 * SAINTCON 2019 Enigma Badge — STM32L433VCTx Hardware & Peripheral Emulator Shim
 *
 * Shims the STM32L4 HAL, GPIO (26 keyboard switches + 26 open-drain plugboard pins),
 * SPI1 (64x16 RGB LED matrix frame sent to the ICE40HX1K-TQ144 in matrix.c:192),
 * I2C1 (IS31FL3736 26-lamp RGB driver at 0x50<<1), I2C2 (minibadge expander),
 * UART1/UART2, TIM16 (30 Hz nextFrame tick), and Flash EEPROM emulation (eeprom_emul.h).
 */

#include "emu_2019.h"
#include "main.h"
#include "eeprom_emul.h"
#include "keyboard.h"
#include "lampboard.h"
#include "matrix.h"
#include "plugboard.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#define EMSCRIPTEN_KEEPALIVE
#endif

/* Firmware globals from upstream C files */
extern volatile uint8_t nextFrame;
extern uint8_t badgeLevel;
extern int menuState;
extern int enigma_menuState;
extern int custommessagelength;
extern int custommessagelengthpixels;
extern int newchar;
extern int cursorposition;
extern int framecounter;
extern uint8_t numMinibadgeBoards;
extern uint8_t minibadgeState;
extern uint8_t minibadgeMatrix[16];
extern uint8_t numMinibadgeAddr;
extern char plugboard[26];
extern uint32_t fw_keyboardUpdate(void);
extern void fw_enigma_menu(void);
extern int badge_2019_fw_main(void);

/* Peripheral handle instances referenced by upstream firmware */
GPIO_TypeDef GPIOA_inst = {0};
GPIO_TypeDef GPIOB_inst = {1};
GPIO_TypeDef GPIOC_inst = {2};
GPIO_TypeDef GPIOD_inst = {3};
GPIO_TypeDef GPIOE_inst = {4};
GPIO_TypeDef GPIOH_inst = {5};

I2C_HandleTypeDef hi2c1 = {1};
I2C_HandleTypeDef hi2c2 = {2};
SPI_HandleTypeDef hspi1 = {1};
SPI_HandleTypeDef hspi2 = {2};
TIM_HandleTypeDef htim16 = {16};
static USART_TypeDef USART1_inst = {0};
static USART_TypeDef USART2_inst = {0};
UART_HandleTypeDef huart1 = {1, &USART1_inst};
UART_HandleTypeDef huart2 = {2, &USART2_inst};

/* Framebuffer (384x96 RGB565 = 64x16 LED matrix at 6x6 px per LED cell) */
static uint16_t g_fb[EMU_2019_FB_W * EMU_2019_FB_H];

/* 26 RGB lamps (D1..D26 in lamp_matrix[0..25] slot order, 3 bytes R,G,B each) */
static uint8_t g_leds[EMU_2019_LED_COUNT * 3];

/* 512-byte persistent EEPROM image:
 *   bytes 0..319   : 8-bit values for slots 0..319 (NB_OF_VARIABLES = 320)
 *   bytes 320..367 : validity bitmap (bit=1 if slot has been written)
 *   bytes 508..511 : magic 'S','C','1','9'
 */
static uint8_t g_eeprom[EMU_2019_EEPROM_SZ];
static uint8_t g_factory_test_eeprom[EMU_2019_EEPROM_SZ];
static int g_eeprom_dirty = 0;
static int g_in_factory_test = 0;
static int g_factory_test_boot_pending = 0;
static int g_in_enigma_menu = 0;

/* IS31FL3736 register state on I2C1 (address 0x50 << 1 = 0xA0) */
static uint8_t g_issi_page = 0;
static uint8_t g_issi_pwm[256];

/* 26-key keyboard state (bit 0 = 'A' .. bit 25 = 'Z') */
static uint32_t g_az_keys_mask = 0;
static uint8_t g_nav_buttons_mask = 0;
static uint32_t g_prev_raw_az = 0;
static uint32_t g_latched_az_press = 0;
static int g_kbd_sampled_this_frame = 0;

/* 26-pin plugboard state (0 = 'A' .. 25 = 'Z') */
static uint32_t g_plug_mode[26];
static GPIO_PinState g_plug_out[26];
static int8_t g_plug_partner[26] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/* Virtual time & 30 Hz frame pacing */
static uint32_t g_tick_ms = 0;
static uint32_t g_30hz_msec_accum = 0;
static uint32_t g_speed_mult = 1;
static int g_host_steps_accum = 0;
static int g_30hz_budget = 0;
static int g_in_reset_kb = 0;

/* Cooperative fiber context */
#define FIBER_STACK_SZ (256 * 1024)
static int g_fiber_started = 0;

#ifdef __EMSCRIPTEN__
static uint8_t g_c_stack[FIBER_STACK_SZ] __attribute__((aligned(16)));
static uint8_t g_async_stack[FIBER_STACK_SZ] __attribute__((aligned(16)));
static uint8_t g_main_async_stack[4096] __attribute__((aligned(16)));
static emscripten_fiber_t g_fw_fiber;
static emscripten_fiber_t g_host_fiber;
static int g_host_fiber_inited = 0;
#else
static uint8_t g_c_stack[FIBER_STACK_SZ] __attribute__((aligned(16)));
static ucontext_t g_fw_ctx;
static ucontext_t g_host_ctx;
#endif

static void emu_yield_to_host(void) {
#ifdef __EMSCRIPTEN__
  emscripten_fiber_swap(&g_fw_fiber, &g_host_fiber);
#else
  swapcontext(&g_fw_ctx, &g_host_ctx);
#endif
}

static void emu_wait_next_30hz_frame(void) {
  while (g_30hz_budget <= 0) {
    emu_yield_to_host();
  }
  g_30hz_budget--;
  nextFrame = 1;
}

static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

/* Fixed monotonic brightness curve shared by the 64x16 RGB LED matrix and 26 RGB lamps:
 *   v == 0        -> 0
 *   1 <= v <= 128 -> 48 .. 200 (keeps firmware 0..128 COLOR_* constants bright on screen)
 *   129 <= v <= 255 -> 200 .. 255 (monotonically non-decreasing across the full 0..255 ramp)
 */
uint8_t emu_2019_scale_led_channel(uint8_t v) {
  if (v == 0U) return 0U;
  if (v <= 128U) {
    return (uint8_t)(48U + ((uint32_t)v * 152U) / 128U);
  }
  return (uint8_t)(200U + (((uint32_t)(v - 128U) * 55U) / 127U));
}

/* Render 64x16 RGB888 matrix into 384x96 RGB565 framebuffer with LED dot styling */
static void render_matrix_to_fb(const uint8_t *rgb888) {
  const uint16_t bg_pcb = rgb888_to_rgb565(8, 10, 14);
  const uint16_t off_ring = rgb888_to_rgb565(22, 25, 32);
  const uint16_t off_pkg = rgb888_to_rgb565(34, 38, 46);

  for (int row = 0; row < EMU_2019_MATRIX_H; row++) {
    for (int col = 0; col < EMU_2019_MATRIX_W; col++) {
      const uint8_t *px = &rgb888[(row * EMU_2019_MATRIX_W + col) * 3];
      uint8_t r = emu_2019_scale_led_channel(px[0]);
      uint8_t g = emu_2019_scale_led_channel(px[1]);
      uint8_t b = emu_2019_scale_led_channel(px[2]);

      int base_x = col * EMU_2019_DOT_SCALE;
      int base_y = row * EMU_2019_DOT_SCALE;

      if (r == 0 && g == 0 && b == 0) {
        for (int dy = 0; dy < EMU_2019_DOT_SCALE; dy++) {
          uint16_t *dst = &g_fb[(base_y + dy) * EMU_2019_FB_W + base_x];
          for (int dx = 0; dx < EMU_2019_DOT_SCALE; dx++) {
            if (dx == 0 || dy == 0 || dx == 5 || dy == 5) {
              dst[dx] = bg_pcb;
            } else if ((dx == 1 || dx == 4) && (dy == 1 || dy == 4)) {
              dst[dx] = off_ring;
            } else {
              dst[dx] = off_pkg;
            }
          }
        }
      } else {
        uint16_t core_col = rgb888_to_rgb565(r, g, b);
        uint8_t hr = (uint8_t)(r + ((255U - r) >> 2));
        uint8_t hg = (uint8_t)(g + ((255U - g) >> 2));
        uint8_t hb = (uint8_t)(b + ((255U - b) >> 2));
        uint16_t center_col = rgb888_to_rgb565(hr, hg, hb);
        uint16_t halo_col = rgb888_to_rgb565((uint8_t)(r >> 1), (uint8_t)(g >> 1), (uint8_t)(b >> 1));

        for (int dy = 0; dy < EMU_2019_DOT_SCALE; dy++) {
          uint16_t *dst = &g_fb[(base_y + dy) * EMU_2019_FB_W + base_x];
          for (int dx = 0; dx < EMU_2019_DOT_SCALE; dx++) {
            if (dx == 0 || dy == 0 || dx == 5 || dy == 5) {
              dst[dx] = bg_pcb;
            } else if ((dx == 1 || dx == 4) && (dy == 1 || dy == 4)) {
              dst[dx] = halo_col;
            } else if ((dx == 2 || dx == 3) && (dy == 2 || dy == 3)) {
              dst[dx] = center_col;
            } else {
              dst[dx] = core_col;
            }
          }
        }
      }
    }
  }
}

/* Update g_leds[26*3] from IS31FL3736 PWM registers written by lampboard_update() / animations.c:
 * In lampboard.h, row y (0x00, 0x30, 0x60, 0x90) is GREEN, row y+1 (0x10, 0x40, 0x70, 0xA0) is RED,
 * and row y+2 (0x20, 0x50, 0x80, 0xB0) is BLUE. */
static void sync_lamps_from_issi(void) {
  int index = 0;
  for (int y = 0; y < 12 && index < 26; y += 3) {
    for (int x = 0; x < 16 && index < 26; x += 2) {
      uint8_t g = emu_2019_scale_led_channel(g_issi_pwm[(y * 16) + x]);
      uint8_t r = emu_2019_scale_led_channel(g_issi_pwm[((y + 1) * 16) + x]);
      uint8_t b = emu_2019_scale_led_channel(g_issi_pwm[((y + 2) * 16) + x]);
      g_leds[index * 3 + 0] = r;
      g_leds[index * 3 + 1] = g;
      g_leds[index * 3 + 2] = b;
      index++;
    }
  }
}

/* Compute raw 26-key state from keyboard + navigation buttons (bit 0 = 'A' .. bit 25 = 'Z') */
static uint32_t raw_az_mask(void) {
  uint32_t m = g_az_keys_mask;
  if (g_nav_buttons_mask & (1U << 0)) m |= (1U << ('W' - 'A')); /* UP    -> W */
  if (g_nav_buttons_mask & (1U << 1)) m |= (1U << ('S' - 'A')); /* DOWN  -> S */
  if (g_nav_buttons_mask & (1U << 2)) m |= (1U << ('A' - 'A')); /* LEFT  -> A */
  if (g_nav_buttons_mask & (1U << 3)) m |= (1U << ('D' - 'A')); /* RIGHT -> D */
  if (g_nav_buttons_mask & (1U << 4)) m |= (1U << ('K' - 'A')); /* A     -> K */
  if (g_nav_buttons_mask & (1U << 5)) m |= (1U << ('O' - 'A')); /* B     -> O */
  return m & 0x03FFFFFFU;
}

static void latch_rising_az_keys(void) {
  uint32_t raw = raw_az_mask();
  uint32_t rising = raw & ~g_prev_raw_az;
  g_latched_az_press |= rising;
  g_prev_raw_az = raw;
}

/* Compute combined 26-key state including any unconsumed rising-edge press */
static uint32_t effective_az_mask(void) {
  if (g_in_reset_kb) return 0;
  return (raw_az_mask() | g_latched_az_press) & 0x03FFFFFFU;
}

/* Map (GPIOx, GPIO_Pin) to keyboard letter index 0..25, or -1 */
static int find_kbd_pin_index(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
  static const struct {
    GPIO_TypeDef *port;
    uint16_t pin;
  } kbd_map[26] = {
    {KBDA_GPIO_Port, KBDA_Pin}, {KBDB_GPIO_Port, KBDB_Pin},
    {KBDC_GPIO_Port, KBDC_Pin}, {KBDD_GPIO_Port, KBDD_Pin},
    {KBDE_GPIO_Port, KBDE_Pin}, {KBDF_GPIO_Port, KBDF_Pin},
    {KBDG_GPIO_Port, KBDG_Pin}, {KBDH_GPIO_Port, KBDH_Pin},
    {KBDI_GPIO_Port, KBDI_Pin}, {KBDJ_GPIO_Port, KBDJ_Pin},
    {KBDK_GPIO_Port, KBDK_Pin}, {KBDL_GPIO_Port, KBDL_Pin},
    {KBDM_GPIO_Port, KBDM_Pin}, {KBDN_GPIO_Port, KBDN_Pin},
    {KBDO_GPIO_Port, KBDO_Pin}, {KBDP_GPIO_Port, KBDP_Pin},
    {KBDQ_GPIO_Port, KBDQ_Pin}, {KBDR_GPIO_Port, KBDR_Pin},
    {KBDS_GPIO_Port, KBDS_Pin}, {KBDT_GPIO_Port, KBDT_Pin},
    {KBDU_GPIO_Port, KBDU_Pin}, {KBDV_GPIO_Port, KBDV_Pin},
    {KBDW_GPIO_Port, KBDW_Pin}, {KBDX_GPIO_Port, KBDX_Pin},
    {KBDY_GPIO_Port, KBDY_Pin}, {KBDZ_GPIO_Port, KBDZ_Pin},
  };
  for (int i = 0; i < 26; i++) {
    if (kbd_map[i].port == GPIOx && kbd_map[i].pin == GPIO_Pin) {
      return i;
    }
  }
  return -1;
}

/* Map (GPIOx, GPIO_Pin) to plugboard letter index 0..25, or -1 */
static int find_plug_pin_index(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
  static const struct {
    GPIO_TypeDef *port;
    uint16_t pin;
  } plug_map[26] = {
    {PlugA_GPIO_Port, PlugA_Pin}, {PlugB_GPIO_Port, PlugB_Pin},
    {PlugC_GPIO_Port, PlugC_Pin}, {PlugD_GPIO_Port, PlugD_Pin},
    {PlugE_GPIO_Port, PlugE_Pin}, {PlugF_GPIO_Port, PlugF_Pin},
    {PlugG_GPIO_Port, PlugG_Pin}, {PlugH_GPIO_Port, PlugH_Pin},
    {PlugI_GPIO_Port, PlugI_Pin}, {PlugJ_GPIO_Port, PlugJ_Pin},
    {PlugK_GPIO_Port, PlugK_Pin}, {PlugL_GPIO_Port, PlugL_Pin},
    {PlugM_GPIO_Port, PlugM_Pin}, {PlugN_GPIO_Port, PlugN_Pin},
    {PlugO_GPIO_Port, PlugO_Pin}, {PlugP_GPIO_Port, PlugP_Pin},
    {PlugQ_GPIO_Port, PlugQ_Pin}, {PlugR_GPIO_Port, PlugR_Pin},
    {PlugS_GPIO_Port, PlugS_Pin}, {PlugT_GPIO_Port, PlugT_Pin},
    {PlugU_GPIO_Port, PlugU_Pin}, {PlugV_GPIO_Port, PlugV_Pin},
    {PlugW_GPIO_Port, PlugW_Pin}, {PlugX_GPIO_Port, PlugX_Pin},
    {PlugY_GPIO_Port, PlugY_Pin}, {PlugZ_GPIO_Port, PlugZ_Pin},
  };
  for (int i = 0; i < 26; i++) {
    if (plug_map[i].port == GPIOx && plug_map[i].pin == GPIO_Pin) {
      return i;
    }
  }
  return -1;
}

/* ============================================================================
 * STM32L4xx HAL Implementations
 * ============================================================================ */

HAL_StatusTypeDef HAL_Init(void) { return HAL_OK; }

uint32_t HAL_GetTick(void) { return g_tick_ms; }

void HAL_Delay(uint32_t Delay) {
  uint32_t target = g_tick_ms + Delay;
  while ((int32_t)(target - g_tick_ms) > 0) {
    emu_yield_to_host();
  }
  g_latched_az_press = 0;
  nextFrame = 1;
}

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init) {
  if (!GPIO_Init) return;
  int plug_idx = find_plug_pin_index(GPIOx, (uint16_t)GPIO_Init->Pin);
  if (plug_idx >= 0) {
    g_plug_mode[plug_idx] = GPIO_Init->Mode;
    if (GPIO_Init->Mode == GPIO_MODE_INPUT) {
      g_plug_out[plug_idx] = GPIO_PIN_SET;
    }
  }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState) {
  int plug_idx = find_plug_pin_index(GPIOx, GPIO_Pin);
  if (plug_idx >= 0) {
    g_plug_out[plug_idx] = PinState;
  }
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
  (void)GPIOx;
  (void)GPIO_Pin;
}

uint32_t keyboardUpdate(void) {
  if (!g_in_reset_kb) {
    emu_wait_next_30hz_frame();
  }
  uint32_t ret = fw_keyboardUpdate();
  if (!g_in_reset_kb) {
    g_latched_az_press = 0;
    g_kbd_sampled_this_frame = 0;
  }
  nextFrame = 1;
  return ret;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
  if (GPIOx == TEST1_GPIO_Port && GPIO_Pin == TEST1_Pin) {
    /* TEST1 (PA15) has GPIO_PULLDOWN in gpio.c:130 and is low unless a factory test jumper is fitted */
    return GPIO_PIN_RESET;
  }
  int kbd_idx = find_kbd_pin_index(GPIOx, GPIO_Pin);
  if (kbd_idx >= 0) {
    g_kbd_sampled_this_frame = 1;
    uint32_t mask = effective_az_mask();
    /* Active-low switches (GPIO_PULLUP: pressed = GPIO_PIN_RESET, released = GPIO_PIN_SET) */
    return ((mask >> kbd_idx) & 1U) ? GPIO_PIN_RESET : GPIO_PIN_SET;
  }

  int plug_idx = find_plug_pin_index(GPIOx, GPIO_Pin);
  if (plug_idx >= 0) {
    if (g_plug_mode[plug_idx] != GPIO_MODE_INPUT && g_plug_out[plug_idx] == GPIO_PIN_RESET) {
      return GPIO_PIN_RESET;
    }
    int partner = g_plug_partner[plug_idx];
    if (partner >= 0 && partner < 26) {
      if (g_plug_mode[partner] != GPIO_MODE_INPUT && g_plug_out[partner] == GPIO_PIN_RESET) {
        return GPIO_PIN_RESET;
      }
    }
    return GPIO_PIN_SET;
  }

  return GPIO_PIN_SET;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout) {
  (void)Timeout;
  if (hspi == &hspi1 && pData && Size == 3072U) {
    render_matrix_to_fb(pData);
    if (g_kbd_sampled_this_frame) {
      g_latched_az_press = 0;
      g_kbd_sampled_this_frame = 0;
    }
    nextFrame = 1;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress,
                                    uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout) {
  (void)MemAddSize;
  (void)Timeout;
  if (hi2c == &hi2c1 && DevAddress == (0x50U << 1) && pData && Size >= 1U) {
    uint8_t reg = (uint8_t)(MemAddress & 0xFFU);
    uint8_t val = pData[0];
    if (reg == 0xFDU) {
      g_issi_page = val;
    } else if (g_issi_page == 0x01U) {
      g_issi_pwm[reg] = val;
      /* Flush when last lamp (ZBLU = 0x2A in lampboard_update or LBLU = 0xB2 in animations.c) is updated */
      if (reg == 0x2AU || reg == 0xB2U) {
        sync_lamps_from_issi();
      }
    }
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                          uint8_t *pData, uint16_t Size, uint32_t Timeout) {
  (void)hi2c;
  (void)DevAddress;
  (void)pData;
  (void)Size;
  (void)Timeout;
  /* No external I2C2 minibadge expander board attached */
  return HAL_ERROR;
}

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                         uint8_t *pData, uint16_t Size, uint32_t Timeout) {
  (void)hi2c;
  (void)DevAddress;
  if (pData && Size > 0) {
    memset(pData, 0, Size);
  }
  (void)Timeout;
  return HAL_ERROR;
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                        uint32_t Trials, uint32_t Timeout) {
  (void)hi2c;
  (void)DevAddress;
  (void)Trials;
  (void)Timeout;
  return HAL_ERROR;
}

void FPGA_Init(void) {}
void MX_USB_DEVICE_Init(void) {}

HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim) {
  if (htim == &htim16) {
    nextFrame = 1;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout) {
  (void)huart;
  (void)pData;
  (void)Size;
  (void)Timeout;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size) {
  (void)huart;
  (void)pData;
  (void)Size;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }

void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority) {
  (void)IRQn;
  (void)PreemptPriority;
  (void)SubPriority;
}
void HAL_NVIC_EnableIRQ(IRQn_Type IRQn) { (void)IRQn; }

HAL_StatusTypeDef HAL_PWR_ConfigPVD(PWR_PVDTypeDef *sConfigPVD) {
  (void)sConfigPVD;
  return HAL_OK;
}
void HAL_PWR_EnablePVD(void) {}
HAL_StatusTypeDef HAL_PWREx_ControlVoltageScaling(uint32_t VoltageScaling) {
  (void)VoltageScaling;
  return HAL_OK;
}

void HAL_SYSCFG_EnableVREFBUF(void) {}
void HAL_SYSCFG_VREFBUF_HighImpedanceConfig(uint32_t Mode) { (void)Mode; }

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct) {
  (void)RCC_OscInitStruct;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *RCC_ClkInitStruct, uint32_t FLatency) {
  (void)RCC_ClkInitStruct;
  (void)FLatency;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *PeriphClkInit) {
  (void)PeriphClkInit;
  return HAL_OK;
}

/* ============================================================================
 * Emulated EEPROM (eeprom_emul.h) backed by g_eeprom[512]
 * ============================================================================ */

static int virt_to_slot(uint16_t VirtAddress) {
  if (VirtAddress == 1U) {
    return 1; /* Raw EEP_CHALLENGE_LEVEL in CheckSerial() */
  }
  if (VirtAddress >= 11U && (VirtAddress % 10U) == 1U) {
    uint16_t idx = (uint16_t)((VirtAddress - 1U) / 10U);
    if (idx < NB_OF_VARIABLES) {
      return (int)idx;
    }
  }
  if (VirtAddress < NB_OF_VARIABLES) {
    return (int)VirtAddress;
  }
  return -1;
}

static int eeprom_slot_is_valid(int slot) {
  if (slot < 0 || slot >= (int)NB_OF_VARIABLES) return 0;
  const uint8_t *buf = g_in_factory_test ? g_factory_test_eeprom : g_eeprom;
  return (buf[320 + (slot >> 3)] >> (slot & 7)) & 1;
}

static void eeprom_slot_set(int slot, uint8_t val) {
  if (slot < 0 || slot >= (int)NB_OF_VARIABLES) return;
  uint8_t *buf = g_in_factory_test ? g_factory_test_eeprom : g_eeprom;
  uint8_t valid_bit = (uint8_t)(1U << (slot & 7));
  int was_valid = (buf[320 + (slot >> 3)] & valid_bit) != 0;
  if (!was_valid || buf[slot] != val) {
    buf[slot] = val;
    buf[320 + (slot >> 3)] |= valid_bit;
    if (!g_in_factory_test) {
      g_eeprom_dirty = 1;
    }
  }
}

static void eeprom_slot_clear(int slot) {
  if (slot < 0 || slot >= (int)NB_OF_VARIABLES) return;
  uint8_t *buf = g_in_factory_test ? g_factory_test_eeprom : g_eeprom;
  buf[slot] = 0xFFU;
  buf[320 + (slot >> 3)] &= (uint8_t)~(1U << (slot & 7));
  if (!g_in_factory_test) {
    g_eeprom_dirty = 1;
  }
}

static int eeprom_has_magic(void) {
  return g_eeprom[508] == 'S' && g_eeprom[509] == 'C' &&
         g_eeprom[510] == '1' && g_eeprom[511] == '9';
}

static void eeprom_ensure_initialized(void) {
  if (!eeprom_has_magic()) {
    memset(g_eeprom, 0, sizeof(g_eeprom));
    g_eeprom[508] = 'S';
    g_eeprom[509] = 'C';
    g_eeprom[510] = '1';
    g_eeprom[511] = '9';
    /* Populate the post-TestMode() EEPROM values using the firmware's own layout
     * and loop order (testmode.c:48-62) so a fresh save starts past the factory key test. */
    int saved_ft = g_in_factory_test;
    g_in_factory_test = 0;
    eeprom_slot_set(EEP_CHALLENGE_LEVEL, 0);
    for (int x = 0; x < 6; x++) {
      eeprom_slot_set(EEP_MESSAGE_CODE_1 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_2 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_3 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_4 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_5 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_6 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_7 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_8 + x, (uint8_t)(rand() % 26));
      eeprom_slot_set(EEP_MESSAGE_CODE_9 + x, (uint8_t)(rand() % 26));
    }
    g_in_factory_test = saved_ft;
    g_eeprom_dirty = 1; /* Save the fresh codes at once so a reload keeps them */
  }
}

EE_Status EE_Init(uint16_t *VirtAddTab, EE_Erase_type EraseType) {
  (void)VirtAddTab;
  (void)EraseType;
  eeprom_ensure_initialized();
  return EE_OK;
}

EE_Status EE_Format(EE_Erase_type EraseType) {
  (void)EraseType;
  if (g_in_factory_test) {
    memset(g_factory_test_eeprom, 0, sizeof(g_factory_test_eeprom));
  } else {
    memset(g_eeprom, 0, sizeof(g_eeprom));
    eeprom_ensure_initialized();
  }
  return EE_OK;
}

EE_Status EE_ReadVariable8bits(uint16_t VirtAddress, uint8_t *pData) {
  eeprom_ensure_initialized();
  int slot = virt_to_slot(VirtAddress);
  if (g_in_factory_test && g_factory_test_boot_pending && slot == EEP_MESSAGE_CODE_1) {
    /* Trigger TestMode() at main.c:296 while keeping all factory-test writes in g_factory_test_eeprom */
    g_factory_test_boot_pending = 0;
    return EE_NO_DATA;
  }
  if (slot < 0 || !eeprom_slot_is_valid(slot)) {
    return EE_NO_DATA;
  }
  if (pData) {
    const uint8_t *buf = g_in_factory_test ? g_factory_test_eeprom : g_eeprom;
    *pData = buf[slot];
  }
  return EE_OK;
}

EE_Status EE_WriteVariable8bits(uint16_t VirtAddress, uint8_t Data) {
  eeprom_ensure_initialized();
  int slot = virt_to_slot(VirtAddress);
  if (slot < 0) {
    return EE_INVALID_VIRTUALADDRESS;
  }
  eeprom_slot_set(slot, Data);
  return EE_OK;
}

EE_Status EE_ReadVariable16bits(uint16_t VirtAddress, uint16_t *pData) {
  uint8_t v = 0;
  EE_Status st = EE_ReadVariable8bits(VirtAddress, &v);
  if (st == EE_OK && pData) *pData = v;
  return st;
}

EE_Status EE_WriteVariable16bits(uint16_t VirtAddress, uint16_t Data) {
  return EE_WriteVariable8bits(VirtAddress, (uint8_t)(Data & 0xFFU));
}

EE_Status EE_ReadVariable32bits(uint16_t VirtAddress, uint32_t *pData) {
  uint8_t v = 0;
  EE_Status st = EE_ReadVariable8bits(VirtAddress, &v);
  if (st == EE_OK && pData) *pData = v;
  return st;
}

EE_Status EE_WriteVariable32bits(uint16_t VirtAddress, uint32_t Data) {
  return EE_WriteVariable8bits(VirtAddress, (uint8_t)(Data & 0xFFU));
}

EE_Status EE_CleanUp(void) { return EE_OK; }
EE_Status EE_CleanUp_IT(void) { return EE_OK; }
EE_Status EE_DeleteCorruptedFlashAddress(uint32_t Address) {
  (void)Address;
  return EE_OK;
}

/* ============================================================================
 * Fiber Entry & Public Emulator API
 * ============================================================================ */

/* Track when the firmware is inside enigma_menu() (and its child enigma_settings())
 * so the web UI only displays the "Hold 3s = Back" hint on screens that honour it. */
void enigma_menu(void) {
  g_in_enigma_menu = 1;
  fw_enigma_menu();
  g_in_enigma_menu = 0;
}

static void fw_fiber_entry(void *arg) {
  (void)arg;
  badge_2019_fw_main();
  for (;;) {
    emu_yield_to_host();
  }
}

#ifndef __EMSCRIPTEN__
static void fw_ucontext_trampoline(void) {
  fw_fiber_entry(NULL);
}
#endif

static void start_or_restart_fiber(void) {
  /* Reset firmware mutable globals before starting main() */
  nextFrame = 1;
  badgeLevel = 1;
  if (!g_in_factory_test && eeprom_slot_is_valid(EEP_CHALLENGE_LEVEL)) {
    uint8_t saved_lvl = g_eeprom[EEP_CHALLENGE_LEVEL];
    if (saved_lvl > 0) {
      badgeLevel = saved_lvl;
    }
  }
  menuState = 0;
  enigma_menuState = 0;
  g_in_enigma_menu = 0;
  custommessagelength = 0;
  custommessagelengthpixels = 0;
  newchar = 65;
  cursorposition = 0;
  framecounter = 0;
  numMinibadgeBoards = 0;
  minibadgeState = 0;
  numMinibadgeAddr = 0;
  memset(minibadgeMatrix, 0, sizeof(minibadgeMatrix));
  g_host_steps_accum = 0;
  g_30hz_msec_accum = 0;
  g_30hz_budget = 1;
  g_prev_raw_az = 0;
  g_latched_az_press = 0;
  g_kbd_sampled_this_frame = 0;

  memset(g_issi_pwm, 0, sizeof(g_issi_pwm));
  memset(g_leds, 0, sizeof(g_leds));

  uint8_t blank_rgb[EMU_2019_MATRIX_W * EMU_2019_MATRIX_H * 3];
  memset(blank_rgb, 0, sizeof(blank_rgb));
  render_matrix_to_fb(blank_rgb);

  /* Clear static button hold counters in keyboard.c by polling once with 0 keys */
  uint32_t saved_az = g_az_keys_mask;
  uint8_t saved_nav = g_nav_buttons_mask;
  g_az_keys_mask = 0;
  g_nav_buttons_mask = 0;
  g_in_reset_kb = 1;
  (void)keyboardUpdate();
  g_in_reset_kb = 0;
  g_az_keys_mask = saved_az;
  g_nav_buttons_mask = saved_nav;

  for (int i = 0; i < 26; i++) {
    g_plug_mode[i] = GPIO_MODE_INPUT;
    g_plug_out[i] = GPIO_PIN_SET;
  }

#ifdef __EMSCRIPTEN__
  if (!g_host_fiber_inited) {
    emscripten_fiber_init_from_current_context(&g_host_fiber, g_main_async_stack, sizeof(g_main_async_stack));
    g_host_fiber_inited = 1;
  }
  emscripten_fiber_init(&g_fw_fiber, fw_fiber_entry, NULL,
                        g_c_stack, sizeof(g_c_stack),
                        g_async_stack, sizeof(g_async_stack));
#else
  getcontext(&g_fw_ctx);
  g_fw_ctx.uc_stack.ss_sp = g_c_stack;
  g_fw_ctx.uc_stack.ss_size = sizeof(g_c_stack);
  g_fw_ctx.uc_link = &g_host_ctx;
  makecontext(&g_fw_ctx, fw_ucontext_trampoline, 0);
#endif
  g_fiber_started = 1;
}

EMSCRIPTEN_KEEPALIVE
void emu_init(uint32_t seed) {
  if (seed == 0U) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
      seed = (uint32_t)(ts.tv_sec ^ ts.tv_nsec ^ (uintptr_t)&ts);
    } else {
      seed = (uint32_t)time(NULL);
    }
    if (seed == 0U) seed = 1U;
  }
  srand(seed);
  g_in_factory_test = 0;
  g_factory_test_boot_pending = 0;
  g_az_keys_mask = 0;
  g_nav_buttons_mask = 0;
  g_tick_ms = 0;
  eeprom_ensure_initialized();
  start_or_restart_fiber();
}

EMSCRIPTEN_KEEPALIVE
void emu_reset(void) {
  g_in_factory_test = 0;
  g_factory_test_boot_pending = 0;
  eeprom_ensure_initialized();
  start_or_restart_fiber();
}

EMSCRIPTEN_KEEPALIVE
void emu_step(uint32_t elapsed_ms, uint32_t buttons_mask) {
  if (!g_fiber_started) {
    emu_init(0x20195343U);
  }
  g_nav_buttons_mask = (uint8_t)(buttons_mask & 0xFFU);
  latch_rising_az_keys();

  /* Advance virtual clock and grant 30 Hz TIM16 frame ticks (~33.33 ms per frame). */
  uint32_t dt = (elapsed_ms > 0U && elapsed_ms <= 250U)
                    ? (elapsed_ms * g_speed_mult)
                    : ((g_host_steps_accum & 1) ? 17U : 16U);
  g_host_steps_accum++;
  g_tick_ms += dt;
  g_30hz_msec_accum += dt;
  if (g_30hz_msec_accum >= 33U || (elapsed_ms == 0U && (g_host_steps_accum & 1) == 1)) {
    g_30hz_budget = 1;
    g_30hz_msec_accum = (g_30hz_msec_accum >= 33U) ? (g_30hz_msec_accum - 33U) : 0U;
  }

#ifdef __EMSCRIPTEN__
  emscripten_fiber_swap(&g_host_fiber, &g_fw_fiber);
#else
  swapcontext(&g_host_ctx, &g_fw_ctx);
#endif
}

EMSCRIPTEN_KEEPALIVE
void emu_set_speed(uint32_t multiplier) {
  g_speed_mult = (multiplier >= 1U && multiplier <= 600U) ? multiplier : 1U;
}

EMSCRIPTEN_KEEPALIVE
const uint16_t *emu_framebuffer(void) { return g_fb; }

EMSCRIPTEN_KEEPALIVE
int emu_fb_width(void) { return EMU_2019_FB_W; }

EMSCRIPTEN_KEEPALIVE
int emu_fb_height(void) { return EMU_2019_FB_H; }

EMSCRIPTEN_KEEPALIVE
const uint8_t *emu_leds(void) {
  sync_lamps_from_issi();
  return g_leds;
}

EMSCRIPTEN_KEEPALIVE
int emu_led_count(void) { return EMU_2019_LED_COUNT; }

EMSCRIPTEN_KEEPALIVE
uint8_t emu_backlight(void) { return 255U; }

EMSCRIPTEN_KEEPALIVE
uint8_t *emu_eeprom(void) { return g_eeprom; }

EMSCRIPTEN_KEEPALIVE
int emu_eeprom_size(void) { return EMU_2019_EEPROM_SZ; }

EMSCRIPTEN_KEEPALIVE
int emu_eeprom_dirty(void) {
  int d = g_eeprom_dirty;
  g_eeprom_dirty = 0;
  return d;
}

EMSCRIPTEN_KEEPALIVE
void emu_eeprom_clear_dirty(void) { g_eeprom_dirty = 0; }

/* ============================================================================
 * 2019-Specific Exports (26-Key Keyboard, Plugboard Wires, Factory Test)
 * ============================================================================ */

EMSCRIPTEN_KEEPALIVE
void emu_2019_set_keys(uint32_t az_mask) {
  g_az_keys_mask = az_mask & 0x03FFFFFFU;
  latch_rising_az_keys();
}

EMSCRIPTEN_KEEPALIVE
uint32_t emu_2019_get_keys(void) {
  return raw_az_mask();
}

EMSCRIPTEN_KEEPALIVE
void emu_2019_plugboard_clear(void) {
  for (int i = 0; i < 26; i++) {
    g_plug_partner[i] = -1;
  }
}

EMSCRIPTEN_KEEPALIVE
int emu_2019_plugboard_connect(int a, int b) {
  if (a < 0 || a >= 26 || b < 0 || b >= 26 || a == b) {
    return -1;
  }
  /* Disconnect any existing wire on a or b first */
  if (g_plug_partner[a] >= 0) {
    int old_b = g_plug_partner[a];
    if (old_b >= 0 && old_b < 26) g_plug_partner[old_b] = -1;
    g_plug_partner[a] = -1;
  }
  if (g_plug_partner[b] >= 0) {
    int old_a = g_plug_partner[b];
    if (old_a >= 0 && old_a < 26) g_plug_partner[old_a] = -1;
    g_plug_partner[b] = -1;
  }
  g_plug_partner[a] = (int8_t)b;
  g_plug_partner[b] = (int8_t)a;
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_2019_plugboard_disconnect(int a) {
  if (a < 0 || a >= 26) return -1;
  int b = g_plug_partner[a];
  if (b >= 0 && b < 26) {
    g_plug_partner[b] = -1;
  }
  g_plug_partner[a] = -1;
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_2019_plugboard_get_partner(int a) {
  if (a < 0 || a >= 26) return -1;
  return (int)g_plug_partner[a];
}

EMSCRIPTEN_KEEPALIVE
int emu_2019_plugboard_get_state(char *out26) {
  if (!out26) return 0;
  plugboardUpdate();
  memcpy(out26, plugboard, 26);
  int pairs = 0;
  for (int i = 0; i < 26; i++) {
    if (out26[i] > ('A' + i)) {
      pairs++;
    }
  }
  return pairs;
}

EMSCRIPTEN_KEEPALIVE
int emu_2019_get_badge_level(void) {
  return (int)badgeLevel;
}

EMSCRIPTEN_KEEPALIVE
int emu_2019_get_menu_state(void) {
  return (menuState & 0xFFFFFF) | (g_in_enigma_menu ? (1 << 24) : 0);
}

EMSCRIPTEN_KEEPALIVE
void emu_2019_start_factory_test(void) {
  eeprom_ensure_initialized();
  memcpy(g_factory_test_eeprom, g_eeprom, sizeof(g_eeprom));
  g_in_factory_test = 1;
  g_factory_test_boot_pending = 1;
  start_or_restart_fiber();
}
