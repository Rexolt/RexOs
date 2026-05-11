#pragma once
#include <rexos/types.h>
#include <rexos/net.h>

/* Alapvető E1000 Regiszter Offszetek (MMIO) */
#define E1000_REG_CTRL      0x00000 /* Device Control */
#define E1000_REG_STATUS    0x00008 /* Device Status */
#define E1000_REG_EEPROM    0x00014 /* EEPROM Read */
#define E1000_REG_ICR       0x000C0 /* Interrupt Cause Read */
#define E1000_REG_IMS       0x000D0 /* Interrupt Mask Set/Read */
#define E1000_REG_IMC       0x000D8 /* Interrupt Mask Clear */
#define E1000_REG_RCTL      0x00100 /* Receive Control */
#define E1000_REG_TCTL      0x00400 /* Transmit Control */

/* MAC Address (Receive Address) */
#define E1000_REG_RAL       0x05400 /* Receive Address Low */
#define E1000_REG_RAH       0x05404 /* Receive Address High */

/* Transmit / Receive gyűrűk regiszterei */
#define E1000_REG_RDBAL     0x02800 /* RX Descriptor Base Low */
#define E1000_REG_RDBAH     0x02804 /* RX Descriptor Base High */
#define E1000_REG_RDLEN     0x02808 /* RX Descriptor Length */
#define E1000_REG_RDH       0x02810 /* RX Descriptor Head */
#define E1000_REG_RDT       0x02818 /* RX Descriptor Tail */

#define E1000_REG_TDBAL     0x03800 /* TX Descriptor Base Low */
#define E1000_REG_TDBAH     0x03804 /* TX Descriptor Base High */
#define E1000_REG_TDLEN     0x03808 /* TX Descriptor Length */
#define E1000_REG_TDH       0x03810 /* TX Descriptor Head */
#define E1000_REG_TDT       0x03818 /* TX Descriptor Tail */

/* Struktúra az RX/TX Deszkriptorokhoz */
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __packed e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __packed e1000_tx_desc_t;

/* Inicializáló függvény, amelyet a kmain hív meg */
bool e1000_init(void);
