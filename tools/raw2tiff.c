/* $Id: raw2tiff.c,v 1.5 2003-07-10 20:04:42 dron Exp $
 *
 * Project:  libtiff tools
 * Purpose:  Convert raw byte sequences in TIFF images
 * Author:   Andrey Kiselev, dron@remotesensing.org
 *
 ******************************************************************************
 * Copyright (c) 2002, Andrey Kiselev <dron@remotesensing.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiffio.h"

static	uint16 compression = (uint16) -1;
static	int jpegcolormode = JPEGCOLORMODE_RGB;
static	int quality = 75;		/* JPEG quality */
static	uint16 predictor = 0;

static void swapBytesInScanline(unsigned char *, uint32, TIFFDataType);
static void usage(void);
static	int processCompressOptions(char*);

int
main(int argc, char* argv[])
{
	tsize_t	width = 0, length = 0, hdr_size = 0, linebytes, bufsize;
	int	nbands = 1;		/* number of bands in input image*/
	TIFFDataType dtype = TIFF_BYTE;
	int	depth = 1;		/* bytes per pixel in input image */
	int	swab = 0;		/* byte swapping flag */
	int	interleaving = 0;	/* interleaving type flag */
	uint32 rowsperstrip = (uint32) -1;
	uint16	photometric = PHOTOMETRIC_MINISBLACK;
	uint16	config = PLANARCONFIG_CONTIG;
	uint16	fillorder = FILLORDER_LSB2MSB;
	struct stat instat;
	FILE	*in;
	char	*outfilename = NULL;
	TIFF	*out;
	
	uint32 row, col, band;
	int	c;
	unsigned char *buf = NULL, *buf1 = NULL;
	extern int optind;
	extern char* optarg;
	

	while ((c = getopt(argc, argv, "c:r:H:w:l:b:d:LMp:si:o:h")) != -1)
		switch (c) {
		case 'c':		/* compression scheme */
			if (!processCompressOptions(optarg))
				usage();
			break;
		case 'r':		/* rows/strip */
			rowsperstrip = atoi(optarg);
			break;
		case 'H':		/* size of input image file header */
			hdr_size = atoi(optarg);
			break;
		case 'w':		/* input image width */
			width = atoi(optarg);
			break;
		case 'l':		/* input image length */
			length = atoi(optarg);
			break;
		case 'b':		/* number of bands in input image */
			nbands = atoi(optarg);
			break;
		case 'd':		/* type of samples in input image */
			if (strncmp(optarg, "byte", 4) == 0)
				dtype = TIFF_BYTE;
			else if (strncmp(optarg, "short", 5) == 0)
				dtype = TIFF_SHORT;
			else if  (strncmp(optarg, "long", 4) == 0)
				dtype = TIFF_LONG;
			else if  (strncmp(optarg, "sbyte", 5) == 0)
				dtype = TIFF_SBYTE;
			else if  (strncmp(optarg, "sshort", 6) == 0)
				dtype = TIFF_SSHORT;
			else if  (strncmp(optarg, "slong", 5) == 0)
				dtype = TIFF_SLONG;
			else if  (strncmp(optarg, "float", 5) == 0)
				dtype = TIFF_FLOAT;
			else if  (strncmp(optarg, "double", 6) == 0)
				dtype = TIFF_DOUBLE;
			else
				dtype = TIFF_BYTE;
			depth = TIFFDataWidth(dtype);
			break;
		case 'L':		/* input has lsb-to-msb fillorder */
			fillorder = FILLORDER_LSB2MSB;
			break;
		case 'M':		/* input has msb-to-lsb fillorder */
			fillorder = FILLORDER_MSB2LSB;
			break;
		case 'p':		/* photometric interpretation */
			if (strncmp(optarg, "miniswhite", 10) == 0)
				photometric = PHOTOMETRIC_MINISWHITE;
			else if (strncmp(optarg, "minisblack", 10) == 0)
				photometric = PHOTOMETRIC_MINISBLACK;
			else if (strncmp(optarg, "rgb", 3) == 0)
				photometric = PHOTOMETRIC_RGB;
			else if (strncmp(optarg, "cmyk", 4) == 0)
				photometric = PHOTOMETRIC_SEPARATED;
			else if (strncmp(optarg, "ycbcr", 5) == 0)
				photometric = PHOTOMETRIC_YCBCR;
			else if (strncmp(optarg, "cielab", 6) == 0)
				photometric = PHOTOMETRIC_CIELAB;
			else if (strncmp(optarg, "icclab", 6) == 0)
				photometric = PHOTOMETRIC_ICCLAB;
			else if (strncmp(optarg, "itulab", 6) == 0)
				photometric = PHOTOMETRIC_ITULAB;
			else
				photometric = PHOTOMETRIC_MINISBLACK;
			break;
		case 's':		/* do we need to swap bytes? */
			swab = 1;
			break;
		case 'i':		/* type of interleaving */
			if (strncmp(optarg, "pixel", 4) == 0)
				interleaving = 0;
			else if  (strncmp(optarg, "band", 6) == 0)
				interleaving = 1;
			else
				interleaving = 0;
			break;
		case 'o':
			outfilename = optarg;
			break;
		case 'h':
			usage();
		default:
			break;
		}
	if (argc - optind < 2)
		usage();
	in = fopen(argv[optind], "rb");
	if (in == NULL) {
		fprintf(stderr, "%s: %s: Cannot open input file.\n",
			argv[0], argv[optind]);
		return (-1);
	}
	stat(argv[optind], &instat);
	if (width == 0 ) {
		fprintf(stderr,
		"%s: %s: You should specify at least width of input image (use -w switch).\n",
		argv[0], argv[optind]);
		return (-1);
	}
	if (length == 0) {
		length = (instat.st_size - hdr_size) / (width * nbands * depth);
		fprintf(stderr,
			"%s: %s: Height is not specified, guessed as %d\n",
			argv[0], argv[optind], length);
	}
	if (instat.st_size < hdr_size + width * length * nbands * depth) {
		fprintf(stderr, "%s: %s: Input file too small.\n",
			argv[0], argv[optind]);
		return (-1);
	}
	
	if (outfilename == NULL)
		outfilename = argv[optind+1];
	out = TIFFOpen(outfilename, "w");
	if (out == NULL) {
		fprintf(stderr, "%s: %s: Cannot open file for output.\n",
			argv[0], outfilename);
		return (-1);
	}
	TIFFSetField(out, TIFFTAG_IMAGEWIDTH, width);
	TIFFSetField(out, TIFFTAG_IMAGELENGTH, length);
	TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, nbands);
	TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, depth * 8);
	TIFFSetField(out, TIFFTAG_FILLORDER, fillorder);
	TIFFSetField(out, TIFFTAG_PLANARCONFIG, config);
	TIFFSetField(out, TIFFTAG_PHOTOMETRIC, photometric);
	switch (dtype) {
	case TIFF_BYTE:
	case TIFF_SHORT:
	case TIFF_LONG:
		TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
		break;
	case TIFF_SBYTE:
	case TIFF_SSHORT:
	case TIFF_SLONG:
		TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_INT);
		break;
	case TIFF_FLOAT:
	case TIFF_DOUBLE:
		TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
		break;
	default:
		TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_VOID);
		break;
	}
	if (compression == (uint16) -1)
		compression = COMPRESSION_PACKBITS;
	TIFFSetField(out, TIFFTAG_COMPRESSION, compression);
	switch (compression) {
	case COMPRESSION_JPEG:
		if (photometric == PHOTOMETRIC_RGB && jpegcolormode == JPEGCOLORMODE_RGB)
			photometric = PHOTOMETRIC_YCBCR;
		TIFFSetField(out, TIFFTAG_JPEGQUALITY, quality);
		TIFFSetField(out, TIFFTAG_JPEGCOLORMODE, jpegcolormode);
		break;
	case COMPRESSION_LZW:
	case COMPRESSION_DEFLATE:
		if (predictor != 0)
			TIFFSetField(out, TIFFTAG_PREDICTOR, predictor);
		break;
	}
	switch(interleaving) {
	case 1:				/* band interleaved data */
		linebytes = width * depth;
		buf = (unsigned char *)_TIFFmalloc(linebytes);
		break;
	case 0:				/* pixel interleaved data */
	default:
		linebytes = width * nbands * depth;
		break;
	}
	bufsize = width * nbands * depth;
	buf1 = (unsigned char *)_TIFFmalloc(bufsize);
	TIFFSetField(out, TIFFTAG_ROWSPERSTRIP,
	    TIFFDefaultStripSize(out, rowsperstrip));
	fseek(in, hdr_size, SEEK_SET);		/* Skip the file header */
	for (row = 0; row < length; row++) {
		switch(interleaving) {
		case 1:				/* band interleaved data */
			for (band = 0; band < nbands; band++) {
				fseek(in,
					hdr_size + (length * band + row) * linebytes,
					SEEK_SET);
				if (fread(buf, linebytes, 1, in) != 1) {
					fprintf(stderr,
					"%s: %s: scanline %lu: Read error.\n",
					argv[0], argv[optind], (unsigned long) row);
				break;
				}
				if (swab)	/* Swap bytes if needed */
					swapBytesInScanline(buf, width, dtype);
				for (col = 0; col < width; col++)
					memcpy(buf1 + (col * nbands + band) * depth,
						buf + col * depth, depth);
			}
			break;
		case 0:				/* pixel interleaved data */
		default:
			if (fread(buf1, bufsize, 1, in) != 1) {
				fprintf(stderr, "%s: %s: scanline %lu: Read error.\n",
					argv[0], argv[optind], (unsigned long) row);
				break;
			}
			if (swab)		/* Swap bytes if needed */
				swapBytesInScanline(buf1, width, dtype);
			break;
		}
				
		if (TIFFWriteScanline(out, buf1, row, 0) < 0) {
			fprintf(stderr,	"%s: %s: scanline %lu: Write error.\n",
					argv[0], outfilename, (unsigned long) row);
			break;
		}
	}
	if (buf)
		_TIFFfree(buf);
	if (buf1)
		_TIFFfree(buf1);
	TIFFClose(out);
	return (0);
}

