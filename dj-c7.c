#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libserialport.h>
#include "print.h"

/*
 * Decode stages:
 *
 *  protocol
 *   -> interperet into
 *  memory (binary)
 *   -> interperet fields
 *  generalized config
 */

/*
 * When "sending", radio sends 42 bytes at a time, each ending with a '\r'
 * It expects a '\r\nOK\r\n' in reply acknowledging each piece of data. After a
 * short timeout, it will display "Failed" if no ack is recieved.
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
 * 0DC6 is 04 when step 10
 *
 * When the first memory location is written, 0D60 has it's high bit set (00 vs 80)
 *
 */

#define PKT_BYTES 42

struct dj_parms {
	const char *ack;
	const char magic[4];
	size_t mem_size;
};

static const struct dj_parms dj_c7 = {
	.ack = "\r\nOK\r\n",
	.magic = "AL~F",
	.mem_size = 0xfff + 1,
};

static ssize_t
read_pkt(struct sp_port *port, char buf[static PKT_BYTES], size_t len)
{
	(void)len;
	size_t pos = 0;

	for (;;) {
		enum sp_return sr = sp_blocking_read(port, buf + pos, PKT_BYTES - pos, 100);
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
			return pos;
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

static void pkt_encode(const struct dj_parms *p, uint_fast16_t offset, const unsigned char *buf, char *pkt)
{
	memcpy(pkt, p->magic, sizeof(p->magic));
	pkt += sizeof(p->magic);

	assert(offset <= 0xffff);
	sprintf((char *)pkt, "%04" PRIXFAST16, offset);

	pkt += 4;

	*pkt = 'W';
	
	pkt ++;

	size_t i;
	for (i = 0; i < 16; i++) {
		sprintf((char *)pkt, "%02X", buf[i]);
		pkt += 2;
	}

	*pkt = '\r';
}

struct dj_c7_pkt {
	uint8_t magic[4];
	uint_fast16_t offset;
	char action;
	uint8_t data[16];
};

static int_fast16_t
decode_hex_nibble(char c)
{
	if ('A' <= c && c <= 'F') {
		return c - 'A' + 10;
	} else if ('0' <= c && c <= '9') {
		return c - '0';
	} else
		return -1;
}

static int_fast16_t
decode_hex(char buf[static 2])
{
	int_fast16_t r1 = decode_hex_nibble(buf[0]);
	if (r1 < 0)
		return r1;
	
	int_fast16_t r2 = decode_hex_nibble(buf[1]);
	if (r2 < 0)
		return r2;

	return r1 << 4 | r2;
}

static int
decode_hex_buf(size_t len, char in[static len * 2], uint8_t out[static len])
{
	size_t i;
	for (i = 0; i < len; i ++) {
		int_fast16_t r = decode_hex(in + i * 2);
		if (r < 0) {
			fprintf(stderr, "E: decode hex failed at offset %zu out of %zu\n", i * 2, len);
			return r;
		}

		out[i] = r;
	}
	
	return 0;
}

static int_least32_t
decode_hex_16(char buf[static 4])
{
	int_fast16_t r = decode_hex(buf);
	if (r < 0)
		return r;

	int_fast16_t r2 = decode_hex(buf + 2);
	if (r2 < 0)
		return r2;

	return r << 8 | r2;
}

static int
pkt_decode(struct dj_c7_pkt *pkt, char buf[static PKT_BYTES])
{
	memcpy(pkt->magic, buf, sizeof(pkt->magic));
	buf += sizeof(pkt->magic);
	int_least32_t off = decode_hex_16(buf);
	if (off >= 0)
		pkt->offset = off;
	else
		pkt->offset = 0;
	buf += 4;
	pkt->action = *buf;
	buf ++;
	int r = decode_hex_buf(sizeof(pkt->data), buf, pkt->data);

	if (off < 0) {
		fprintf(stderr, "E: offset decode failed: %"PRIdLEAST32"\n", off);
		return -1;
	}

	if (r < 0) {
		fprintf(stderr, "E: data decode failed: %d\n", r);
		memset(pkt->data, 0, sizeof(pkt->data));
		return -2;
	}

	return 0;
}


static bool
pkt_is_ok(const struct dj_parms *p, struct dj_c7_pkt *pkt)
{
	int e = 0;
	if (memcmp(pkt->magic, p->magic, sizeof(pkt->magic))) {
		fprintf(stderr, "W: magic mis-match, have ");
		print_bytes_as_cstring(pkt->magic, sizeof(pkt->magic), stderr);
		fprintf(stderr, "\n");
		e++;
	}

	if (pkt->action != 'W') {
		fprintf(stderr, "W: unknown action '%c' (%d)\n", pkt->action, pkt->action);
		e++;
	}

	if (pkt->offset & 0xf) {
		fprintf(stderr, "E: low nibble in offset set: %#04"PRIxFAST16"\n", pkt->offset);
		e++;
	}

	if (pkt->offset > p->mem_size) {
		fprintf(stderr, "E: offset exceeds memory size: %#04"PRIxFAST16" > %#04zx\n",
				pkt->offset, p->mem_size);
		e++;
	}

	return !e;
}

static void
__attribute__((format(printf, 1, 2)))
check_printf(const char *fmt, ...)
{
	(void)fmt;
}


#ifdef DEBUG_SEND
#define debug_send(...) fprintf(stderr, "SEND: " __VA_ARGS__)
#else
#define debug_send(...) check_printf(__VA_ARGS__)
#endif

static void dj_send(const struct dj_parms *p, struct sp_port *port, FILE *in)
{
	unsigned char buf[16];
	char ack_buf[PKT_BYTES];
	char pkt[PKT_BYTES];

	size_t i;
	for (i = 0;;) {
		size_t l = fread(buf, 16, 1, in);
		if (l != 1) {
			if (feof(in)) {
				fprintf(stderr, "I: done\n");
				return;
			} else {
				fprintf(stderr, "E: error reading input file\n");
				return;
			}
		}

		pkt_encode(p, i << 4, buf, pkt);

		struct dj_c7_pkt p_dec;
		if (pkt_decode(&p_dec, pkt) < 0) {
			fprintf(stderr, "E: could not decode a packet I generated: ");
			print_bytes_as_cstring(pkt, PKT_BYTES, stderr);
			putc('\n', stderr);
			exit(EXIT_FAILURE);
		}

		if (!pkt_is_ok(p, &p_dec)) {
			fprintf(stderr, "E: a packet I generated was bad\n");
			exit(EXIT_FAILURE);
		}

		enum sp_return sr1 = sp_blocking_write_echocancel(port, pkt, sizeof(pkt), 0, 200);
		if (sr1 < 0) {
			fprintf(stderr, "E: failed to write packet: %d\n", sr1);
			exit(EXIT_FAILURE);
		}

		if ((size_t)sr1 != sizeof(pkt)) {
			fprintf(stderr, "E: failed to send entire packet, sent %d out of %zu bytes\n", sr1, sizeof(pkt));
			exit(EXIT_FAILURE);
		}

		debug_send("I: sent %d bytes\n", sr1);

		enum sp_return sr = sp_blocking_read(port, ack_buf, sizeof(ack_buf), 100);
		if (sr < 0) {
			fprintf(stderr, "E: failed to read packet: %d\n", sr);
			exit(EXIT_FAILURE);
		}

		if ((size_t)sr != strlen(p->ack) || memcmp(p->ack, ack_buf, sr)) {
			fprintf(stderr, "W: offset %#04zx was not acked, got: ", i << 4);
			print_bytes_as_cstring(ack_buf, sr, stderr);
			fprintf(stderr, "\nW: packet was: ");
			print_bytes_as_cstring(pkt, sizeof(pkt), stderr);
			putc('\n', stderr);
		}

		i ++;
	}
}

#ifdef DEBUG_RECV
#define debug_recv(...) fprintf(stderr, "RECV: " __VA_ARGS__)
#else
#define debug_recv(...) check_printf(__VA_ARGS__)
#endif

/*
 * TODO: consider if anyone would want to get a raw-er dump of the transfer
 */
static void *
dj_recv(const struct dj_parms *p, struct sp_port *port)
{
	/* place to put decoded data */
	/* FIXME: we just assume data is zero'd in non-included areas
	 */
	void *data = calloc(p->mem_size, 1);
	assert(data);

	char buf[PKT_BYTES];
	size_t i;
	for (i = 0;;) {
		ssize_t r = read_pkt(port, buf, sizeof(buf));
		if (r != sizeof(buf)) {
			fprintf(stderr, "E: short read of %zd\n", r);
			continue;
		}

		debug_recv("read_pkt\n");

		struct dj_c7_pkt pkt;
		if (pkt_decode(&pkt, buf) < 0) {
			fprintf(stderr, "E: decode failed, skipping packet\n");
			continue;
		}

		debug_recv("pkt_decode\n");

		if (!pkt_is_ok(p, &pkt)) {
			fprintf(stderr, "W: skipping packet\n");
			continue;
		}

		debug_recv("pkt_is_ok\n");

		if (pkt.offset >> 4 != i) {
			if (pkt.offset >> 4 > i) {
				fprintf(stderr, "W: jump from %#04zx to %#04" PRIxFAST16 ", continuing\n", i << 4, pkt.offset);
			} else {
				fprintf(stderr, "E: jump from %#04zx to %#04" PRIxFAST16 ", DATA WILL BE LOST\n", i << 4, pkt.offset);
			}
			i = pkt.offset >> 4;
		}

		/* do something with the data we have */
		switch (pkt.action) {
		case 'W':
			debug_recv("writing to %#04"PRIxFAST16"\n", pkt.offset);
			memcpy((char *)data + pkt.offset, pkt.data, sizeof(pkt.data));
			debug_recv("wrote\n");
			break;
		}
	
		debug_recv("write\n");

		i++;

		enum sp_return sr1 = sp_blocking_write_echocancel(port, p->ack, strlen(p->ack), 0, 100);
		if (sr1 < 0) {
			fprintf(stderr, "E: failed to write ack\n");
			exit(EXIT_FAILURE);
		}

		if ((i  << 4) >= p->mem_size)
			break;
	}

	return data;
}

static const char *opts = "p:hnb:";

static void usage_(const char *prgm, int e)
{
	FILE *f;
	if (e)
		f = stderr;
	else
		f = stdout;

	fprintf(f,
"usage: %s -p <serial-port> -b <binary file> <action>\n"
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
	const char *file = NULL;
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
		case 'b':
			file = optarg;
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

	struct sp_port_config *config = NULL;
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
	FILE *f = NULL;
	switch (*action) {
	case 's':
		if (!file) {
			fprintf(stderr, "E: a file is required\n");
			exit(EXIT_FAILURE);
		}

		f = fopen(file, "r");
		if (!f) {
			fprintf(stderr, "E: could not open file '%s'\n", file);
			exit(EXIT_FAILURE);
		}

		dj_send(&dj_c7, port, f);
		break;
	case 'r': {
		if (file) {
			f = fopen(file, "w");
			if (!f) {
				fprintf(stderr, "E: could not open file '%s'\n", file);
				exit(EXIT_FAILURE);
			}
		}
		void *data = dj_recv(&dj_c7, port);
		fwrite(data, dj_c7.mem_size, 1, f);
		free(data);
		break;
 	}
	default:
		fprintf(stderr, "E: unknown action '%s'\n", action);
		exit(EXIT_FAILURE);
	}

	if (f)
		fclose(f);
	sp_close(port);
	sp_free_config(config);
	return 0;
}
