#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libserialport.h>

/*
 * 1st packet sent by radio
 * Sd00
 * Times out (XXX: how long) after no responce
 */
static const char *pkt_1_tx =
"AL\x7e""F0000"
"W0146520"
"0000001000000000"
"060000000\x0d";

static const char *opts = "p:h";

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
	, prgm, opts);

	exit(e);
}
#define usage(e) usage_(argc?argv[0]:"dj-c7", e)

int main(int argc, char *argv[])
{
	int e = 0;
	const char *port_name = NULL;
	int opt;

	while ((opt = getopt(argc, argv, opts)) != -1) {
		switch (opt) {
		case 'p':
			port_name = optarg;
			break;
		case 'h':
			usage(EXIT_SUCCESS);
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

	sr = sp_set_config(port, config);
	if (sr != SP_OK) {
		fprintf(stderr, "E: failed to apply configuration to port: %d\n", sr);
		exit(EXIT_FAILURE);
	}

	sr = sp_open(port, SP_MODE_WRITE | SP_MODE_READ);
	if (sr != SP_OK) {
		fprintf(stderr, "E: failed to open port '%s': %d\n", port_name, sr);
		exit(EXIT_FAILURE);
	}
	
	return 0;
}