static void
swapBytesInScanline(unsigned char *buf, uint32 width, TIFFDataType dtype)
{
	switch(dtype) {
	case TIFF_SHORT:
	case TIFF_SSHORT:
		TIFFSwabArrayOfShort((uint16*)buf, width);
		break;
	case TIFF_LONG:
	case TIFF_SLONG:
		TIFFSwabArrayOfLong((uint32*)buf, width);
		break;
	/* case TIFF_FLOAT: */	/* FIXME */
	case TIFF_DOUBLE:
		TIFFSwabArrayOfDouble((double*)buf, width);
		break;
	default:
		break;
	}
}

static int
processCompressOptions(char* opt)
{
	if (strcmp(opt, "none") == 0)
		compression = COMPRESSION_NONE;
	else if (strcmp(opt, "packbits") == 0)
		compression = COMPRESSION_PACKBITS;
	else if (strncmp(opt, "jpeg", 4) == 0) {
		char* cp = strchr(opt, ':');
		if (cp && isdigit(cp[1]))
			quality = atoi(cp+1);
		if (cp && strchr(cp, 'r'))
			jpegcolormode = JPEGCOLORMODE_RAW;
		compression = COMPRESSION_JPEG;
	} else if (strncmp(opt, "lzw", 3) == 0) {
		char* cp = strchr(opt, ':');
		if (cp)
			predictor = atoi(cp+1);
		compression = COMPRESSION_LZW;
	} else if (strncmp(opt, "zip", 3) == 0) {
		char* cp = strchr(opt, ':');
		if (cp)
			predictor = atoi(cp+1);
		compression = COMPRESSION_DEFLATE;
	} else
		return (0);
	return (1);
}

