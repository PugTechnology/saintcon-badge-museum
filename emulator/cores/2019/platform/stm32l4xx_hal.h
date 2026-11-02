#ifndef __STM32L4XX_HAL_H
#define __STM32L4XX_HAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __IO volatile
#define HAL_MAX_DELAY 0xFFFFFFFFU
#define RESET 0U
#define SET   1U
#define READ_REG(REG) ((REG))

typedef enum {
  HAL_OK      = 0x00U,
  HAL_ERROR   = 0x01U,
  HAL_BUSY    = 0x02U,
  HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef enum {
  GPIO_PIN_RESET = 0U,
  GPIO_PIN_SET   = 1U
} GPIO_PinState;

typedef struct {
  uint32_t id;
} GPIO_TypeDef;

extern GPIO_TypeDef GPIOA_inst;
extern GPIO_TypeDef GPIOB_inst;
extern GPIO_TypeDef GPIOC_inst;
extern GPIO_TypeDef GPIOD_inst;
extern GPIO_TypeDef GPIOE_inst;
extern GPIO_TypeDef GPIOH_inst;

#define GPIOA (&GPIOA_inst)
#define GPIOB (&GPIOB_inst)
#define GPIOC (&GPIOC_inst)
#define GPIOD (&GPIOD_inst)
#define GPIOE (&GPIOE_inst)
#define GPIOH (&GPIOH_inst)

#define GPIO_PIN_0   ((uint16_t)0x0001U)
#define GPIO_PIN_1   ((uint16_t)0x0002U)
#define GPIO_PIN_2   ((uint16_t)0x0004U)
#define GPIO_PIN_3   ((uint16_t)0x0008U)
#define GPIO_PIN_4   ((uint16_t)0x0010U)
#define GPIO_PIN_5   ((uint16_t)0x0020U)
#define GPIO_PIN_6   ((uint16_t)0x0040U)
#define GPIO_PIN_7   ((uint16_t)0x0080U)
#define GPIO_PIN_8   ((uint16_t)0x0100U)
#define GPIO_PIN_9   ((uint16_t)0x0200U)
#define GPIO_PIN_10  ((uint16_t)0x0400U)
#define GPIO_PIN_11  ((uint16_t)0x0800U)
#define GPIO_PIN_12  ((uint16_t)0x1000U)
#define GPIO_PIN_13  ((uint16_t)0x2000U)
#define GPIO_PIN_14  ((uint16_t)0x4000U)
#define GPIO_PIN_15  ((uint16_t)0x8000U)
#define GPIO_PIN_All ((uint16_t)0xFFFFU)

#define GPIO_MODE_INPUT     0x00000000U
#define GPIO_MODE_OUTPUT_PP 0x00000001U
#define GPIO_MODE_OUTPUT_OD 0x00000011U

#define GPIO_NOPULL   0x00000000U
#define GPIO_PULLUP   0x00000001U
#define GPIO_PULLDOWN 0x00000002U

#define GPIO_SPEED_FREQ_LOW 0x00000000U

typedef struct {
  uint32_t Pin;
  uint32_t Mode;
  uint32_t Pull;
  uint32_t Speed;
  uint32_t Alternate;
} GPIO_InitTypeDef;

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

typedef struct {
  uint32_t ISR;
  uint32_t CR1;
  uint32_t CR3;
  uint32_t RDR;
} USART_TypeDef;

typedef struct { int id; } I2C_HandleTypeDef;
typedef struct { int id; } SPI_HandleTypeDef;
typedef struct { int id; } TIM_HandleTypeDef;
typedef struct {
  int id;
  USART_TypeDef *Instance;
} UART_HandleTypeDef;

#define USART_ISR_PE     (1U << 0)
#define USART_ISR_FE     (1U << 1)
#define USART_ISR_NE     (1U << 2)
#define USART_ISR_ORE    (1U << 3)
#define USART_ISR_RXNE   (1U << 5)
#define USART_CR1_RXNEIE (1U << 5)

#define UART_CLEAR_PEF   (1U << 0)
#define UART_CLEAR_FEF   (1U << 1)
#define UART_CLEAR_NEF   (1U << 2)
#define UART_CLEAR_OREF  (1U << 3)
#define UART_CLEAR_WUF   (1U << 20)
#define __HAL_UART_CLEAR_FLAG(huart, flag) ((void)(huart), (void)(flag))

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint32_t Trials, uint32_t Timeout);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);

HAL_StatusTypeDef HAL_Init(void);
void HAL_Delay(uint32_t Delay);
uint32_t HAL_GetTick(void);

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
#define FLASH_FLAG_OPTVERR 0U
#define __HAL_FLASH_CLEAR_FLAG(x) ((void)(x))

typedef enum {
  PVD_PVM_IRQn   = 1,
  FLASH_IRQn     = 4,
  EXTI15_10_IRQn = 40
} IRQn_Type;

void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority);
void HAL_NVIC_EnableIRQ(IRQn_Type IRQn);

typedef struct {
  uint32_t PVDLevel;
  uint32_t Mode;
} PWR_PVDTypeDef;

#define PWR_PVDLEVEL_0               0U
#define PWR_PVD_MODE_IT_RISING       0U
#define PWR_REGULATOR_VOLTAGE_SCALE1 1U
#define PWR_FLAG_SB                  0U
#define __HAL_PWR_GET_FLAG(flag)     (RESET)
#define __HAL_PWR_CLEAR_FLAG(flag)   ((void)(flag))

