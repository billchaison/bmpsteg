/*
   Using steganography to embed a file into an uncompressed
   24-bit RGB bitmap file.  Version 1.1

   obtain a copy: https://github.com/billchaison/bmpsteg

   compiling: gcc -o ./bmpsteg-lin ./bmpsteg-lin.c
*/
/*---------------------------------------------------------------------------
 This program is released under the "BSD Modified" license.

 Copyright (c) 2015, 2018 - Bill Chaison, all rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
 3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------*/

#pragma pack(2)

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define BUF_SIZE 8192 // enough to hold a scan line of about 2700 pixels wide.
#define MIN_DATA 12 // 3 bytes used to encode file length lo (RGB 24-bit pixel),
                    // 3 bytes used to encode file length hi (RGB 24-bit pixel),
                    // plus minimum of one char file to be embedded into a
                    // 3 byte pixel (RGB 24-bit pixel) is 9 bytes, with padding
                    // of 3 bytes is 12 bytes.  The number of bytes in each line
                    // of a .BMP file is always a multiple of 4.
#define MAX_DATA_FILE 65535 // greater than 65535 requires modifying the algorithm.
#define FILE_SIZE_PIXELS 2 // two pixels reserved to encode embedded file size.
#define BI_RGB 0
#define HDR_CHECKE_PASS 16
#define HDR_CHECKD_PASS 15

