/*
 * BSides Krakow — RTOS IPC Fuzzing Lab
 *
 * Educational target only.
 *
 * Real IPC primitive:
 *   Zephyr k_msgq
 *
 * External control surface:
 *   Zephyr UART shell
 *
 * Intentional vulnerability:
 *   controller parser copies "length" bytes into a 16-byte local buffer
 *   without validating the declared length.
 *
 * DO NOT deploy this application on real devices.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define IPC_MAX_PAYLOAD 32
#define IPC_QUEUE_DEPTH 16
#define IPC_MAGIC 0xB5

struct ipc_message {
    uint8_t magic;
    uint8_t command;
    uint16_t length;
    uint32_t sensor_id;
    uint8_t payload[IPC_MAX_PAYLOAD];
};

K_MSGQ_DEFINE(ipc_msgq, sizeof(struct ipc_message), IPC_QUEUE_DEPTH, 4);

static volatile bool sensor_running = true;
static volatile bool controller_logging = true;

static volatile uint32_t sensor_messages;
static volatile uint32_t controller_messages;

static volatile uint32_t last_temperature = 72;
static volatile uint32_t last_pressure = 101;
static volatile uint32_t last_sensor_id = 1001;
static volatile uint32_t sequence;

K_THREAD_STACK_DEFINE(sensor_stack, 2048);
K_THREAD_STACK_DEFINE(controller_stack, 2048);

static struct k_thread sensor_thread_data;
static struct k_thread controller_thread_data;


/*
 * Sensor producer thread
 */
static void sensor_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (1) {
        if (sensor_running) {
            struct ipc_message msg = {
                .magic = IPC_MAGIC,
                .command = 0x10,
                .length = 8,
                .sensor_id = last_sensor_id,
            };

            uint32_t temp = 70 + (sequence % 10);
            uint32_t pressure = 100 + (sequence % 5);

            memcpy(&msg.payload[0], &temp, sizeof(temp));
            memcpy(&msg.payload[4], &pressure, sizeof(pressure));

            last_temperature = temp;
            last_pressure = pressure;
            sequence++;

            if (k_msgq_put(&ipc_msgq, &msg, K_NO_WAIT) == 0) {
                sensor_messages++;
            }
        }

        k_msleep(2000);
    }
}


/*
 * Controller parser
 *
 * INTENTIONAL VULNERABILITY:
 *
 * The receiver trusts msg.length and copies that many bytes into a
 * 16-byte local buffer.
 *
 * Values greater than 16 can corrupt the stack.
 *
 * The exercise is to discover this condition, reproduce it, and then
 * explain the missing validation.
 */
static void controller_parse(const struct ipc_message *msg)
{
    uint8_t local_payload[16];

    if (controller_logging) {
        printk("[CONTROLLER] cmd=0x%02x len=%u sensor=%u\n",
               msg->command, msg->length, msg->sensor_id);
    }

    if (msg->magic != IPC_MAGIC) {
        if (controller_logging) {
            printk("[CONTROLLER] rejected: bad magic\n");
        }
        return;
    }

    /*
     * Deliberately unsafe for the workshop.
     *
     * The destination is only 16 bytes, but msg->length is attacker
     * controlled.
     */
    memcpy(local_payload, msg->payload, msg->length);

    if (msg->command == 0x10 && msg->length >= 8) {
        uint32_t temp;
        uint32_t pressure;

        memcpy(&temp, &local_payload[0], sizeof(temp));
        memcpy(&pressure, &local_payload[4], sizeof(pressure));

        if (controller_logging) {
            printk("[CONTROLLER] SENSOR temp=%u C pressure=%u kPa\n",
                   temp, pressure);
        }

        if (temp > 75) {
            if (controller_logging) {
                printk("[CONTROLLER] WARNING: high temperature\n");
            }
        }
    } else if (msg->command == 0x20) {
        if (controller_logging) {
            printk("[CONTROLLER] diagnostic command received\n");
        }
    } else {
        if (controller_logging) {
            printk("[CONTROLLER] unknown command\n");
        }
    }
}


/*
 * Controller consumer thread
 */
static void controller_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (1) {
        struct ipc_message msg;

        k_msgq_get(&ipc_msgq, &msg, K_FOREVER);
        controller_messages++;

        controller_parse(&msg);
    }
}


