#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

/*
 * each file has a set of (property,value) pairs associated with it.
 * we want to do a bit-wise diff on every pair of files that has that property set
 *
 * keep track of the bits that differ for a given property when it's value changes.
 * 
 * Some of these bits are related to the property
 * If these bits change for other properties, they are less likely to be related to our property
 * If these bits don't change for our property some of the time, they are less likely to be related
 *
 * For each property:
 *  - # occurances (ie: how many files have it), from which we derive # of comarisons
 * For each bit for each property:
 *  - # of times changed with this property's value
 *  - # of times changed without this property's value changing
 *
 * Problems:
 *  - only know things for sure if we know the storage mechanism
 */
static void process_dir(const char *dir_path)
{
	(void)dir_path;
}


const char *opts = ":hd:";

static void
usage_(const char *prgmname, int e)
{
	FILE *f;
	if (e != EXIT_SUCCESS)
		f = stderr;
	else
		f = stdout;

	fprintf(f,
"Usage: %s [-d <datadir>]...\n"
"Opts: %s\n"
"\n"
"Given a set of binary files with corresponding descriptions named\n"
"<binary-file>.desc.txt guess the meaning of bits & bytes within a binary\n"
"image\n"
"\n"
"Format of *.desc.txt:\n"
" desc := line '\\n' | desc\n"
" line := comment_line | property_line\n"
" comment_line := '#.*'\n"
" property_line := property '=' prop_value\n"
" prop_value := property\n"
" property := '[][a-zA-Z0-9_{}!@$%%^&*()+<>~`'\n"
"Example:\n"
" displayed_frequency = 144000\n"
" band_frequency[1] = 144000\n"
"\n"
"Omitted properties are allowed to be any value\n"
"\n"
"There are trade-offs in how precise one is in describing a property.\n"
"\n"
"Output format:\n"
"   <hex-byte-address>:<bit> = <property_name>\n"
"Example:\n"
"   0x412562:4 = band_1\n"
"\n"
"Each line indicates a location that is probably connected to that property\n"
"Note that if the input data (and description) is insufficient, more bits will\n"
"be seen as related than are actually related.\n"
"\n"
"No warning will be emitted if this is the case.\n",
	prgmname, opts);
	exit(e);
}
#define usage(e) usage_(argc?argv[0]:"bin-id", e)

int main(int argc, char *argv[])
{
	int opt;
	while ((opt = getopt(argc, argv, opts)) != -1) {
		switch (opt) {
		case 'h':
			usage(EXIT_SUCCESS);
			break;
		case 'd':
			process_dir(optarg);
			break;
		case '?':
			usage(EXIT_FAILURE);
		}
	}
}
