# Emacs Attack

This code attacks the GNU Emacs' `make-temp-name` function. It works on Linux.

## Building

You'll need a C++11 compiler. Just type `make`.

## Running

If run with no arguments, the names of candidate temporary files will be printed
to stdout. The following arguments are useful:

| Short | Long        | Default | Meaning                                   |
|-------|-------------|---------|-------------------------------------------|
| `-p`  | `--prefix`  |         | File prefix to use                        |
| `-s`  | `--seconds` |     100 | Number of seconds to attempt as LCG seeds |
| `-f`  | `--files`   |      10 | Number of rounds to run each LCG seed     |
| `-q`  | `--quiet`   |   false | Be quiet                                  |

## License

This code is [free software](https://www.gnu.org/philosophy/free-sw.en.html)
licensed under the GPL v3+.