/*
 * Convert one hexadecimal character to a value.
 */
static int parse_hex_byte(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}


/*
 * Sensor shell command
 *
 * sensor
 * sensor status
 * sensor start
 * sensor stop
 */
static int cmd_sensor(const struct shell *sh, size_t argc, char **argv)
{
    if (argc == 1 || !strcmp(argv[1], "status")) {
        shell_print(sh, "sensor: %s",
                    sensor_running ? "running" : "stopped");

        shell_print(sh, "sensor_id: %u", last_sensor_id);
        shell_print(sh, "temperature: %u C", last_temperature);
        shell_print(sh, "pressure: %u kPa", last_pressure);
        shell_print(sh, "messages_sent: %u", sensor_messages);

        return 0;
    }

    if (!strcmp(argv[1], "start")) {
        sensor_running = true;
        shell_print(sh, "sensor started");
        return 0;
    }

    if (!strcmp(argv[1], "stop")) {
        sensor_running = false;
        shell_print(sh, "sensor stopped");
        return 0;
    }

    shell_error(sh, "usage: sensor [status|start|stop]");
    return -EINVAL;
}


/*
 * Controller shell command
 *
 * controller
 * controller status
 * controller ping
 * controller log on
 * controller log off
 */
static int cmd_controller(const struct shell *sh, size_t argc, char **argv)
{
    /*
     * controller
     * controller status
     */
    if (argc == 1 || !strcmp(argv[1], "status")) {
        shell_print(sh, "controller: running");

        shell_print(sh, "messages_received: %u",
                    controller_messages);

        shell_print(sh, "queue_used: %u",
                    k_msgq_num_used_get(&ipc_msgq));

        shell_print(sh, "queue_free: %u",
                    k_msgq_num_free_get(&ipc_msgq));

        shell_print(sh, "logging: %s",
                    controller_logging ? "on" : "off");

        return 0;
    }


    /*
     * controller ping
     */
    if (!strcmp(argv[1], "ping")) {
        struct ipc_message msg = {
            .magic = IPC_MAGIC,
            .command = 0x20,
            .length = 0,
            .sensor_id = last_sensor_id,
        };

        if (k_msgq_put(&ipc_msgq, &msg, K_NO_WAIT) != 0) {
            shell_error(sh, "queue full");
            return -EAGAIN;
        }

        shell_print(sh, "diagnostic message queued");
        return 0;
    }


    /*
     * controller log on
     * controller log off
     */
    if (!strcmp(argv[1], "log")) {
        if (argc != 3) {
            shell_error(sh, "usage: controller log [on|off]");
            return -EINVAL;
        }

        if (!strcmp(argv[2], "on")) {
            controller_logging = true;
            shell_print(sh, "controller logging enabled");
            return 0;
        }

        if (!strcmp(argv[2], "off")) {
            controller_logging = false;
            shell_print(sh, "controller logging disabled");
            return 0;
        }

        shell_error(sh, "usage: controller log [on|off]");
        return -EINVAL;
    }


    shell_error(sh,
                "usage: controller [status|ping|log on|log off]");

    return -EINVAL;
}


/*
 * IPC shell command
 *
 * ipc
 * ipc status
 * ipc send <hex-bytes>
 */
