#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libserialport.h>
#include "print.h"

/*
 * When "sending", radio sends 42 bytes at a time, each ending with a '\r'
 * It expects a '\r\nOK\r\n' in reply acknowledging each piece of data
 *
 * 0000000000111111111122222222223333333333444
 * 0123456789012345678901234567890123456789012
 * AL~F0XX0W012345678901234567890123456789012\r
 *          |    data bytes in hex          |
 *      ||-> address
 *
 * "AL~F" : 4 bytes: marker, meaning unknown
 * "0AB0" : 4 bytes: address (2 bytes, lowest and highest always zero)
 * "W"    : 1 bytes: action, only 'W' seen
 *        : 32 bytes: data, hex encoded, 16 actual bytes
 * "\r"   : 1 bytes: packet end
 *
 * 0D5D is 27 on unlocked-tx
 * 0D5D is 23 on locked-tx
 *
 * 0D5C is 6c on mprotect on
 * 0D5C is 2c on mprotect off
 *
 * 0D52 is 0A when volume is 10
 * 0D52 is 09 when volume is 9
 *
 * 0D54 is 03 when squelch is 3
 * 0D54 is 04 when squelch is 4
 *
 * 0D5D is 23 when hi-volume
 * 0D5D is 33 when lo-volume
 *
 * 0D5C is 6c when SMA
 * 0D5C is 7c when Ear
 *
 * 0D5D is 33 when rpt normal
 * 0D5D is B3 when rpt star
 *
 * 0D5B is 00 when tone is 1750
 * 0D5B is 01 when tone is 2100
 * 0D5B is 02 when tone is 1000
 * 0D5B is 03 when tone is 1450
 *
 * 0D58 is 00 when APO is off
 * 0D58 is 01 when APO is 30min
 * 0D58 is 02 when APO is 60min
 * 0D58 is 03 when APO is 90min
 *
 * 0D5C is 5C when bs is off
 * 0D5C is 7C when bs is on
 *
 * 0D5C is 5C when beep is on
 * 0D5C is 54 when beep is off
 *
 * 0D5C is 54 when bell is off
 * 0D5C is 55 when bell is on
 *
 * 0D5D is B3 when "busy"
 * 0D5D is F3 when "timer"
 *
 * 0D5D is 23 when step "auto"
 * 0D5D is 03 when step "5"
 *
 * - freq was 145.000
 * 0DC6 is 01 when step 5
 * 0DC6 is 02 when step 6.25
 * 0DC6 is 03 when step 8.33
 *
 * When the first memory location is written, 0D60 has it's high bit set (00 vs 80)
 *
 */


/*
 * 1st packet sent by radio
 * Sd00
 * Times out (XXX: how long) after no responce
 */
static const char pkt_tx[][42] = {
	"AL~F0000W01465200000001000000000060000000\r",
	"AL~F0010W01469700000001021010020060000000\r"
};

/*
 * sent in responce to pkt_1_tx
 * Ld00
 */
static const char pkt_1_rx[] = "\r\nOK\r\n";

static void show_pkt(struct sp_port *port)
{
	char buf[42];
	size_t pos = 0;

	for (;;) {
		enum sp_return sr = sp_blocking_read(port, buf + pos, sizeof(buf) - pos, 100);
		if (sr < 0) {
			fprintf(stderr, "E: failed to read packet: %d\n", sr);
			exit(EXIT_FAILURE);
		}
		if (sr == 0) {
			/* nada, print remainder and exit */
			if (pos) {
				printf("timed out with data: ");
				print_bytes_as_cstring(buf, pos, stdout);
				printf(", flushing\n");
				pos = 0;
			} else
				putc('.', stderr);
			continue;
		}

		/* look for a '\r' */
		char *end = memchr(buf + pos, '\r', sr);
		pos += sr;
		if (end) {
			print_bytes_as_cstring(buf, pos, stdout);
			putchar('\n');
			fflush(stdout);
			pos = 0;
			return;
		}
	}
}

static enum sp_return sp_blocking_write_echocancel(struct sp_port *port, const void *buf, size_t count, unsigned timeout, unsigned echo_timeout)
{
	enum sp_return sr1 = sp_blocking_write(port, buf, count, timeout);
	if (sr1 < 0) {
		fprintf(stderr, "E: failed to write packet: %d\n", sr1);
		return sr1;
	}

	if ((size_t)sr1 != count) {
		fprintf(stderr, "E: failed to send entire packet, sent %d out of %zu bytes\n", sr1, count);
		return sr1;
	}

	/* We're half-duplex, so do echo cancelation */
	char echo_buf[sr1];
	enum sp_return sr2 = sp_blocking_read(port, echo_buf, sizeof(echo_buf), echo_timeout);
	if (sr2 < 0) {
		fprintf(stderr, "E: failed to read echo-cancel data\n");
		return sr2;
	}

#if 0
	printf("canceling: ");
	print_bytes_as_cstring(echo_buf, sr2, stdout);
	putchar('\n');
#endif

	if (sr2 != sr1) {
		fprintf(stderr, "E: did not read enough echo-cancel data, got %d out of %d bytes\n", sr2, sr1);
		return SP_ERR_FAIL;
	}