// BMP file header taken from MSDN.
typedef struct tagBITMAPFILEHEADER
{
	uint8_t  bfType[2];
	uint32_t bfSize;
	uint16_t bfReserved1;
	uint16_t bfReserved2;
	uint32_t bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;

// BMP information header taken from MSDN.
typedef struct tagBITMAPINFOHEADER
{
	uint32_t biSize;
	int32_t  biWidth;
	int32_t  biHeight;
	uint16_t biPlanes;
	uint16_t biBitCount;
	uint32_t biCompression;
	uint32_t biSizeImage;
	int32_t  biXPelsPerMeter;
	int32_t  biYPelsPerMeter;
	uint32_t biClrUsed;
	uint32_t biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct hdrCheck
{
	int nValid;
	int nBMPw;
	int nBMPh;
	int nBMPdlen;
	int nStride;
	int nPadding; // not used when output based on a source BMP.
	uint32_t dwFlags;
} HDRCHECK, *PHDRCHECK;

int usage(void);
uint8_t endian(void);
HDRCHECK validateheadere(void *p, int i, int j);
HDRCHECK validateheaderd(void *p, int i);
int encode(FILE *fBMPin, FILE *fDatain, FILE *fFileout, char *pBMPbufin, char *pDatabufin, HDRCHECK hc, int nFS2, int nRF);
int decode(FILE *fBMPin, FILE *fFileout, char *pBMPbufin, HDRCHECK hc);

int main(int argc, char **argv)
{
	int nFS1, nFS2, nRF = 0, e;
	char *pBMPin = NULL, *pFileout = NULL, *pDatain = NULL; // ASCIIZ file names.
	char *pBMPbufhdrin = NULL, *pBMPbufin = NULL, *pDatabufin = NULL; // pointers to file contents.
	FILE *fBMPin = NULL, *fDatain = NULL, *fFileout = NULL, *x = NULL;
	HDRCHECK hc;

	srand(time(NULL));
	// test the input.
	if(argc != 4 && argc != 6) { usage(); return -1; }
	if((*argv[1] != 'e' && *argv[1] != 'd') || strlen(argv[1]) != 1) { usage(); return -1; }
	if(*argv[1] == 'e' && argc != 6) { usage(); return -1; }
	if(*argv[1] == 'd' && argc != 4) { usage(); return -1; }
	if(*argv[1] == 'e')
	{
		if(!strcmp(argv[2], argv[3]) || !strcmp(argv[2], argv[4]) || !strcmp(argv[3], argv[4]))
		{
			fprintf(stderr, "ERROR: overlapping file names.\n");

			return -1;
		}
		if((*argv[5] != 'r' && *argv[5] != 'n' && *argv[5] != 'd' && *argv[5] != 'l') || strlen(argv[5]) != 1)
		{
			usage();

			return -1;
		}
	}
	if(*argv[1] == 'd')
	{
		if(!strcmp(argv[2], argv[3]))
		{
			fprintf(stderr, "ERROR: overlapping file names.\n");

			return -1;
		}
	}
	// ensure the system is little-endian.
	if(endian())
	{
		fprintf(stderr, "ERROR: big-endian system not supported.\n");

		return -1;
	}
	// test the input files.
	if(*argv[1] == 'e')
	{
		// prepare for encoding.
		pBMPin = argv[2];
		pFileout = argv[4];
		pDatain = argv[3];
		nFS1 = 0;
		if((x = fopen(pBMPin, "rb")) != NULL)
		{
			fseek(x, 0, SEEK_END);
			nFS1 = ftell(x);
			fclose(x);
		}
		if(nFS1 < 1)
		{
			fprintf(stderr, "ERROR: could not get size of <bmp in>.\n");

			return -1;
		}
		nFS2 = 0;
		if((x = fopen(pDatain, "rb")) != NULL)
		{
			fseek(x, 0, SEEK_END);
			nFS2 = ftell(x);
			fclose(x);
		}
		if(nFS2 < 1)
		{
			fprintf(stderr, "ERROR: could not get size of <data in>.\n");

			return -1;
		}
		if(nFS1 * nFS2 == 0 || nFS1 < (sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + MIN_DATA) || nFS2 > MAX_DATA_FILE)
		{
			// either the BMP or data file is empty, the BMP file is too small to embed even 1 character, or the data file is too big.
			fprintf(stderr, "ERROR: bad file size.\n");

			return -1;
		}
		if((fBMPin = fopen(pBMPin, "rb")) == NULL)
		{
			fprintf(stderr, "ERROR: unable to open <bmp in>.\n");

			return -1;
		}
		if((fDatain = fopen(pDatain, "rb")) == NULL)
		{
			fprintf(stderr, "ERROR: unable to open <data in>.\n");
			fclose(fBMPin);

			return -1;
		}
		if((pBMPbufhdrin = (char *)malloc(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))) == NULL)
		{
			fprintf(stderr, "ERROR: unable to allocate buffer for <bmp in> header.\n");
			fclose(fBMPin);
			fclose(fDatain);

			return -1;
		}
		if((pBMPbufin = (char *)malloc(BUF_SIZE)) == NULL)
		{
			fprintf(stderr, "ERROR: unable to allocate buffer for <bmp in> data.\n");
			fclose(fBMPin);
			fclose(fDatain);
			free(pBMPbufhdrin);

			return -1;
		}
		if(fread(pBMPbufhdrin, 1, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), fBMPin) != sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
		{
			fprintf(stderr, "ERROR: unable to read <bmp in> headers.\n");
			fclose(fBMPin);
			fclose(fDatain);
			free(pBMPbufhdrin);
			free(pBMPbufin);

			return -1;
		}
		// sanity check the headers.
		hc = validateheadere(pBMPbufhdrin, nFS1, nFS2);
		if(hc.nValid != HDR_CHECKE_PASS)
		{
			fprintf(stderr, "ERROR: <bmp in> header check failed (%08X).\n", hc.dwFlags);
			fclose(fBMPin);
			fclose(fDatain);
			free(pBMPbufhdrin);
			free(pBMPbufin);

			return -1;
		}
		if((pDatabufin = (char *)malloc(BUF_SIZE)) == NULL)
		{
			fprintf(stderr, "ERROR: unable to allocate buffer for <data in> data.\n");
			fclose(fBMPin);
			fclose(fDatain);
			free(pBMPbufhdrin);
			free(pBMPbufin);

			return -1;
		}
		if((fFileout = fopen(pFileout, "wb")) == NULL)
		{
			fprintf(stderr, "ERROR: unable to open <bmp out>.\n");
			fclose(fBMPin);
			fclose(fDatain);
			free(pBMPbufhdrin);
			free(pBMPbufin);
			free(pDatabufin);

			return -1;
		}
		if(fwrite(pBMPbufhdrin, 1, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), fFileout) != sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
		{
			fprintf(stderr, "ERROR: unable to write <bmp out> header.\n");
			fclose(fBMPin);
			fclose(fDatain);
			fclose(fFileout);
			free(pBMPbufhdrin);
			free(pBMPbufin);
			free(pDatabufin);
			remove(pFileout);

			return -1;
		}
		if(*argv[5] == 'r') nRF = 1;
		if(*argv[5] == 'd') nRF = 2;
		if(*argv[5] == 'l') nRF = 3;
		if((e = encode(fBMPin, fDatain, fFileout, pBMPbufin, pDatabufin, hc, nFS2, nRF)) != 0)
		{
			fprintf(stderr, "ERROR: unable to encode <bmp out> file, code %d.\n", e);
			fclose(fBMPin);
			fclose(fDatain);
			fclose(fFileout);
			free(pBMPbufhdrin);
			free(pBMPbufin);
			free(pDatabufin);
			remove(pFileout);

			return -1;
		}
		else
		{
			fclose(fBMPin);
			fclose(fDatain);
			fclose(fFileout);
			free(pBMPbufhdrin);
			free(pBMPbufin);
			free(pDatabufin);
		}
	}
	else
	{
		// prepare for decoding.
		pBMPin = argv[2];
		pFileout = argv[3];
		nFS1 = 0;
		if((x = fopen(pBMPin, "rb")) != NULL)
		{
			fseek(x, 0, SEEK_END);
			nFS1 = ftell(x);
			fclose(x);
		}
		if(nFS1 < 1)
		{
			fprintf(stderr, "ERROR: could not get size of <bmp in>.\n");

			return -1;
		}
		if(nFS1 < (sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + MIN_DATA))
		{
			// the BMP file is too small to embed even 1 character.
			fprintf(stderr, "ERROR: bad file size.\n");

			return -1;
		}
		if((fBMPin = fopen(pBMPin, "rb")) == NULL)
		{
			fprintf(stderr, "ERROR: unable to open <bmp in>.\n");

			return -1;
		}
		if((pBMPbufhdrin = (char *)malloc(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))) == NULL)
		{
			fprintf(stderr, "ERROR: unable to allocate buffer for <bmp in> header.\n");
			fclose(fBMPin);

			return -1;
		}
		if((pBMPbufin = (char *)malloc(BUF_SIZE)) == NULL)
		{
			fprintf(stderr, "ERROR: unable to allocate buffer for <bmp in> data.\n");
			fclose(fBMPin);
			free(pBMPbufhdrin);

			return -1;
		}
		if(fread(pBMPbufhdrin, 1, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), fBMPin) != sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
		{
			fprintf(stderr, "ERROR: unable to read <bmp in> headers.\n");
			fclose(fBMPin);
			free(pBMPbufhdrin);
			free(pBMPbufin);

			return -1;
		}
		// sanity check the headers.
		hc = validateheaderd(pBMPbufhdrin, nFS1);
		if(hc.nValid != HDR_CHECKD_PASS)
		{
			fprintf(stderr, "ERROR: <bmp in> header check failed (%08X).\n", hc.dwFlags);
			fclose(fBMPin);
			free(pBMPbufhdrin);
			free(pBMPbufin);

			return -1;
		}
		if((fFileout = fopen(pFileout, "wb")) == NULL)
		{
			fprintf(stderr, "ERROR: unable to open <data out>.\n");
			fclose(fBMPin);
			free(pBMPbufhdrin);
			free(pBMPbufin);

			return -1;
		}
		if((e = decode(fBMPin, fFileout, pBMPbufin, hc)) != 0)
		{
			fprintf(stderr, "ERROR: unable to encode <data out> file, code %d.\n", e);
			fclose(fBMPin);
			fclose(fFileout);
			free(pBMPbufhdrin);
			free(pBMPbufin);
			remove(pFileout);

			return -1;
		}
		else
		{
			fclose(fBMPin);
			fclose(fFileout);
			free(pBMPbufhdrin);
			free(pBMPbufin);
		}
	}

	return 0;
}