static int cmd_ipc(const struct shell *sh, size_t argc, char **argv)
{
    if (argc == 1 || !strcmp(argv[1], "status")) {
        shell_print(sh, "IPC: Zephyr k_msgq");

        shell_print(sh, "item_size: %u",
                    (unsigned)sizeof(struct ipc_message));

        shell_print(sh, "depth: %u",
                    IPC_QUEUE_DEPTH);

        shell_print(sh, "used: %u",
                    k_msgq_num_used_get(&ipc_msgq));

        shell_print(sh, "free: %u",
                    k_msgq_num_free_get(&ipc_msgq));

        shell_print(sh,
                    "format: magic cmd length sensor_id payload");

        return 0;
    }


    if (!strcmp(argv[1], "send")) {
        if (argc < 3) {
            shell_error(sh, "usage: ipc send <hex-bytes>");

            shell_print(sh,
                        "example: ipc send b5100800e90300004800000065000000");

            return -EINVAL;
        }

        const char *hex = argv[2];
        size_t hex_len = strlen(hex);

        if ((hex_len % 2) != 0 ||
            hex_len > sizeof(struct ipc_message) * 2) {

            shell_error(sh,
                        "hex length must be even and <= %u characters",
                        (unsigned)(sizeof(struct ipc_message) * 2));

            return -EINVAL;
        }

        struct ipc_message msg = {0};

        uint8_t *raw = (uint8_t *)&msg;

        size_t byte_len = hex_len / 2;


        /*
         * Convert hexadecimal input into raw bytes.
         */
        for (size_t i = 0; i < byte_len; i++) {
            int hi = parse_hex_byte(hex[i * 2]);
            int lo = parse_hex_byte(hex[i * 2 + 1]);

            if (hi < 0 || lo < 0) {
                shell_error(sh,
                            "invalid hex at byte %u",
                            (unsigned)i);

                return -EINVAL;
            }

            raw[i] = (uint8_t)((hi << 4) | lo);
        }


        /*
         * Require enough bytes to contain:
         *
         * magic
         * command
         * length
         * sensor_id
         */
        if (byte_len < 8) {
            shell_error(sh,
                        "message too short: need at least 8 bytes");

            return -EINVAL;
        }


        /*
         * Put the attacker-controlled message into the real
         * Zephyr IPC queue.
         */
        if (k_msgq_put(&ipc_msgq, &msg, K_NO_WAIT) != 0) {
            shell_error(sh, "IPC queue full");
            return -EAGAIN;
        }

        shell_print(sh,
                    "queued %u bytes for controller",
                    (unsigned)byte_len);

        return 0;
    }


    shell_error(sh,
                "usage: ipc [status|send <hex-bytes>]");

    return -EINVAL;
}


/*
 * Diagnostic shell command
 */
static int cmd_diag(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== diagnostic ===");

    shell_print(sh,
                "Zephyr IPC target: ONLINE");

    shell_print(sh,
                "sensor: %s",
                sensor_running ? "RUNNING" : "STOPPED");

    shell_print(sh,
                "controller: RUNNING");

    shell_print(sh,
                "controller logging: %s",
                controller_logging ? "ON" : "OFF");

    shell_print(sh,
                "IPC queue: %u/%u used",
                k_msgq_num_used_get(&ipc_msgq),
                IPC_QUEUE_DEPTH);

    shell_print(sh,
                "sensor messages: %u",
                sensor_messages);

    shell_print(sh,
                "controller messages: %u",
                controller_messages);

    shell_print(sh, "==================");

    return 0;
}


/*
 * Zephyr shell command registration
 */
SHELL_CMD_REGISTER(
    sensor,
    NULL,
    "Sensor controls: status/start/stop",
    cmd_sensor
);

SHELL_CMD_REGISTER(
    controller,
    NULL,
    "Controller status, diagnostic ping and logging",
    cmd_controller
);

SHELL_CMD_REGISTER(
    ipc,
    NULL,
    "Inspect/send IPC messages",
    cmd_ipc
);

SHELL_CMD_REGISTER(
    diag,
    NULL,
    "Show complete target status",
    cmd_diag
);


/*
 * Application entry point
 */
int main(void)
{
    printk("\n");
    printk("=============================================\n");
    printk(" BSides Krakow — RTOS IPC Fuzzing Target\n");
    printk(" Zephyr / QEMU x86\n");
    printk("=============================================\n");

    printk("Real IPC: k_msgq\n");
    printk("UART shell: type 'help'\n\n");


    /*
     * Create sensor producer thread.
     */
    k_thread_create(
        &sensor_thread_data,
        sensor_stack,
        K_THREAD_STACK_SIZEOF(sensor_stack),
        sensor_thread,
        NULL,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT
    );

    k_thread_name_set(
        &sensor_thread_data,
        "sensor"
    );


    /*
     * Create controller consumer thread.
     */
    k_thread_create(
        &controller_thread_data,
        controller_stack,
        K_THREAD_STACK_SIZEOF(controller_stack),
        controller_thread,
        NULL,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT
    );

    k_thread_name_set(
        &controller_thread_data,
        "controller"
    );


    return 0;
}