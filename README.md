# TS (timestamp standard input)

## Overview

This utility, a C reimplementation of the `ts` command from the
[`moreutils`](https://joeyh.name/code/moreutils/) package, is designed
to add a timestamp to the beginning of each line of input, enhancing
the readability and utility of log files or any streamed output. It
can also convert existing timestamps in the input to either absolute
or relative times, supporting a variety of common timestamp formats.

Please note that this repository does not currently offer binaries,
requiring users to build from source.

### Motivation

The primary motivation behind reimplementing the `ts` utility was to
eliminate the dependency on Perl and its associated packages. This
decision was driven by the goal of simplifying deployment, especially
in containerised or constrained environments where minimising the
footprint is essential. By removing the need for Perl, the utility
becomes more lightweight and easier to integrate into various systems
without the overhead of managing additional dependencies.

While this version eliminates the dependency on Perl and its
associated packages, it requires the [PCRE](https://www.pcre.org/)
library for regular expression support, both at build and runtime.

I also wanted a small project where I could experiment with building a
fat binary using [cosmopolitan](https://github.com/jart/cosmopolitan)
to further simplify distribution. The goal is to build `ts` just once
and copy it into any container without having to consider whether the
PCRE library is present.

## Synopsis

```plaintext
ts [-r] [-i | -s] [-m] [-p <precision>] [format]
```

By default, `ts` adds a timestamp to each line using the format `%b %d
%H:%M:%S`. Users can specify a custom format, which adheres to the
conventions used by `strftime(3)`, with extensions for microsecond
resolution using the specifiers `%.S`, `%.s`, or `%.T`.

- **Relative Time Conversion (`-r`)**: When used, `ts` converts
  existing timestamps within the input into relative times (e.g.,
  "15m5s ago"), automatically detecting and supporting many common
  timestamp formats. If a custom output format is also specified with
  `-r`, `ts` will use it for the time conversion.

- **Incremental Timestamps**: The `-i` and `-s` flags alter the
  utility's behaviour to report timestamps incrementally:
    - **`-i`**: Each timestamp represents the time elapsed since the last timestamp.
    - **`-s`**: Timestamps represent the time elapsed since the start of the `ts` command execution. The default format for incremental timestamps is `%H:%M:%S`.

- **Monotonic Clock (`-m`)**: Opting for this flag makes `ts` use the
  system's monotonic clock, ensuring that the timestamps are not
  affected by changes in the system clock.

- **Precision (`-p`)**: This version of `ts` introduces the `-p`
option as an extension to the `moreutils` version of ts, providing
users with the ability to specify the precision of time units in the
output when using the `-r` flag for relative time differences. Valid
precision levels are 0 to 5 inclusive and the default precision level
is 2.

The `TZ` environment variable is respected, influencing the timezone
used for timestamps when not explicitly included in the timestamp's
format.

### Examples

```bash
# Basic timestamping.
$ echo "Log entry" | ts
Feb 05 21:00:42 Log entry

# Custom format.
$ echo "Another log entry" | ts "%Y-%m-%d %H:%M:%S"
2024-02-05 21:00:52 Another log entry

# Relative times.
$ cat log.txt | ts -r
27d4h ago host syslogd[441]: Statistics

# Incremental timestamps since last event.
$ echo -e "foo\nbar\nbaz" | while IFS= read -r line; do echo "$line"; sleep 2; done | ts -i
00:00:00 foo
00:00:02 bar
00:00:02 baz

# Incremental timestamps since start.
$ echo -e "foo\nbar\nbaz" | while IFS= read -r line; do echo "$line"; sleep 2; done | ts -s
00:00:00 foo
00:00:02 bar
00:00:04 baz

# Custom format using the monotonic clock.
$ echo "Process started" | ts -m '%FT%.T'
2024-02-05T21:13:36.848360 Process started

# High precision timestamping.

$ echo "High precision event" | ts "%Y-%m-%d %H:%M:%.S"
2024-02-05 22:50:40.383986 High precision event

$ echo "High precision event" | ts "%F %.T"
2024-02-05 22:50:54.140320 High precision event

$ echo "High precision event" | ts -m "%.s"
1707173461.672750 High precision event

$ echo -e "foo\nbar\nbaz" | while IFS= read -r line; do echo "$line"; sleep 2; done | ts -m -s %.s
0.130000 foo
2.172200 bar
4.608700 baz
```

## Understanding Relative Timestamps

The `-r` flag in the `ts` utility transforms absolute timestamps into
relative terms, simplifying the comprehension of when events occurred.
This transformation includes recognising timestamps through pattern
matching, parsing them into component units, and approximating the
time difference in a user-friendly format. The process emphasises
focusing on non-zero units, employing rounding where applicable, and
managing rollovers, ensuring the output is precise yet easily
understandable.

### Pattern Matching and Parsing

When `ts` encounters a timestamp in the input, it matches it against
predefined patterns. A successful match leads to the decomposition of
the timestamp into its constituent units: years, days, hours, minutes,
and seconds. This breakdown is crucial for calculating the relative
time difference. Notably absent is the month unit, omitted due to its
variable length complicating uniform calculations. This decision
streamlines approximation processes, focusing on fixed-length units
for consistency and simplicity in conveying time spans.

### Approximation: Focusing on Non-Zero Units

The approximation step calculates the time difference between the
current moment and the parsed timestamp, adjusting this difference to
highlight the most significant, relevant units:

- **Focusing on Non-Zero Units**: Initially, `ts` identifies units
  with non-zero values, prioritising them to ensure the approximation
  highlights actual time differences rather than zero-valued units.
  This focus is essential for clarity.

- **Rollovers**: Rollover handling adjusts time units exceeding their
  conventional maximum (e.g., 60 minutes) to the next higher unit
  (e.g., converting 70 minutes to 1 hour and 10 minutes), maintaining
  consistency with standard time measurement practices.

- **Rounding Up**: For time units halfway or more towards their
  maximum, `ts` will round up to simplify the representation, such as
  approximating 1 hour and 30 minutes to "2 hours ago." This rounding
  provides a simplified yet meaningful estimate of elapsed time,
  particularly when minute accuracy is less important than conveying a
  general sense of duration.

### Approximation: User Defined Precision

Precision, defined by a range from 0 to 5, determines the detail level
in the timestamp approximation, affecting the output's granularity and
specificity. Understanding precision's role is crucial for effectively
using `ts` with relative timestamping.

### Precision Levels Defined

- **High Precision (4, 5)**: Ideal for detailed logging or tracking,
  necessary for forensic analysis or debugging.
- **Medium Precision (2, 3)**: Balances detail and readability, suited
  for operational logs or summarising activities.
- **Low Precision (0, 1)**: Best for overviews or when event
  occurrence is more important than exact timing.

### Examples

- **Example 1**: For "1 year, 15 days, 0 hours, 20 minutes," with
  precision 2, `ts` would present "1 year and 15 days ago," omitting
  less significant units.

- **Example 2**: A difference of "0 years, 0 days, 23 hours, 45
  minutes," with precision 1, simplifies to "24 hours ago" due to
  rounding up the substantial hour count.

- **Example 3**: Given a timestamp resulting in "2 years, 11 months,
  30 days, 5 hours," with precision set to 3, `ts` would focus on "2
  years, 11 months, and 30 days ago," ignoring hours to maintain the
  specified precision level. This example highlights how `ts` can
  condense information to the most relevant units, providing a clear
  yet detailed view of the time elapsed for scenarios requiring a
  balance between detail and overview.

- **Example 4**: For a time difference of "0 years, 0 days, 2 hours,
  15 minutes, 30 seconds," with precision set to 2, `ts` simplifies
  this to "2 hours and 15 minutes ago," omitting seconds. This
  precision level is particularly useful for tracking short-term
  events or tasks where the minute is the smallest significant unit,
  offering a concise summary without losing important detail.

Adjusting precision allows tailoring `ts` output for specific
contexts, from detailed event logging to broad overviews of time
periods. The default precision level is 2.

## Combining Relative timestamps with a Custom Format

The `-r` flag used with a custom format enables precise customisation
of relative timestamp presentation according to `strftime` formatting
rules. For example, displaying a log entry as "Occurred on %Y-%m-%d at
%H:%M" instead of "3 hours and 45 minutes ago", applying the `-r` flag
with the desired custom format achieves this structured presentation
while preserving the relative nature of the timing.

## Monotonic clock

The `moreutils` ts implementation uses a clever approach to combine
the stability of the monotonic clock with the relevance of real-world
timestamps.

The monotonic clock (CLOCK_MONOTONIC) is used for measuring time
intervals. It is not affected by system time changes (like NTP
adjustments or DST shifts), making it ideal for timing where
consistency is key. However, the monotonic clock does not represent
real-world time; it typically measures time since system boot.

To output meaningful timestamps while using the monotonic clock, the
program calculates a 'monodelta' at startup. This is the difference
between the current real-world time (CLOCK_REALTIME) and the current
monotonic time. By adding this delta to the monotonic timestamps, we
adjust them to align with the real-world time. This way, the
timestamps output by the program represent the actual time of day,
even though the timing is done using the monotonic clock.

## Building & Installation

Before compiling the `ts` utility, you must have the
[PCRE](https://www.pcre.org/) development library installed on your
system. The development library is commonly named `pcre-devel`,
`libpcre3-dev`, or a similar variant, depending on your operating
system and distribution.

```bash
git clone https://github.com/frobware/ts
cd ts
make INSTALL_BINDIR=$HOME/.local/bin install
```

### NixOS/Nix Support

This utility can be easily integrated into NixOS configurations or
consumed in Nix environments using Nix Flakes. Below is an example of
how to include the `ts` utility in your NixOS system or project.

```nix
{
  description = "Example Nix flake for integrating the `ts` utility into a NixOS configuration.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    ts-flake.url = "https://github.com/frobware/ts";
  };

  outputs = { self, nixpkgs, ts-flake, ... }@inputs:
  let
    pkgsWithOverlay = system: import nixpkgs {
      inherit system;
      overlays = [ ts-flake.overlays.default ];
    };

    buildNixOS = { system, modules }: let
      pkgs = pkgsWithOverlay system;
    in nixpkgs.lib.nixosSystem {
      inherit system modules pkgs;
    };
  in
  {
    nixosConfigurations.exampleHost = buildNixOS {
      system = "aarch64-linux";
      modules = [
        ({ pkgs, ... }: {
          environment.systemPackages = [ pkgs.ts ];
        })
      ];
    };
  };
}
```

## Licensing

The entire ts project, including both the source code now in C and the
manpage documentation, is licensed under the GNU General Public
License version 2 (GPLv2). This unified licensing approach ensures
full compliance with the original moreutils package's licensing terms,
from which the manpage documentation is verbatim copied, and aligns
the reimplemented `ts` utility under the same open-source license.
