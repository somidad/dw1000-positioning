# tagRPi

## State Diagram

![](../_state-diagram/tag.png)

## Prerequisite

### Enable I2C on Raspberry Pi

```sh
[sudo] raspi-config
# Enable I2C in Interfacing option
```

### libi2c-dev

```sh
[sudo] apt-get install libi2c-dev
```

### mlat

**mlat additionally requires Eigen3**

```sh
[sudo] apt-get install libeigen3-dev
git clone https://github.com/gsongsong/mlat
```
Edit `MLAT` in `Makefile` properly

## Building `tagRPi`

- Copy or link `def.h`, `i2c.h` from the parent directory to here
- Edit `I2CSLAVEADDR` in `i2c.h` if needed
- Edit `MLAT` in `Makefile` properly
- Edit `I2CDEV` in `i2cController.h` properly

```sh
make
```


## Running `tagRPi`

```sh
./tagRPi
```
