# A thread-safe and non-blocking logger via UART-DMA for STM32

This is a thread-safe and non-blocking logger with a small footprint for STM32 application. It formats and enqueues all messages into an internal circular buffer in RAM first, and then send out all data via UART-DMA automatically. LDREX/STREX instructions are used to exclusively access the circular buffer, so it's safe to use this logger in ISRs without disabling interrupts and all log sentences always retain intact.

## Usage

1. Initialize a UART handle with following settings: 

    - The `USE_HAL_UART_REGISTER_CALLBACKS` macro in `stm32f4xx_hal_conf.h` should be defined to `1U`

    - UARTx global interrupt `Enabled`

    - The DMA stream of the TX pin of the huart should be configured with following settings
        - DMAx Streamx global interrupt `Enabled`
        - DMA `Normal` mode
        - Peripheral Increment Address `Disabled` and Memory Increment Address `Enabled`
        - Use Fifo `Disable`
        - Set Data Width of both Peripheral and Memory to `Byte`
        - 
2. Initialize the logger with the UART handle

``` c++
logger.init(&huartx)
```

3. Put the logger.process() into your main loop

## Example

``` c++
logger.warning("temperature is:", 3.14);
logger.logln(4, " is bigger than ", 3.14);
logger.log("current speed: ");
logger.log(35);
logger.log('\n');
```

## Output

```
Warning: temperature is:3.140
4 is bigger than 3.140
current speed: 35
```

## License

Under [MIT](https://opensource.org/license/mit/) LICENSE

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
