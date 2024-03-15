# TS (timestamp standard input)

## Overview

This utility, a C reimplementation of the `ts` command from the
[`moreutils`](https://joeyh.name/code/moreutils/) package, is designed
to add a timestamp to the beginning of each line of input, enhancing
the readability and utility of log files or any streamed output. It
can also convert existing timestamps in the input to either absolute
or relative times, supporting a variety of common timestamp formats.

Please note that this repository does not offer pre-built binaries,
requiring users to build from source.

### Motivation

The primary motivation behind reimplementing the `ts` utility was to
eliminate the dependency on Perl and its associated packages. This
decision was driven by the goal of simplifying deployment, especially
in containerised or constrained environments where minimising the
footprint is essential.

While this version eliminates the dependency on Perl and its
associated packages, it requires the [PCRE](https://www.pcre.org/)
library for regular expression support, both at build and runtime.

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
    - **`-s`**: Timestamps represent the time elapsed since the start of the `ts` command execution.

    The default format for incremental timestamps is `%H:%M:%S`.

- **Monotonic Clock (`-m`)**: Opting for this flag makes `ts` use the
  system's monotonic clock, ensuring that the timestamps are not
  affected by changes in the system clock.

- **Precision (`-p`)**: This version of `ts` introduces the `-p`
  option as an extension to the
  [`moreutils`](https://joeyh.name/code/moreutils/) version of `ts`,
  providing users with the ability to specify the precision of time
  units in the output when using the `-r` flag for relative time
  differences. Valid precision levels are 1 to 4 inclusive.

  The default precision level is 2.

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

The `-r` flag transforms absolute timestamps into relative terms,
simplifying the comprehension of when events occurred. This
transformation includes recognising timestamps through pattern
matching, parsing them into component units, and approximating the
time difference in a user-friendly format. The process emphasises
focusing on non-zero units, managing rollovers, and rounding where
applicable, ensuring the output is precise yet easily understandable.

### Pattern Matching and Parsing

When `ts` encounters a timestamp in the input, it matches it against
predefined patterns. A successful match leads to the decomposition of
the timestamp into its constituent units: years, days, hours, minutes,
and seconds. This breakdown is crucial for calculating the relative
time difference. Notably absent is the month unit, omitted due to its
variable length complicating uniform calculations. This decision
streamlines approximation, focusing on fixed-length units for
consistency and simplicity in conveying time spans.

### Approximation: Focusing on Non-Zero Units

The approximation step calculates the time difference between the
current moment and the parsed timestamp, adjusting this difference to
highlight the most significant, relevant units:

- **Focusing on Non-Zero Units:** Initially, `ts` identifies units
  with non-zero values, prioritising them to ensure the approximation
  highlights actual time differences rather than zero-valued units.
  This focus is essential for clarity.

- **Rollovers:** Rollover handling adjusts time units exceeding their
  conventional maximum (e.g., 60 minutes) to the next higher unit
  (e.g., converting 70 minutes to 1 hour and 10 minutes), maintaining
  consistency with standard time measurement practices.

- **Rounding Up:** For time units halfway or more towards their
  maximum, `ts` will round up to simplify the representation, such as
  approximating 1 hour and 30 minutes to "2 hours ago." This rounding
  provides a simplified yet meaningful estimate of elapsed time,
  particularly when minute accuracy is less important than conveying a
  general sense of duration.

### Approximation: User Defined Precision

Precision ranges from 1 to 4, and determines the detail level in the
timestamp approximation, affecting the output's granularity and
specificity. The default precision level is 2.

Precision Levels Defined:

- **Low Precision (1):** Best for overviews or when event occurrence
  is more important than exact timing. This level simplifies the
  timestamp to the single most significant non-zero unit.

- **Medium Precision (2, 3)**: Balances detail and readability, suited
  for operational logs or summarising activities. Precision 2 focuses
  on the two most significant non-zero units, while precision 3 allows
  for an additional layer of detail.

- **High Precision (4):** Ideal for detailed logging or tracking,
  necessary for forensic analysis or debugging. Retains all units up
  to the four most significant non-zero units, providing a detailed
  view without approximation.

## Combining Relative timestamps with a Custom Format

The `-r` flag used with a custom format enables precise customisation
of relative timestamp presentation according to `strftime` formatting
rules. For example, displaying a log entry as "Occurred on %Y-%m-%d at
%H:%M" instead of "3 hours and 45 minutes ago", applying the `-r` flag
with the desired custom format achieves this structured presentation
while preserving the relative nature of the timing.

## Monotonic clock

The [`moreutils`](https://joeyh.name/code/moreutils/) `ts`
implementation uses a clever approach to combine the stability of the
monotonic clock with the relevance of real-world timestamps. The
monotonic clock (CLOCK_MONOTONIC) is used for measuring time
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
system. The development library is commonly named `pcre2-devel`,
`libpcre2-dev`, or a similar variant, depending on your operating
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