	if (memcmp(buf, echo_buf, sr1)) {
		fprintf(stderr, "E: echo-cancel data is not equal to sent data\n");
		return SP_ERR_FAIL;
	}

	return sr1;
}

static void dj_c7_send(struct sp_port *port)
{
	enum sp_return sr1 = sp_blocking_write_echocancel(port, pkt_tx[0], sizeof(pkt_tx[0]) - 1, 0, 200);
	if (sr1 < 0) {
		fprintf(stderr, "E: failed to write packet: %d\n", sr1);
		exit(EXIT_FAILURE);
	}

	if ((size_t)sr1 != sizeof(pkt_tx[0]) - 1) {
		fprintf(stderr, "E: failed to send entire packet, sent %d out of %zu bytes\n", sr1, sizeof(pkt_tx[0]) - 1);
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "I: sent %d bytes\n", sr1);

	show_pkt(port);
}

static void
dj_c7_recv(struct sp_port *port)
{
	for (;;) {
		show_pkt(port);
		enum sp_return sr1 = sp_blocking_write_echocancel(port, pkt_1_rx, sizeof(pkt_1_rx) - 1, 0, 100);
		if (sr1 < 0) {
			fprintf(stderr, "E: failed to read\n");
			exit(EXIT_FAILURE);
		}
	}
}

static const char *opts = "p:hn";

static void usage_(const char *prgm, int e)
{
	FILE *f;
	if (e)
		f = stderr;
	else
		f = stdout;

	fprintf(f,
"usage: %s -p <serial-port> <action>\n"
"actions:\n"
" send\n"
" receive\n"
"options: -%s\n"
" -n	don't configure serial port\n"
	, prgm, opts);

	exit(e);
}
#define usage(e) usage_(argc?argv[0]:"dj-c7", e)

int main(int argc, char *argv[])
{
	int e = 0;
	const char *port_name = NULL;
	bool do_config = true;
	int opt;

	while ((opt = getopt(argc, argv, opts)) != -1) {
		switch (opt) {
		case 'p':
			port_name = optarg;
			break;
		case 'h':
			usage(EXIT_SUCCESS);
			break;
		case 'n':
			do_config = false;
			break;
		default:
			e++;
			fprintf(stderr, "E: unknown option %c\n", opt);
			break;
		}
	}

	if (!port_name) {
		e++;
		fprintf(stderr, "E: no port (-p) specified\n");
	}

	if (optind != (argc - 1)) {
		e++;
		fprintf(stderr, "E: require a single <action> after options\n");
	}

	if (e)
		usage(EXIT_FAILURE);

	struct sp_port *port;
	enum sp_return sr = sp_get_port_by_name(port_name, &port);
	if (sr != SP_OK) {
		fprintf(stderr, "E: failed to get serial port '%s': %d\n", port_name, sr);
		exit(EXIT_FAILURE);
	}

	struct sp_port_config *config;
	if (do_config) {
		sr = sp_new_config(&config);
		if (sr != SP_OK) {
			fprintf(stderr, "E: failed to create port config: %d\n", sr);
			exit(EXIT_FAILURE);
		}

		sr = sp_set_config_baudrate(config, 9600);
		if (sr != SP_OK) {
			fprintf(stderr, "E: failed to set config baud rate: %d\n", sr);
			exit(EXIT_FAILURE);
		}

		sr = sp_set_config_bits(config, 8);
		if (sr != SP_OK) {
			fprintf(stderr, "E: failed to set config bits: %d\n", sr);
			exit(EXIT_FAILURE);
		}

		sr = sp_set_config_parity(config, SP_PARITY_NONE);
		if (sr != SP_OK) {
			fprintf(stderr, "E: failed to set config parity: %d\n", sr);
			exit(EXIT_FAILURE);
		}

		sr = sp_set_config_stopbits(config, 1);
		if (sr != SP_OK) {
			fprintf(stderr, "E: failed to set config stopbits: %d\n", sr);
			exit(EXIT_FAILURE);
		}

		sr = sp_set_config_flowcontrol(config, SP_FLOWCONTROL_NONE);
		if (sr != SP_OK) {
			fprintf(stderr, "E: failed to set config flow control: %d\n", sr);
			exit(EXIT_FAILURE);
		}
	}

	sr = sp_open(port, SP_MODE_WRITE | SP_MODE_READ);
	if (sr != SP_OK) {
		fprintf(stderr, "E: failed to open port '%s': %d\n", port_name, sr);
		exit(EXIT_FAILURE);
	}

	if (do_config) {
		sr = sp_set_config(port, config);
		if (sr != SP_OK) {
			fprintf(stderr, "E: failed to apply configuration to port: %d\n", sr);
			exit(EXIT_FAILURE);
		}
	}

	const char *action = argv[optind];
	switch (*action) {
	case 's':
		dj_c7_send(port);
		break;
	case 'r':
		dj_c7_recv(port);
		break;
	default:
		fprintf(stderr, "E: unknown action '%s'\n", action);
		exit(EXIT_FAILURE);
	}

	return 0;
}