HAL_StatusTypeDef HAL_PWR_ConfigPVD(PWR_PVDTypeDef *sConfigPVD);
void HAL_PWR_EnablePVD(void);
HAL_StatusTypeDef HAL_PWREx_ControlVoltageScaling(uint32_t VoltageScaling);

#define SYSCFG_VREFBUF_HIGH_IMPEDANCE_DISABLE 0U
void HAL_SYSCFG_EnableVREFBUF(void);
void HAL_SYSCFG_VREFBUF_HighImpedanceConfig(uint32_t Mode);

#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)

typedef struct {
  uint32_t PLLState;
  uint32_t PLLSource;
  uint32_t PLLM;
  uint32_t PLLN;
  uint32_t PLLP;
  uint32_t PLLQ;
  uint32_t PLLR;
} RCC_PLLInitTypeDef;

typedef struct {
  uint32_t PLLSAI1Source;
  uint32_t PLLSAI1M;
  uint32_t PLLSAI1N;
  uint32_t PLLSAI1P;
  uint32_t PLLSAI1Q;
  uint32_t PLLSAI1R;
  uint32_t PLLSAI1ClockOut;
} RCC_PLLSAI1InitTypeDef;

typedef struct {
  uint32_t OscillatorType;
  uint32_t HSIState;
  uint32_t HSICalibrationValue;
  uint32_t HSI48State;
  uint32_t MSIState;
  uint32_t MSICalibrationValue;
  uint32_t MSIClockRange;
  RCC_PLLInitTypeDef PLL;
} RCC_OscInitTypeDef;

typedef struct {
  uint32_t ClockType;
  uint32_t SYSCLKSource;
  uint32_t AHBCLKDivider;
  uint32_t APB1CLKDivider;
  uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

typedef struct {
  uint32_t PeriphClockSelection;
  uint32_t Usart1ClockSelection;
  uint32_t Usart2ClockSelection;
  uint32_t I2c1ClockSelection;
  uint32_t I2c2ClockSelection;
  uint32_t UsbClockSelection;
  uint32_t RngClockSelection;
  RCC_PLLSAI1InitTypeDef PLLSAI1;
} RCC_PeriphCLKInitTypeDef;

#define RCC_OSCILLATORTYPE_HSI48       1U
#define RCC_OSCILLATORTYPE_HSI         2U
#define RCC_OSCILLATORTYPE_MSI         4U
#define RCC_HSI_ON                     1U
#define RCC_HSICALIBRATION_DEFAULT     0U
#define RCC_HSI48_ON                   1U
#define RCC_MSI_ON                     1U
#define RCC_MSICALIBRATION_DEFAULT     0U
#define RCC_MSIRANGE_6                 6U
#define RCC_PLL_ON                     1U
#define RCC_PLLSOURCE_HSI              1U
#define RCC_PLLSOURCE_MSI              2U
#define RCC_PLLP_DIV2                  2U
#define RCC_PLLP_DIV7                  7U
#define RCC_PLLQ_DIV2                  2U
#define RCC_PLLR_DIV2                  2U
#define RCC_PLLSAI1_48M2CLK            1U
#define RCC_CLOCKTYPE_HCLK             1U
#define RCC_CLOCKTYPE_SYSCLK           2U
#define RCC_CLOCKTYPE_PCLK1            4U
#define RCC_CLOCKTYPE_PCLK2            8U
#define RCC_SYSCLKSOURCE_PLLCLK        1U
#define RCC_SYSCLK_DIV1                1U
#define RCC_HCLK_DIV1                  1U
#define RCC_PERIPHCLK_USART1           1U
#define RCC_PERIPHCLK_USART2           2U
#define RCC_PERIPHCLK_I2C1             4U
#define RCC_PERIPHCLK_I2C2             8U
#define RCC_PERIPHCLK_USB              16U
#define RCC_PERIPHCLK_RNG              32U
#define RCC_USART1CLKSOURCE_PCLK2      0U
#define RCC_USART2CLKSOURCE_PCLK1      0U
#define RCC_I2C1CLKSOURCE_PCLK1        0U
#define RCC_I2C2CLKSOURCE_PCLK1        0U
#define RCC_USBCLKSOURCE_HSI48         0U
#define RCC_USBCLKSOURCE_PLLSAI1       1U
#define RCC_RNGCLKSOURCE_PLLSAI1       1U
#define FLASH_LATENCY_4                4U

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *RCC_ClkInitStruct, uint32_t FLatency);
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *PeriphClkInit);

/* Forward declarations for firmware functions called across TUs without headers,
 * ensuring exact WebAssembly (wasm-ld) call signatures. */
void FPGA_Init(void);
void InitMinibadges(void);
void scroll_message(char *message);
void displayAnimations(void);
void UpdateMinibadges(uint8_t allowInterruptions);
#define _UMB_GET_MACRO(_0, _1, NAME, ...) NAME
#define _UMB_0() UpdateMinibadges(0)
#define _UMB_1(x) UpdateMinibadges(x)
#define UpdateMinibadges(...) _UMB_GET_MACRO(_0, ##__VA_ARGS__, _UMB_1, _UMB_0)(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* __STM32L4XX_HAL_H */
