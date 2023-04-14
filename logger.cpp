/**
 * @file logger.h
 * @author Keanight (hzh0602@gmail.com)
 * @brief A thread-safe and non-blocking logger via UART-DMA for STM32
 * @version 0.0.1
 * @date 2023-03-31
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the “Software”), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "logger.h"

Logger logger;

/**
 * @brief Increase a half-word atomically
 */
#define ATOMIC_INCH(VAL)                           \
    while (__STREXH(__LDREXH(&(VAL)) + 1, &(VAL))) \
        ;

/**
 * @brief Decrease a half-word atomically
 */
#define ATOMIC_DECH(VAL)                           \
    while (__STREXH(__LDREXH(&(VAL)) - 1, &(VAL))) \
        ;

/**
 * @brief Initialize the Logger with a uart handle
 *
 * @param huart A uart handle initialized as follows
 *
 * 1. The `USE_HAL_UART_REGISTER_CALLBACKS` macro in `stm32f4xx_hal_conf.h` should be defined to `1U`
 *
 * 2. Enable UARTx global interrupt
 *
 * 3. The DMA and related interrupt should be enabled for the TX pin of the huart with following settings
 *    - `Normal` mode
 *    - Peripheral Increment Address `Disabled` and Memory Increment Address `Enabled`
 *    - Use Fifo `Disable`
 *    - Set Data Width of both Peripheral and Memory to `Byte`
 */
void Logger::init(UART_HandleTypeDef* huart) {
    m_uart = huart;
    HAL_UART_RegisterCallback(m_uart, HAL_UART_TX_COMPLETE_CB_ID, transferCompletedCallback);
}

/**
 * @brief Enqueue a string into the send buffer
 * @note This method will be called in noth main thread and ISRs, so it has to be thread-safe
 */
void Logger::enqueue(char* str, uint16_t length) {

    ATOMIC_INCH(m_enqueue_guard);  // Start the enqueue progress

    uint16_t write_pos, new_write_pos;
    do {
        write_pos = __LDREXH(&m_write_pos);  // Other ISRs may change the m_write_pos at any time, so get a local copy

        // Check if there are enough spaces in the send buffer to enqueue the string. If yes, advance the m_write_pos by
        // length atomically and start to enqueue. Otherwise, leave the write_pos unchanged.
        new_write_pos = availableSpace(write_pos) >= length ? advancePos(write_pos, length) : write_pos;

    } while (__STREXH(new_write_pos, &m_write_pos));  // Set new write pos atomically

    if (write_pos != new_write_pos) {

        // There are enough spaces to enqueue the string, start to enqueue
        if (new_write_pos > write_pos) {
            // can be enqueued in a single loop
            for (uint16_t i = 0; i < length; i++) {
                m_send_buffer[write_pos++] = str[i];
            }
        } else {
            // need to be enqueued in two loops
            uint16_t i = 0, till_end_count = SEND_BUFFER_SIZE - write_pos;
            // fill to the end
            while (i < till_end_count) {
                m_send_buffer[write_pos++] = str[i++];
            }
            // fill the rest
            write_pos = 0;
            while (i < length) {
                m_send_buffer[write_pos++] = str[i++];
            }
        }
    } else {
        // The circular buffer is not big enough for this message, simply increase m_missed_count for debugging
        ATOMIC_INCH(m_missed_count);  // Increase the m_missed_count for debugging purpose
    }
    ATOMIC_DECH(m_enqueue_guard);     // Finish the enqueue progress

    process();                        // Try to start transfer if it's called in main loop
}

void Logger::process() {
    if (m_is_sending || isInISR()) {  // do nothing if dma has already been working or called in ISRs
        return;
    }
    startTransfer();  // Start send out data in the buffer
}

/**
 * @brief try to start transfer if any data is ready
 */
void Logger::startTransfer() {
    uint16_t send_pos = m_write_pos;

    // Ensure that all enqueue progresses have been finished before send_pos by checking m_enqueue_guard, if not,
    // try to start the transfer in next entry
    if (send_pos != m_read_pos && m_enqueue_guard == 0) {
        m_is_sending = true;
        if (send_pos > m_read_pos) {
            // the data to send is in a continuous region, send all data
            m_new_read_pos = send_pos;
            HAL_UART_Transmit_DMA(m_uart, (const uint8_t*)(&m_send_buffer[m_read_pos]), send_pos - m_read_pos);
        } else {
            // the data cross the end of the buffer. Send the data to the end, and the rest will be sent in next ISR
            m_new_read_pos = 0;
            HAL_UART_Transmit_DMA(m_uart, (const uint8_t*)(&m_send_buffer[m_read_pos]), SEND_BUFFER_SIZE - m_read_pos);
        }
        __HAL_DMA_DISABLE_IT(m_uart->hdmatx, DMA_IT_HT);  // Disable the half transfer interrupt
    } else {
        m_is_sending = false;                             // no more data to send
    }
}

/**
 * @brief uart transfer complete callback, need to be modified if there are mutiple logger instances
 */
void Logger::transferCompletedCallback(UART_HandleTypeDef* huart) {
    // Add other instances here if multiple loggers are used
    if (huart == logger.getUARTHandle()) {
        logger.m_read_pos = logger.m_new_read_pos;
        logger.startTransfer();
    }
}

/**
 * @brief Format a signed number
 */
uint16_t Logger::formatSignedNum(char* buf, int32_t val) {
    char* start = buf;
    if (val < 0) {
        val = -val;
        buf[0] = '-';
        ++buf;
    }
    uint16_t length = formatUnsignedNum(buf, (uint32_t)val);
    return buf + length - start;
}

/**
 * @brief Format an unsigned number
 */
uint16_t Logger::formatUnsignedNum(char* buf, uint32_t val) {
    uint8_t i = 0;
    do {
        buf[i++] = val % 10 + '0';
        val /= 10;
    } while (val);

    // swap
    uint8_t start = 0, end = i - 1;
    while (start < end) {
        char temp = buf[start];
        buf[start++] = buf[end];
        buf[end--] = temp;
    }
    return i;
}

/**
 * @brief Format a double number with 3 decimal places
 */
uint16_t Logger::formatDouble(char* buf, double val) {
    char* start = buf;
    if (val < 0) {
        *(buf++) = '-';
        val = -val;
    }

    double rounding = 0.5 * 0.001;  // For 3 decimal places
    val += rounding;

    uint32_t int_part = (uint32_t)val;
    double   remainder = val - int_part;
    uint16_t length = formatUnsignedNum(buf, int_part);
    buf += length;

    *(buf++) = '.';

    for (uint8_t i = 0; i < 3; i++) {
        remainder *= 10;
        *(buf++) = (char)remainder + '0';
        remainder -= (char)remainder;
    }
    return buf - start;
}

/**
 * @brief Copy a string(ends with '\0') and return the length
 * @return length of the string
 */
uint16_t Logger::_strcpy(char* des, const char* src) {
    uint16_t i = 0;
    while (src[i] != '\0') {
        des[i++] = src[i];
    }
    return i;
}