# btc-hashrate

`btc-hashrate` is a C++20 command-line tool that estimates the average Bitcoin network hashrate over a user-specified number of days.

It uses `bitcoin-cli` to gather block header data and calculates hashrate based on average difficulty and time between blocks.

## Features

* Estimates hashrate using actual block header data
* Supports floating-point input for precise day ranges
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
./btc-hashrate [days]
```

* `days` – optional; floating-point number of days to measure. Defaults to 1.

### Example

```sh
./btc-hashrate 3.5
```

## Output

```
Days: 3.5
Block Height: 904143
Blocks: 504
Next Diff Adjustment In: 1040 Blocks
Expected Time: 3d:12h:0m:0s
Actual Time:   3d:4h:30m:2s
Averge Block Time: 9.11m
Median Block Time: 5.67m
Std Dev: 9.50m
Average Difficulty: 116,958,512,019,761.8
Estimated Hashrate: 919.3 EH/s
```

## License

MIT

