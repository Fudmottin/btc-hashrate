# btc-hashrate

`btc-hashrate` is a C++20 command-line tool that estimates the average Bitcoin network hashrate over a user-specified number of blocks.

It uses `bitcoin-cli` to gather block header data and calculates hashrate based on average difficulty and time between blocks.

## Features

* Estimates hashrate using actual block header data
* Displays readable summaries including block count, time intervals, difficulty, and hashrate

## Requirements

* `bitcoin-cli` (must be in `$PATH` and connected to a full node)
* C++20 compiler (Clang/GCC)
* Boost libraries (JSON)

## Build

```sh
mkdir build && cd build
cmake ..
make
```

## Usage

```sh
./btc-hashrate [blocks | start-end]
```

* `blocks` – optional; integer defaults to 144. Or `start-end` for starting block height and ending block height.

### Example

```sh
./btc-hashrate 432
```

## Output

```
btc-hashrate 432
Block Range: 943629 - 944060
Sampled Blocks: 432
Sampled Intervals: 431
Next Diff Adjustment In: 1444 Blocks
Expected Time: 2d:23h:50m:0s
Actual Time:   3d:1h:44m:37s
Average Block Time: 10.27m
Median Block Time:  8.08m
Std Dev:            9.84m
Average Difficulty: 138,966,872,071,213.2
Estimated Hashrate: 969.0 EH/s

Miners / Pools
  Foundry USA Pool         --   140 --   32.4%
  AntPool                  --    71 --   16.4%
  F2Pool                   --    51 --   11.8%
  ViaBTC                   --    45 --   10.4%
  SpiderPool               --    27 --    6.2%
  SECPOOL                  --    26 --    6.0%
  MARA Pool                --    20 --    4.6%
  Luxor                    --    14 --    3.2%
  Unknown                  --    11 --    2.5%
  Binance Pool             --    10 --    2.3%
  SBI Crypto               --     8 --    1.9%
  Braiins Pool             --     5 --    1.2%
  Ocean                    --     3 --    0.7%
  Ultimus                  --     1 --    0.2%
```

## License

MIT