int usage()
{
	// print the command line options.
	fprintf(stderr, "Usage: bmpsteg-lin <mode e> <bmp in> <data in> <bmp out> <fill>\n");
	fprintf(stderr, "       bmpsteg-lin <mode d> <bmp in> <data out>\n\n");
	fprintf(stderr, "<mode> The mode of operation, either e or d. Mode e encodes <bmp in> with\n");
	fprintf(stderr, "       bytes from <data in> and stores the results in <bmp out>.  Mode d\n");
	fprintf(stderr, "       decodes the embedded data from <bmp in> and stores the results in\n");
	fprintf(stderr, "       the file specified by <data out>.\n");
	fprintf(stderr, "<fill> This is only used when <mode> is e and helps to hide visible artifacts\n");
	fprintf(stderr, "       by inserting random bits into unused pixels. This parameter is either\n");
	fprintf(stderr, "       r for random, d for random dark bias, l for random light bias or n for\n");
	fprintf(stderr, "       no fill. If <bmp out> shows banding visually then experiment with these\n");
	fprintf(stderr, "       parameters to produce less noticeable artifacts.\n\n");
	fprintf(stderr, "(Examples)\n");
	fprintf(stderr, "Encode: bmpsteg-lin e /dir/img.in.bmp /dir/doc.in.txt /dir/img.out.bmp r\n");
	fprintf(stderr, "Decode: bmpsteg-lin d /dir/img.out.bmp /dir/doc.out.txt\n\n");
	fprintf(stderr, "The <bmp in> file must be a 24-bit uncompressed RGB bitmap without color space\n");
	fprintf(stderr, "information. Max size of <data in> is %d bytes.\n\nReleased under the \"BSD Modified\" license, Bill Chaison (c) 2018.\n", MAX_DATA_FILE);

	return 0;
}

