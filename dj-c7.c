#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libserialport.h>
#include "print.h"

/*
 * 1st packet sent by radio
 * Sd00
 * Times out (XXX: how long) after no responce
 */
static const char pkt_1_tx[] =
"AL~F0000"
"W0146520"
"0000001000000000"
"060000000\x0d";

/*
 * sent in responce to pkt_1_tx
 * Ld00
 */
static const char pkt_1_rx[] = "\r\nOK\r\n";

static void show_pkt(struct sp_port *port)
{
	char buf[1024];
	size_t pos = 0;

	for (;;) {
		enum sp_return sr = sp_blocking_read(port, buf + pos, sizeof(buf) - pos, 500);
		if (sr < 0) {
			fprintf(stderr, "E: failed to read packet: %d\n", sr);
			exit(EXIT_FAILURE);
		}
		if (sr == 0) {
			/* nada, print remainder and exit */
			printf("\n");
			return;
		}

		/* todo: print in chunks */
		pos += sr;
		print_bytes_as_cstring(buf, pos, stdout);
		pos = 0;
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

	if (sr2 != sr1) {
		fprintf(stderr, "E: did not read enough echo-cancel data, got %d out of %d bytes\n", sr2, sr1);
		return SP_ERR_FAIL;
	}

	if (memcmp(buf, pkt_1_tx, sr1)) {
		fprintf(stderr, "E: echo-cancel data is not equal to sent data\n");
		return SP_ERR_FAIL;
	}

	return sr1;
}

static void dj_c7_send(struct sp_port *port)
{
	enum sp_return sr1 = sp_blocking_write_echocancel(port, pkt_1_tx, sizeof(pkt_1_tx) - 1, 0, 200);
	if (sr1 < 0) {
		fprintf(stderr, "E: failed to write packet: %d\n", sr1);
		exit(EXIT_FAILURE);
	}

	if ((size_t)sr1 != sizeof(pkt_1_tx) - 1) {
		fprintf(stderr, "E: failed to send entire packet, sent %d out of %zu bytes\n", sr1, sizeof(pkt_1_tx) - 1);
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "I: sent %d bytes\n", sr1);

	show_pkt(port);
}

static void
dj_c7_recv(struct sp_port *p)
{

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
