#ifndef __EEPROM_EMUL_H
#define __EEPROM_EMUL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of variables handled in emulated EEPROM.
 * Upstream eeprom_emul_conf.h set 260U while EEP_MESSAGE_CODE_9 + 5 reaches 311;
 * 320U ensures VirtAddVarTab[EEP_MESSAGE_CODE_1..9 + x] is always in-bounds. */
#define NB_OF_VARIABLES 320U

/* EEPROM variable map (from upstream Inc/eeprom_emul_conf.h) */
#define EEP_CHALLENGE_LEVEL       1   /* 1 byte */
#define EEP_CUSTOM_MESSAGE_LENGTH 2   /* Up to 255 */
#define EEP_CUSTOM_MESSAGE_START  3   /* 1-255 */
#define EEP_MINIBADGE_STATE       259 /* 1 byte */
#define EEP_MESSAGE_CODE_1        258 /* 6 bytes */
#define EEP_MESSAGE_CODE_2        264 /* 6 bytes */
#define EEP_MESSAGE_CODE_3        270 /* 6 bytes */
#define EEP_MESSAGE_CODE_4        276 /* 6 bytes */
#define EEP_MESSAGE_CODE_5        282 /* 6 bytes */
#define EEP_MESSAGE_CODE_6        288 /* 6 bytes */
#define EEP_MESSAGE_CODE_7        294 /* 6 bytes */
#define EEP_MESSAGE_CODE_8        300 /* 6 bytes */
#define EEP_MESSAGE_CODE_9        306 /* 6 bytes */

typedef enum {
  EE_OK = 0U,
  EE_ERASE_ERROR,
  EE_WRITE_ERROR,
  EE_ERROR_NOACTIVE_PAGE,
  EE_ERROR_NOERASE_PAGE,
  EE_ERROR_NOERASING_PAGE,
  EE_ERROR_NOACTIVE_NORECEIVE_NOVALID_PAGE,
  EE_NO_DATA,
  EE_INVALID_VIRTUALADDRESS,
  EE_INVALID_PAGE,
  EE_INVALID_PAGE_SEQUENCE,
  EE_INVALID_ELEMENT,
  EE_TRANSFER_ERROR,
  EE_DELETE_ERROR,
  EE_INVALID_BANK_CFG,
  EE_CLEANUP_REQUIRED = 0x100U
} EE_Status;

typedef enum {
  EE_FORCED_ERASE = 0U,
  EE_CONDITIONAL_ERASE
} EE_Erase_type;

EE_Status EE_Init(uint16_t *VirtAddTab, EE_Erase_type EraseType);
EE_Status EE_Format(EE_Erase_type EraseType);
EE_Status EE_ReadVariable8bits(uint16_t VirtAddress, uint8_t *pData);
EE_Status EE_WriteVariable8bits(uint16_t VirtAddress, uint8_t Data);
EE_Status EE_ReadVariable16bits(uint16_t VirtAddress, uint16_t *pData);
EE_Status EE_WriteVariable16bits(uint16_t VirtAddress, uint16_t Data);
EE_Status EE_ReadVariable32bits(uint16_t VirtAddress, uint32_t *pData);
EE_Status EE_WriteVariable32bits(uint16_t VirtAddress, uint32_t Data);
EE_Status EE_CleanUp(void);
EE_Status EE_CleanUp_IT(void);
EE_Status EE_DeleteCorruptedFlashAddress(uint32_t Address);

#ifdef __cplusplus
}
#endif

#endif /* __EEPROM_EMUL_H */
