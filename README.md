# ESPboy_SystemBeeps
A chip music album - System Beeps 

by Shiru 12.12.2019

shiru@mail.ru

https://www.patreon.com/shiru8bit

My first project for ESPboy, in form of an Arduino IDE sketch. This is just a chip music album that plays a set of monophonic songs via built-in speaker, so not much useful as is, but it is a start.

What may come handy here is some code. Adafruit GFX coupled with the ST77xx display driver proved to be extremely slow. For one, text screen redraw took a good second to complete, with flicker, which is not acceptable by any means, and it wouldn’t allow any color images besides raw RGB565 that is just inconvenient to handle. So I figured out a few tricks that has been used to make it work reasonably fast and without flicker, as well as support regular 8-bit BMP files and draw them partially when needed (see drawBMP8Part, drawCharFast).
