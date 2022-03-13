## README.md to be expanded upon shortly

1. Clone repo
2. `cd` into repo path
3. run `git submodule update --init` to clone XKCP
3. `make rocketpool`
4. `./rocketpool > rocketpool.json`
5. `make`
6. `./mine [16 or 32] [prefix]`

you may have to install missing dependencies along the way (libjson-c-dev comes to mind)

on a machine with AVX2:
```
$ ./mine 16 0x00000001
Searching for 0x00000001 using 16 threads
At salt 0x04b34080... 5.00s (15.77M salts/sec)
At salt 0x09497f80... 10.00s (15.39M salts/sec)
At salt 0x0dfddd80... 15.00s (15.79M salts/sec)
At salt 0x12954300... 20.00s (15.41M salts/sec)
At salt 0x171c3780... 25.00s (15.19M salts/sec)
```

my NUC with AVX512 got over 40M salts/sec

