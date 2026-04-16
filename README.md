<h1>Bare-Metal Blinky</h1>
This is my attempt at making led-blinker on Nucleo-F401RE without using any major helper libraries like OpenOCD or advanced IDEs like STM32CubeIDE.  

Info about registers, peripherals and pinouts can be found on *STM32-F401x user manual*, *STM32-F401x datasheet*, *STM32 Nucleo-64 Boards user manual* and *Cortex-M4 Developer Guide*.  

Libraries used: ARM GNU Toolchain (for cross-platform compilation), MakeFile (building), and stlink-tools (to actually transfer .bin with st-link/V2 programmator).  

Huge props to https://github.com/cpq/bare-metal-programming-guide?tab=readme-ov-file
