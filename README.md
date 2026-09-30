# RTOS IPC Fuzzing Workshop

A hands-on workshop on assessing an embedded system through a UART interface, discovering its message-processing attack surface, and progressing from manual testing to protocol-aware fuzzing.

The lab uses **Zephyr RTOS**, **QEMU (`qemu_x86`)**, a UART shell, and Zephyr's `k_msgq` message queue. The target is intentionally vulnerable and is designed for isolated training environments only.

> **Safety notice:** This is an educational target. Do not deploy it on real devices or expose it to untrusted networks. Run it locally or in an isolated lab.

## What you'll learn

- Identify and interact with a UART-based command interface.
- Enumerate shell commands and inspect system state.
- Discover an inter-thread message queue and understand the message format.
- Construct and submit a valid IPC message.
- Manually mutate message fields and observe failure conditions.
- Trace a message from UART input through the queue to the consumer.
- Recognize an unchecked length-controlled copy and understand its impact.
- Automate test cases with a Python UART client.
- Apply and validate a bounds-checking fix.
- Relate the exercise to message-passing concepts used in other RTOS environments, including QNX.

## Lab architecture

```text
Host terminal / Python client
          |
       UART shell
          |
   ipc send <hex bytes>
          |
   Parse message into struct
          |
    Zephyr k_msgq
          |
   Controller thread
          |
   Message parser
          |
   Local payload buffer
```

The lab uses a **custom educational message format** transported through a real Zephyr `k_msgq`. It demonstrates inter-thread communication inside one Zephyr image; it is not a demonstration of QNX-style process-to-process IPC.

## Repository layout

```text
.
├── bsides-ipc-demo/       # Zephyr application used as the lab target
├── set-up.sh              # Environment setup script (if included in this checkout)
├── run-qemu-pty.sh        # Optional QEMU launcher exposing UART through a PTY
└── uart_tool.py           # Python UART shell/fuzzing client (if included in this checkout)
```

Check the repository for the exact filenames available in your checkout.

## Prerequisites

The setup is intended for Ubuntu or a compatible Linux environment. You will need:

- Git
- Python 3 and `venv`
- CMake, Ninja, and Zephyr build dependencies
- Zephyr `west` and the Zephyr SDK
- QEMU system emulator for x86
- `pyserial` for the Python UART client

Review setup scripts before running them, especially on shared or production machines.

## 1. Clone the repository

```bash
git clone https://github.com/kashif-23/fuzzingRTOS_Workshop.git
cd fuzzingRTOS_Workshop
```

## 2. Prepare the Zephyr environment

If `set-up.sh` is included, inspect it and run it on a fresh lab machine:

```bash
chmod +x set-up.sh
./set-up.sh
```

This is intended for initial environment preparation, not for every launch.

If you already have a working Zephyr workspace, activate its virtual environment and build the application:

```bash
cd ~/zephyrproject
source .venv/bin/activate

west build -p always -b qemu_x86 \
  ~/zephyrproject/fuzzingRTOS_Workshop/bsides-ipc-demo
```

A successful build should produce `build/zephyr/zephyr.elf`.

## 3. Run the target

### Standard interactive mode

From the Zephyr workspace:

```bash
west build -t run
```

QEMU starts Zephyr and attaches the UART shell to the terminal. You should see the Zephyr boot banner and a prompt similar to:

```text
uart:~$
```

Exit QEMU with `Ctrl+A`, then `X`.

### PTY mode for Python automation

An optional PTY launcher can expose the QEMU UART as a host pseudo-terminal, such as `/dev/pts/7`. The number varies between runs and machines.

```bash
chmod +x ~/run-qemu-pty.sh
~/run-qemu-pty.sh
```

Record the exact PTY path printed by QEMU. PTY mode redirects the guest UART away from the normal QEMU terminal; use another terminal to run the Python client.

If QEMU shows SeaBIOS/iPXE instead of the Zephyr banner, verify that the firmware ELF exists and that QEMU is launched with the correct build artifact:

```bash
ls -lh ~/zephyrproject/build/zephyr/zephyr.elf
```

## 4. Explore the UART shell

At the `uart:~$` prompt, start with:

```text
help
device
kernel
sensor status
controller status
ipc status
diag
```

Useful commands include:

```text
sensor stop
sensor start
controller log off
controller log on
controller status
ipc status
```

Turning controller logging off suppresses periodic controller messages; it does not stop the controller thread.

## 5. Understand the message format

The custom IPC message contains:

| Field | Size | Purpose |
|---|---:|---|
| `magic` | 1 byte | Message marker (`0xB5`) |
| `command` | 1 byte | Operation identifier |
| `length` | 2 bytes | Number of payload bytes to process (little-endian) |
| `sensor_id` | 4 bytes | Sensor identifier (little-endian) |
| `payload` | 32 bytes | Message data |

For the sensor command (`0x10`), the first four payload bytes represent temperature and the next four represent pressure, both encoded as little-endian 32-bit integers.

Example valid message:

```text
b5100800e90300004800000065000000
```

| Bytes | Meaning |
|---|---|
| `b5` | Magic `0xB5` |
| `10` | Sensor command |
| `08 00` | Payload length 8 |
| `e9 03 00 00` | Sensor ID 1001 |
| `48 00 00 00` | Temperature 72 |
| `65 00 00 00` | Pressure 101 |

Submit it through the shell:

```text
ipc send b5100800e90300004800000065000000
```

The shell decodes the hex bytes into the message structure and queues it for the controller thread.

## 6. Manual testing and vulnerability analysis

Start with a valid message and vary the `length` field while keeping the rest of the message controlled. Compare behavior at values such as 8, 16, and 17.

The intentionally unsafe parser copies the declared number of bytes into a local buffer with a fixed capacity. The issue is **an unchecked, externally influenced copy length**, not that `memcpy` is inherently unsafe.

A defensive implementation should reject a length greater than the destination buffer's capacity before copying. After applying a fix, rebuild and repeat the tests to verify that oversized messages are rejected safely.

## 7. Python UART client

If `uart_tool.py` is present, install its dependency:

```bash
python3 -m pip install --user pyserial
```

The client provides two modes:

```bash
python3 uart_tool.py /dev/pts/7 shell
python3 uart_tool.py /dev/pts/7 fuzz
```

- `shell` provides an interactive way to send commands through the PTY.
- `fuzz` automates mutations of the IPC message length field.

Replace `/dev/pts/7` with the PTY path printed by your QEMU process. The client must connect to the same QEMU instance you intend to test; a second QEMU process is a separate target.

## Suggested workshop flow

1. Discover the UART and enumerate commands.
2. Inspect the sensor, controller, and IPC queue.
3. Identify the message format.
4. Send a valid message manually.
5. Mutate the length field and observe boundary behavior.
6. Trace the message path through the application source.
7. Explain the unchecked copy and its consequences.
8. Automate the experiment with Python.
9. Apply a bounds check and validate the fix.
10. Discuss similar trust-boundary and message-validation questions in other RTOS IPC models.

## Troubleshooting

### `west` says the directory is not a Zephyr build directory

Run commands from the Zephyr workspace and specify the application directory:

```bash
cd ~/zephyrproject
source .venv/bin/activate
west build -p always -b qemu_x86 \
  ~/zephyrproject/fuzzingRTOS_Workshop/bsides-ipc-demo
```

### `zephyr.elf` does not exist

Build the application first and inspect the build output for errors:

```bash
west build -p always -b qemu_x86 \
  ~/zephyrproject/fuzzingRTOS_Workshop/bsides-ipc-demo
```

### QEMU boots SeaBIOS/iPXE instead of Zephyr

Confirm QEMU's `-kernel` argument points to the ELF generated by a successful Zephyr build. Do not use a guessed or stale build path.

### Python opens the PTY but commands do not reach the target

- Confirm QEMU is still running.
- Use the exact PTY path printed by that QEMU process.
- Run Python and QEMU in the same host environment (for example, both in the same WSL instance or VPS).
- Ensure no other process is consuming input from the PTY.
- Test the Python client's `shell` mode before fuzzing.

### The original terminal no longer shows `uart:~$`

That is expected in PTY mode: the UART has been redirected to the pseudo-terminal. Use the Python shell mode or return to `west build -t run` for direct terminal interaction.

## Disclaimer

This project is intended solely for authorized education, research, and isolated lab use. The vulnerable behavior is deliberate and should not be copied into production firmware.
