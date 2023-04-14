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

#pragma once

/* Note
This is a thread-safe and non-blocking logger with a small footprint STM32 application. It formats and logs all
messages into an internal circular send buffer first, and then send out all data via UART with DMA mode automatically.
LDREX/STREX instructions are used to exclusively access the shared buffer, so the interrupt is never disabled.

Usage:
This logger can log message in any number and in any order. If you need log your own message type, please overwrite the
formatSingle function

logger.log(Msg1, Msg2, Msg3, ...) //Message can be in any order and number
Examples:
logger.logln("The temperature is: ", float_value);
logger.info(string, " is not a valid command");
logger.warning("Sensor A: ", float_value, "Sensor B: ", int_value);
logger.error(uint_value, " is bigger than ", uint_value);
*/

#include "stm32f4xx_hal.h"

class Logger {

   public:
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
    void init(UART_HandleTypeDef* huart);

    /**
     * @brief Start transfer if any message is ready
     * @note Need to poll it frequently,  so put it into the main loop
     */
    void process();

    /**
     * @brief Format and Log data to the send buffer
     */
    template <typename... T>
    void log(T... args) {
        char line_buffer[SINGLE_MSG_SIZE];
        enqueue(line_buffer, formatMulti(line_buffer, args...));
    }

    /**
     * @brief Generate function with a header and an EOL
     */
#define GENERATE_FUNC(NAME)                                               \
    template <typename... T>                                              \
    void NAME(T... args) {                                                \
        char     line_buffer[SINGLE_MSG_SIZE];                            \
        uint16_t total_length = _strcpy(line_buffer, NAME##_str);         \
        total_length += formatMulti(&line_buffer[total_length], args...); \
        line_buffer[total_length++] = '\n';                               \
        assert_param(total_length <= SINGLE_MSG_SIZE);                    \
        enqueue(line_buffer, total_length);                               \
    }

    /**
     * @brief Generate function for Logger::logln(), Logger::info(), Logger::warning() and Logger::error()
     */
    GENERATE_FUNC(logln);
    GENERATE_FUNC(info);
    GENERATE_FUNC(warning);
    GENERATE_FUNC(error);

    /**
     * @brief Get the uart handle of the logger
     */
    UART_HandleTypeDef* getUARTHandle() {
        return m_uart;
    }

    /**
     * @brief Get the count of missed messages which are unable to be cached into the circular buffer
     * @note When missed_count is not 0, try to increase the [SEND_BUFFER_SIZE] or  the uart Baud rate
     */
    uint16_t getMissedCount() {
        return m_missed_count;
    }

   private:
    /**
     * @brief enqueue a formatted string to the circular buffer
     *
     * @param str formatted string
     * @param length length of the string
     */
    void enqueue(char* str, uint16_t length);

    /**
     *@brief Format multiple messages
     */
    template <typename T, typename... Ts>
    static uint16_t formatMulti(char* pos, T first, Ts... rest) {
        uint16_t length = formatSingle(pos, first);
        return length + formatMulti(pos + length, rest...);
    }
    /**
     * @brief formatMulti recursion end
     */
    static inline uint16_t formatMulti(char* pos) {
        return 0U;
    }

    /**
     * @brief Format a signed value
     */
    static inline uint16_t formatSingle(char* pos, int32_t value) {
        return formatSignedNum(pos, value);
    }
    static inline uint16_t formatSingle(char* pos, int16_t value) {
        return formatSignedNum(pos, value);
    }
    static inline uint16_t formatSingle(char* pos, int8_t value) {
        return formatSignedNum(pos, value);
    }
    static inline uint16_t formatSingle(char* pos, int value) {
        return formatSignedNum(pos, value);
    }

    /**
     * @brief Format an unsigned value
     */
    static inline uint16_t formatSingle(char* pos, uint32_t value) {
        return formatUnsignedNum(pos, value);
    }
    static inline uint16_t formatSingle(char* pos, uint16_t value) {
        return formatUnsignedNum(pos, value);
    }
    static inline uint16_t formatSingle(char* pos, uint8_t value) {
        return formatUnsignedNum(pos, value);
    }
    static inline uint16_t formatSingle(char* pos, unsigned int value) {
        return formatUnsignedNum(pos, value);
    }

    /**
     * @brief Format a float value
     */
    static inline uint16_t formatSingle(char* pos, float value) {
        return formatDouble(pos, value);
    }

    /**
     * @brief Format a double value
     */
    static inline uint16_t formatSingle(char* pos, double value) {
        return formatDouble(pos, value);
    }

    /**
     * @brief Format a string
     */
    static inline uint16_t formatSingle(char* pos, const char* str) {
        return _strcpy(pos, str);
    }

    /**
     * @brief Format a char
     */
    static inline uint16_t formatSingle(char* pos, char cha) {
        *pos = cha;
        return 1;
    }

    /**
     * @brief Start transfer to uart if any data is ready in the send buffer
     */
    void startTransfer();

    /**
     * @brief message transfer completed ISR
     */
    static void transferCompletedCallback(UART_HandleTypeDef* huart);

    /**
     * @brief Format an unsigned value
     * @return uint16_t length of formatted string
     */
    static uint16_t formatUnsignedNum(char* buf, uint32_t val);

    /**
     * @brief Format a signed value
     * @return uint16_t length of formatted string
     */
    static uint16_t formatSignedNum(char* buf, int32_t val);

    /**
     * @brief Format a decimal value
     * @return uint16_t length of formatted string
     */
    static uint16_t formatDouble(char* buf, double val);

    /**
     * @brief Copy a string(ends with '\0') and return the length
     * @return length of the string
     */
    static uint16_t _strcpy(char* des, const char* src);

    /**
     * @brief If caller is in an ISR
     */
    static inline bool isInISR() {
        return __get_IPSR() != 0;
    }

    /**
     * @brief Available space with write_pos in the send buffer
     */
    inline uint16_t availableSpace(uint16_t write_pos) {
        // one slot is always empty
        return write_pos >= m_read_pos ? SEND_BUFFER_SIZE - (write_pos - m_read_pos) - 1U : m_read_pos - write_pos - 1U;
    }

    /**
     * @brief Advance pos in the circular buffer
     */
    static inline uint16_t advancePos(uint16_t pos, uint16_t step = 1) {
        return (pos + step) % SEND_BUFFER_SIZE;
    }

   public:
    volatile uint16_t m_read_pos;      // the read pos of the circular buffer, only modified in DMA ISR
    volatile uint16_t m_new_read_pos;  // the new read_pos to be set

   private:
    static constexpr const char* logln_str = "";  // headers for different log function
    static constexpr const char* info_str = "Info: ";
    static constexpr const char* warning_str = "Warning: ";
    static constexpr const char* error_str = "Error: ";

    static constexpr uint16_t SEND_BUFFER_SIZE = 512U;  // The length of the send buffer
    static constexpr uint16_t SINGLE_MSG_SIZE = 256U;   // The maximum size of a single log message

    volatile char m_send_buffer[SEND_BUFFER_SIZE];      // all log data are cached here before send via uart
    volatile bool m_is_sending = 0U;                    // indicate if the dma is sending out the log data

    volatile uint16_t m_write_pos = 0U;                 // the write_pos can be modified by both main thread and ISRs
    volatile uint16_t m_enqueue_guard = 0U;             // When it's 0, all data have been enqueued and ready to send
    volatile uint16_t m_missed_count = 0U;              // A counter for missed messages when the send buffer is full

    UART_HandleTypeDef* m_uart;                         // Uart handle
};

extern Logger logger;