# Build

To compile project from root run:
```console
make
```
By default build type is debug and for release build type:
```console
make BUILD_TYPE=release
```
To run emulator you need CHIP-8 rom:
```console
 ./chip8emu "roms/Space Invaders [David Winter].ch8"
```
And it will look like this:

<br><img width="510" alt="image" src="https://github.com/masamonoke/chip-8-emu/assets/68110536/ad2f8e22-6755-42ce-8e06-56317d42dd75">

Project uses SDL2 as frontend and you need to specify where it is installed:
```console
make SDL_PATH=/opt/homebrew/Cellar/sdl2/2.28.3
```