uint8_t endian(void)
{
	// returns 1 for big-endian and 0 for little-endian
	union
	{
		uint16_t w;
		uint8_t  c[2];
	} e = { 0x0100 };

	return e.c[0];
}

HDRCHECK validateheadere(void *p, int i, int j)
{
	// sanity check the BMP headers for encode.
	HDRCHECK hc = { 0 };
	PBITMAPFILEHEADER pBMPhdrin;
	PBITMAPINFOHEADER pBMPinfoin;

	pBMPhdrin = (PBITMAPFILEHEADER)p;
	pBMPinfoin = (PBITMAPINFOHEADER)(p + sizeof(BITMAPFILEHEADER));

	if(pBMPhdrin->bfType[0] == 'B') { hc.nValid++; hc.dwFlags |= 1; } // BMP signature valid.
	if(pBMPhdrin->bfType[1] == 'M') { hc.nValid++; hc.dwFlags |= 2; } // BMP signature valid.
	if((uint32_t)pBMPhdrin->bfReserved1 == 0) { hc.nValid++; hc.dwFlags |= 4; } // reserved bytes valid.
	if(pBMPhdrin->bfOffBits == (uint32_t)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))) { hc.nValid++; hc.dwFlags |= 8; } // data offset valid.
	if(pBMPinfoin->biSize == (uint32_t)sizeof(BITMAPINFOHEADER)) { hc.nValid++; hc.dwFlags |= 16; } // BMP info header size valid.
	hc.nBMPw = pBMPinfoin->biWidth;
	hc.nBMPh = abs(pBMPinfoin->biHeight); // remove sign, origin not important.
	if(hc.nBMPw > 0 && hc.nBMPh > 0) { hc.nValid++; hc.dwFlags |= 32; } // pixel width and height valid.
	if(pBMPinfoin->biPlanes == 1) { hc.nValid++; hc.dwFlags |= 64; } // BMP planes valid.
	if(pBMPinfoin->biBitCount == 24) { hc.nValid++; hc.dwFlags |= 128; } // bits per pixel valid.
	if(pBMPinfoin->biCompression == BI_RGB) { hc.nValid++; hc.dwFlags |= 256; } // uncompressed RGB valid.
	hc.nBMPdlen = i - sizeof(BITMAPFILEHEADER) - sizeof(BITMAPINFOHEADER);
	if(pBMPinfoin->biSizeImage == 0 || pBMPinfoin->biSizeImage == hc.nBMPdlen) { hc.nValid++; hc.dwFlags |= 512; } // image data struct size valid.
	if(pBMPinfoin->biClrUsed == 0) { hc.nValid++; hc.dwFlags |= 1024; } // valid for BMP file.
	if(pBMPinfoin->biClrImportant == 0) { hc.nValid++; hc.dwFlags |= 2048; } // valid for BMP file.
	hc.nStride = (((hc.nBMPw * pBMPinfoin->biBitCount) + 31) & ~31) >> 3;
	if(!(hc.nStride % 4) && hc.nStride < BUF_SIZE) { hc.nValid++; hc.dwFlags |= 4096; } // scan line is a multiple of 4 and not too big.
	hc.nPadding = hc.nStride - (hc.nBMPw * 3);
	if(hc.nPadding > -1) { hc.nValid++; hc.dwFlags |= 8192; } // null padding value is valid.
	if(hc.nBMPdlen == (hc.nBMPh * hc.nStride)) { hc.nValid++; hc.dwFlags |= 16384; } // image file data length is valid for scan line.
	if(((hc.nBMPw * hc.nBMPh) - FILE_SIZE_PIXELS) >= j) { hc.nValid++; hc.dwFlags |= 32768; } // data file is not too big to embed into the given BMP file.

	return hc;
}

