# Glover Practice ROM

Tools for practicing Glover speedruns.

An Expansion Pak is required.

## Applying the patch

Get the patch from the [releases page](https://github.com/Eddie-Lack/Glover-Practice-Rom/releases)

Open the `.html` file in a browser and follow the instructions.
You can also apply the `.bps` file with any BPS patcher to the ROM in `.z64` byte order.

## Controls and features

Hold all four C buttons to open or close the practice menu. The wiki describes the controls and each menu:

- [Home](wiki/Home.md)
- [Controls](wiki/Controls.md)

## Thanks

- [naclomi](https://github.com/naclomi) the goat, figured out pretty much everything 
- The developers of N64 decompilation tools: [splat](https://github.com/ethteck/splat), [spimdisasm](https://github.com/Decompollaborate/spimdisasm), [asm-differ](https://github.com/simonlindholm/asm-differ) and [decomp-permuter](https://github.com/simonlindholm/decomp-permuter), [m2c](https://github.com/matt-kempster/m2c), and the [decompals build of GCC 2.7.2 for MIPS](https://github.com/decompals/mips-gcc-2.7.2)
- Shoutouts to the true og's, gz and usamune

## Building

On Ubuntu 24.04:

```sh
sudo apt install git python3 python3-venv build-essential gcc-multilib bison gperf binutils-mips-linux-gnu
git clone --recursive https://github.com/Eddie-Lack/Glover-Practice-Rom
cd Glover-Practice-Rom
make setup BASEROM=path/to/glover.z64   # needs your Glover (USA) ROM in .z64 byte order
sudo apt install gcc-mips-linux-gnu      # after make setup: it replaces gcc-multilib
make                                     # build/glover-practice.z64
make patch                               # build/glover-practice.bps and build/glover-practice.html
make release                             # on a version tag: build/release/ (docs/release.md)
```
