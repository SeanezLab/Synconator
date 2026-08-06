/*
 * crc.h
 *
 *  Created on: Dec 29, 2025
 *      Author: rdkee
 */

#ifndef INC_CRC_H_
#define INC_CRC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <main.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "structs.h"

// Two byte protocol
#define LEN_FIELD_BYTES 2
#define HEADER_BYTES 2 // This was 3?!
#define FOOTER_BYTES 2
#define CRC_BYTES 2
#define RX_BUF_LEN 50 // Modify this field if you expect to receive packets >50 bytes.


// Fill in Below for each new protocol ///////////////////////////////////////////////////////////////////////

extern uint16_t payload_length_key[]; // Fill out in source file. Array that tell the CRC packager how many bytes are used for each data field
extern char *payload_entries[]; // Fill out in source file. Human readable list of each entry (Not necessary for the CRC packager but helps me remember).
#define PAYLOAD_BYTES 11 // Total number of Payload Bytes (Be sure to calculate this correctly! Its the sum of payload_length_key)

// End of Fill out //////////////////////////////////////////////////////////////////////////////////////////

extern uint8_t rx_buffer[];
extern uint8_t compiled_payload[];
#define PAYLOAD_DATA_FIELDS sizeof(payload_length_key) / sizeof(payload_length_key[0]) // Does not need to be changed. Number of Payload Data Fields.
#define PKT_BYTES (HEADER_BYTES + LEN_FIELD_BYTES + PAYLOAD_BYTES + CRC_BYTES + FOOTER_BYTES)



void compile_data_sources(uint8_t input_count, ...); // helper function for compiling data from different memory locations to the compiled payload location
void crc_uart_send_data(const uint8_t* src, UART_HandleTypeDef* huart); // Send crc-packeted data over UART
void crc_uart_rcv_data(rdg_buf_struct* rdg_struct, uint16_t length); // Receive and handle crc_packeted data over UART



#ifdef __cplusplus
}
#endif

#endif /* INC_CRC_H_ */
