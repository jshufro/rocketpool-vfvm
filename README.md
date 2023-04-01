# rocketpool-vfvm

`vfvm` is a very-fast-vanity-miner for Rocket Pool.

![wen poap](vfvm.png)

## Supported Platforms
Currently, `vfvm` works on x86_64 Linux systems. It is compiled on-host to take advantage of [Advanced Vector Extensions](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions) and will use AVX512 or AVX2 if available, with a fallback generic x64 implementation if not.

ARM64 and OSX support are under investigation.

## **WARNING**  
If you have generated `rocketpool.json` and/or pre-mined hashes, they may be antiquated by updates to Rocket Pool's smart contracts.
These contract updates are rare, but Atlas will be released on April 18th at 00:00 UTC. Any salts mined before this will be made invalid.

You will have to re-generate rocketpool.json (step 2) and re-mine your hashes (step 4) after each of these contract upgrades.

## Usage
`vfvm` does not need to run on the same machine as your Rocket Pool node, however, it does need to query the rocketpool_api container for some data before it can begin mining.

### 1. Clone the repository
On the machine you intend to do the mining, clone this repository:  
```bash
git clone https://github.com/jshufro/rocketpool-vfvm.git
```

### 2. Gather the vanity artifacts
**If you are going to be mining vanity salts on your node, simply run:**
```bash
./rocketpool.py > rocketpool.json
```
**If you are going to be mining vanity salts on a separate machine, you need to collect the artifacts from your node.**

SSH into your node and run:
```bash
wget https://raw.githubusercontent.com/jshufro/rocketpool-vfvm/master/rocketpool.py
chmod u+x rocketpool.py
./rocketpool.py > rocketpool.json
```
This will create a file `rocketpool.json` which must be copied to the mining machine and placed in the `rocketpool-vfvm/` directory.

Once this is done, feel free to delete the downloaded/generated files on your node:
```bash
rm rocketpool.py rocketpool.json
```

### 3. Build vfvm
First, run 
```bash
git submodule update --init
```
to clone [XKCP](https://github.com/XKCP/XKCP).  
Install dependencies with:  
```bash
sudo apt update
sudo apt install xsltproc libjson-c-dev make gcc
```
Build the project with:
```bash
make
```

### 4. Mine a vanity hash
Begin mining with:
```bash
./mine [prefix] [optional starting salt]
```

Note that the salt cannot exceed UINT64_MAX (i.e. 0xffffffffffffffff) due to internal limitations, so pick a starting salt well below this value. It's recommended to start from nothing for all new prefix searches.

## Performance
In testing, I found the following speedups over the embedded Rocket Pool vanity miner:
* generic64: 20%
* avx2: 100%
* avx512: 1000%

On a AWS c5.4xlarge, the miner processes 72 million salts per second.  
On a c5.24xlarge, the miner processes 420 million salts per second.

These performance gains come from two optimizations:
1. No memory overhead. Mining does no allocations after initialization.
2. AVX vectorization. SIMD intrinsics can test up to 8 salts at a time, per AVX unit. Many intel processors have 2 units per core.

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

I do not own any OSX hardware, so portability contributions are appreciated.

## License
[MIT](LICENSE)