HDRCHECK validateheaderd(void *p, int i)
{
	// sanity check the BMP headers for decode.
	HDRCHECK hc = { 0 };
	PBITMAPFILEHEADER pBMPhdrin;
	PBITMAPINFOHEADER pBMPinfoin;

	pBMPhdrin = (PBITMAPFILEHEADER)p;
	pBMPinfoin = (PBITMAPINFOHEADER)(p + sizeof(BITMAPFILEHEADER));

	if(pBMPhdrin->bfType[0] == 'B') { hc.nValid++; hc.dwFlags |= 1; } // BMP signature valid.
	if(pBMPhdrin->bfType[1] == 'M') { hc.nValid++; hc.dwFlags |= 2; } // BMP signature valid.
	if((uint32_t)pBMPhdrin->bfReserved1 == 0) { hc.nValid++; hc.dwFlags |= 4; } // reserved bytes valid.
	if(pBMPhdrin->bfOffBits == (uint32_t)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))) { hc.nValid++; hc.dwFlags |= 8; } // data offset valid.
	if(pBMPinfoin->biSize == (uint32_t)sizeof(BITMAPINFOHEADER)) { hc.nValid++; hc.dwFlags |= 16; } // BMP info header size valid.
	hc.nBMPw = pBMPinfoin->biWidth;
	hc.nBMPh = abs(pBMPinfoin->biHeight); // remove sign, origin not important.
	if((hc.nBMPw * hc.nBMPh) > 2) { hc.nValid++; hc.dwFlags |= 32; } // pixel width and height valid for at least one char.
	if(pBMPinfoin->biPlanes == 1) { hc.nValid++; hc.dwFlags |= 64; } // BMP planes valid.
	if(pBMPinfoin->biBitCount == 24) { hc.nValid++; hc.dwFlags |= 128; } // bits per pixel valid.
	if(pBMPinfoin->biCompression == BI_RGB) { hc.nValid++; hc.dwFlags |= 256; } // uncompressed RGB valid.
	hc.nBMPdlen = i - sizeof(BITMAPFILEHEADER) - sizeof(BITMAPINFOHEADER);
	if(pBMPinfoin->biSizeImage == 0 || pBMPinfoin->biSizeImage == hc.nBMPdlen) { hc.nValid++; hc.dwFlags |= 512; } // image data struct size valid.
	if(pBMPinfoin->biClrUsed == 0) { hc.nValid++; hc.dwFlags |= 1024; } // valid for BMP file.
	if(pBMPinfoin->biClrImportant == 0) { hc.nValid++; hc.dwFlags |= 2048; } // valid for BMP file.
	hc.nStride = (((hc.nBMPw * pBMPinfoin->biBitCount) + 31) & ~31) >> 3;
	if(!(hc.nStride % 4) && hc.nStride < BUF_SIZE) { hc.nValid++; hc.dwFlags |= 4096; } // scan line is a multiple of 4 and not too big.
	hc.nPadding = hc.nStride - (hc.nBMPw * 3);
	if(hc.nPadding > -1) { hc.nValid++; hc.dwFlags |= 8192; } // null padding value is valid.
	if(hc.nBMPdlen == (hc.nBMPh * hc.nStride)) { hc.nValid++; hc.dwFlags |= 16384; } // image file data length is valid for scan line.

	return hc;
}