char* stuff[] = {
"raw2tiff --- tool to converting raw byte sequences in TIFF images",
"usage: raw2tiff [options] input.raw output.tif",
"where options are:",
" -L		input data has LSB2MSB bit order (default)",
" -M		input data has MSB2LSB bit order",
" -r #		make each strip have no more than # rows",
" -H #		size of input image file header in bytes (0 by default)",
" -w #		width of input image in pixels (obligatory)",
" -l #		length of input image in lines",
" -b #		number of bands in input image (1 by default)",
"",
" -d data_type	type of samples in input image",
"where data_type may be:",
" byte		8-bit unsigned integer (default)",
" short		16-bit unsigned integer",
" long		32-bit unsigned integer",
" sbyte		8-bit signed integer",
" sshort		16-bit signed integer",
" slong		32-bit signed integer",
" float		32-bit IEEE floating point",
" double		64-bit IEEE floating point",
"",
" -p photo	photometric interpretation (color space) of the input image",
"where photo may be:",
" miniswhite	white color represented with 0 value",
" minisblack	black color represented with 0 value (default)",
" rgb		image has RGB color model",
" cmyk		image has CMYK (separated) color model",
" ycbcr		image has YCbCr color model",
" cielab		image has CIE L*a*b color model",
" icclab		image has ICC L*a*b color model",
" itulab		image has ITU L*a*b color model",
"",
" -s		swap bytes fetched from input file",
"",
" -i config	type of samples interleaving in input image",
"where config may be:",
" pixel		pixel interleaved data (default)",
" band		band interleaved data",
"",
" -c lzw[:opts]	compress output with Lempel-Ziv & Welch encoding",
"               (no longer supported by default due to Unisys patent enforcement)", 
" -c zip[:opts]	compress output with deflate encoding",
" -c jpeg[:opts]compress output with JPEG encoding",
" -c packbits	compress output with packbits encoding",
" -c none	use no compression algorithm on output",
"",
"JPEG options:",
" #		set compression quality level (0-100, default 75)",
" r		output color image as RGB rather than YCbCr",
"For example, -c jpeg:r:50 to get JPEG-encoded RGB data with 50% comp. quality",
"",
"LZW and deflate options:",
" #		set predictor value",
"For example, -c lzw:2 to get LZW-encoded data with horizontal differencing",
" -o out.tif	write output to out.tif",
" -h		this help message",
NULL
};

static void
usage(void)
{
	char buf[BUFSIZ];
	int i;

	setbuf(stderr, buf);
        fprintf(stderr, "%s\n\n", TIFFGetVersion());
	for (i = 0; stuff[i] != NULL; i++)
		fprintf(stderr, "%s\n", stuff[i]);
	exit(-1);
}