int encode(FILE *fBMPin, FILE *fDatain, FILE *fFileout, char *pBMPbufin, char *pDatabufin, HDRCHECK hc, int nFS2, int nRF)
{
	// encodes <bmp out> from <bmp in> and <data in>.
	// Each byte from <data in> is spread across low-order BGR bits.
	// Some studies suggest that the eye is more sensitive to changes
	// in green. So only 2 bits will be robbed from G, while 3 bits
	// will be robbed from B and R. 2 bits represents 1.5% of the
	// color space, 3 bits represents 3.1% of the color space.
	// The first two pixels store the number of bytes embedded in
	// the remaining pixels.
	//  fBMPin at start of image data in <bmp in>.
	//  fDatain at head of <data in>.
	//  fFileout at start of image data of <bmp out>.
	//  pBMPbufin head of buffer to read scan lines from <bmp in>.
	//  pDatabufin head of buffer to read bytes from <data in>.
	//  hc context values for reading, writing and encoding.
	//  nFS1 size of entire <bmp in> file.
	//  nFS2 size of entire <data in> file.
	//  nRF 1 to random fill unused bytes, 2 for dark fill, 3 for
	//      light fill, 0 no fill.
	// BMP data starts at the bottom lefthand corner of the image.
	union
	{
		uint16_t w;
		uint8_t c[2];
	} dfs;
	int wpels, hpels, state = 0;
	char *pC; // BGR pixel pointer in current stride.
	uint8_t dibyte, mask = 0x29; // B_G_R encoding mask in binary = 001_01_001
                                     // white = 0xffffff, black = 0x000000

	dfs.w = (uint16_t)nFS2;
	wpels = hc.nBMPw;
	hpels = hc.nBMPh;
	pC = pBMPbufin;
	if(fread(pBMPbufin, 1, hc.nStride, fBMPin) != hc.nStride) return -1;
	// encode low-order byte of <data in> file size.
	*pC &= 0xf8;
	*(pC + 1) &= 0xfc;
	*(pC + 2) &= 0xf8;
	*pC |= dfs.c[0] & 0x7;
	*(pC + 1) |= (dfs.c[0] >> 3) & 0x3;
	*(pC + 2) |= (dfs.c[0] >> 5) & 0x7;
	if(--wpels)
	{
		// encode high-order byte of <data in> file size.
		pC += 3;
		*pC &= 0xf8;
		*(pC + 1) &= 0xfc;
		*(pC + 2) &= 0xf8;
		*pC |= dfs.c[1] & 0x7;
		*(pC + 1) |= (dfs.c[1] >> 3) & 0x3;
		*(pC + 2) |= (dfs.c[1] >> 5) & 0x7;
	}
	else
	{
		// is a 1 pixel wide BMP, write updated pixel and read new scan line.
		// encode high-order byte of <data in> file size.
		if(--hpels == 0) return -2;
		wpels = hc.nBMPw;
		if(fwrite(pBMPbufin, 1, hc.nStride, fFileout) != hc.nStride) return -3;
		if(fread(pBMPbufin, 1, hc.nStride, fBMPin) != hc.nStride) return -4;
		*pC &= 0xf8;
		*(pC + 1) &= 0xfc;
		*(pC + 2) &= 0xf8;
		*pC |= dfs.c[1] & 0x7;
		*(pC + 1) |= (dfs.c[1] >> 3) & 0x3;
		*(pC + 2) |= (dfs.c[1] >> 5) & 0x7;
	}
	// file size of <data in> encoding complete.
	if(--wpels)
	{
		// is at least a 3 pixel wide BMP. first <data in> byte write to current scan line.
		pC += 3;
	}
	else
	{
		// is a 1 or 2 pixel wide BMP, write updated pixels and read new scan line.
		if(--hpels == 0) return -5;
		wpels = hc.nBMPw;
		if(fwrite(pBMPbufin, 1, hc.nStride, fFileout) != hc.nStride) return -6;
		if(fread(pBMPbufin, 1, hc.nStride, fBMPin) != hc.nStride) return -7;
		pC = pBMPbufin;
	}
	// encode <data in> bytes into <bmp in> scan lines. pC points to next BGR to modify.
	while(hpels)
	{
		// state == 0 use <data in> byte, 1 rand fill, 2 dark fill, 3 light fill, -1 no fill
		switch(state)
		{
			case 0:
				if(fread(pDatabufin, 1, 1, fDatain) != 1)
				{
					// no more data to read, gen random byte.
					state = (nRF ? nRF : -1);
					dibyte = (uint8_t)rand();
					if(state == 2) dibyte &= mask; // darken (more 0s)
					if(state == 3) dibyte |= ~mask; // lighten (more 1s)
				}
				else
				{
					dibyte = *pDatabufin;
				}
				break;
			case 1:
					dibyte = (uint8_t)rand();
				break;
			case 2:
					dibyte = (uint8_t)rand();
					dibyte &= mask; // darken (more 0s)
				break;
			case 3:
					dibyte = (uint8_t)rand();
					dibyte |= ~mask; // lighten (more 1s)
				break;
			case 4:
				// do nothing
				break;
			default:
				break;
		}
		if(state > -1)
		{
			// fill with read or generated byte.
			*pC &= 0xf8;
			*(pC + 1) &= 0xfc;
			*(pC + 2) &= 0xf8;
			*pC |= dibyte & 0x7;
			*(pC + 1) |= (dibyte >> 3) & 0x3;
			*(pC + 2) |= (dibyte >> 5) & 0x7;
		}
		if(--wpels)
		{
			// more pixels in the current scan line
			pC += 3;
		}
		else
		{
			// write updated pixels and read new scan line.
			wpels = hc.nBMPw;
			if(fwrite(pBMPbufin, 1, hc.nStride, fFileout) != hc.nStride) return -8;
			if(--hpels)
			{
				if(fread(pBMPbufin, 1, hc.nStride, fBMPin) != hc.nStride) return -9;
				pC = pBMPbufin;
			}
		}
	}

	return 0;
}

int decode(FILE *fBMPin, FILE *fFileout, char *pBMPbufin, HDRCHECK hc)
{
	// decodes <data out> from <bmp in>.
	//  fBMPin at start of image data in <bmp in>.
	union
	{
		uint16_t w;
		uint8_t c[2];
	} dfs;
	int wpels, hpels;
	char *pC; // BGR pixel pointer in current stride.
	uint8_t dobyte;

	wpels = hc.nBMPw;
	hpels = hc.nBMPh;
	pC = pBMPbufin;
	if(fread(pBMPbufin, 1, hc.nStride, fBMPin) != hc.nStride) return -1;
	// decode embedded data size low-order byte.
	dfs.c[0] = 0;
	dfs.c[0] |= *pC & 0x7;
	dfs.c[0] |= (*(pC + 1) & 0x3) << 3;
	dfs.c[0] |= (*(pC + 2) & 0x7) << 5;
	// decode embedded data high-order byte.
	if(--wpels)
	{
		// is not 1 pixel wide, get next BGR.
		pC += 3;
		dfs.c[1] = 0;
		dfs.c[1] |= *pC & 0x7;
		dfs.c[1] |= (*(pC + 1) & 0x3) << 3;
		dfs.c[1] |= (*(pC + 2) & 0x7) << 5;
	}
	else
	{
		// is a 1 pixel wide BMP read new scan line.
		if(--hpels == 0) return -2;
		wpels = hc.nBMPw;
		if(fread(pBMPbufin, 1, hc.nStride, fBMPin) != hc.nStride) return -3;
		dfs.c[1] = 0;
		dfs.c[1] |= *pC & 0x7;
		dfs.c[1] |= (*(pC + 1) & 0x3) << 3;
		dfs.c[1] |= (*(pC + 2) & 0x7) << 5;
	}
	if((hc.nBMPw * hc.nBMPh) - FILE_SIZE_PIXELS < dfs.w) return -4;
	while(hpels)
	{
		dobyte = 0;
		if(--wpels)
		{
			// more pixels in the current scan line
			pC += 3;
			dobyte |= *pC & 0x7;
			dobyte |= (*(pC + 1) & 0x3) << 3;
			dobyte |= (*(pC + 2) & 0x7) << 5;
		}
		else
		{
			// read new scan line.
			if(--hpels == 0) return -5;
			wpels = hc.nBMPw;
			pC = pBMPbufin;
			if(fread(pBMPbufin, 1, hc.nStride, fBMPin) != hc.nStride) return -6;
			dobyte |= *pC & 0x7;
			dobyte |= (*(pC + 1) & 0x3) << 3;
			dobyte |= (*(pC + 2) & 0x7) << 5;
		}
		if(fwrite(&dobyte, 1, 1, fFileout) != 1) return -7;
		if(--dfs.w == 0) break;
	}

	return 0;
}

