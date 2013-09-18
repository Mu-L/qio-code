/*
============================================================================
Copyright (C) 2012-2013 V.

This file is part of Qio source code.

Qio source code is free software; you can redistribute it 
and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

Qio source code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA,
or simply visit <http://www.gnu.org/licenses/>.
============================================================================
*/
// img_convert.cpp - image converting functions
#include "img_local.h"
#include <api/coreAPI.h>
#include <stdlib.h> // malloc

static const byte g_quake1DefaultPalette[256][3] = {
	{0x00, 0x00, 0x00}, {0x0f, 0x0f, 0x0f}, {0x1f, 0x1f, 0x1f}, {0x2f, 0x2f, 0x2f},
	{0x3f, 0x3f, 0x3f}, {0x4b, 0x4b, 0x4b}, {0x5b, 0x5b, 0x5b}, {0x6b, 0x6b, 0x6b},
	{0x7b, 0x7b, 0x7b}, {0x8b, 0x8b, 0x8b}, {0x9b, 0x9b, 0x9b}, {0xab, 0xab, 0xab},
	{0xbb, 0xbb, 0xbb}, {0xcb, 0xcb, 0xcb}, {0xdb, 0xdb, 0xdb}, {0xeb, 0xeb, 0xeb},
	{0x07, 0x0b, 0x0f}, {0x0b, 0x0f, 0x17}, {0x0b, 0x17, 0x1f}, {0x0f, 0x1b, 0x27},
	{0x13, 0x23, 0x2f}, {0x17, 0x2b, 0x37}, {0x17, 0x2f, 0x3f}, {0x1b, 0x37, 0x4b},
	{0x1b, 0x3b, 0x53}, {0x1f, 0x43, 0x5b}, {0x1f, 0x4b, 0x63}, {0x1f, 0x53, 0x6b},
	{0x1f, 0x57, 0x73}, {0x23, 0x5f, 0x7b}, {0x23, 0x67, 0x83}, {0x23, 0x6f, 0x8f},
	{0x0f, 0x0b, 0x0b}, {0x1b, 0x13, 0x13}, {0x27, 0x1b, 0x1b}, {0x33, 0x27, 0x27},
	{0x3f, 0x2f, 0x2f}, {0x4b, 0x37, 0x37}, {0x57, 0x3f, 0x3f}, {0x67, 0x47, 0x47},
	{0x73, 0x4f, 0x4f}, {0x7f, 0x5b, 0x5b}, {0x8b, 0x63, 0x63}, {0x97, 0x6b, 0x6b},
	{0xa3, 0x73, 0x73}, {0xaf, 0x7b, 0x7b}, {0xbb, 0x83, 0x83}, {0xcb, 0x8b, 0x8b},
	{0x00, 0x00, 0x00}, {0x00, 0x07, 0x07}, {0x00, 0x0b, 0x0b}, {0x00, 0x13, 0x13},
	{0x00, 0x1b, 0x1b}, {0x00, 0x23, 0x23}, {0x07, 0x2b, 0x2b}, {0x07, 0x2f, 0x2f},
	{0x07, 0x37, 0x37}, {0x07, 0x3f, 0x3f}, {0x07, 0x47, 0x47}, {0x0b, 0x4b, 0x4b},
	{0x0b, 0x53, 0x53}, {0x0b, 0x5b, 0x5b}, {0x0b, 0x63, 0x63}, {0x0f, 0x6b, 0x6b},
	{0x00, 0x00, 0x07}, {0x00, 0x00, 0x0f}, {0x00, 0x00, 0x17}, {0x00, 0x00, 0x1f},
	{0x00, 0x00, 0x27}, {0x00, 0x00, 0x2f}, {0x00, 0x00, 0x37}, {0x00, 0x00, 0x3f},
	{0x00, 0x00, 0x47}, {0x00, 0x00, 0x4f}, {0x00, 0x00, 0x57}, {0x00, 0x00, 0x5f},
	{0x00, 0x00, 0x67}, {0x00, 0x00, 0x6f}, {0x00, 0x00, 0x77}, {0x00, 0x00, 0x7f},
	{0x00, 0x13, 0x13}, {0x00, 0x1b, 0x1b}, {0x00, 0x23, 0x23}, {0x00, 0x2b, 0x2f},
	{0x00, 0x2f, 0x37}, {0x00, 0x37, 0x43}, {0x07, 0x3b, 0x4b}, {0x07, 0x43, 0x57},
	{0x07, 0x47, 0x5f}, {0x0b, 0x4b, 0x6b}, {0x0f, 0x53, 0x77}, {0x13, 0x57, 0x83},
	{0x13, 0x5b, 0x8b}, {0x1b, 0x5f, 0x97}, {0x1f, 0x63, 0xa3}, {0x23, 0x67, 0xaf},
	{0x07, 0x13, 0x23}, {0x0b, 0x17, 0x2f}, {0x0f, 0x1f, 0x3b}, {0x13, 0x23, 0x4b},
	{0x17, 0x2b, 0x57}, {0x1f, 0x2f, 0x63}, {0x23, 0x37, 0x73}, {0x2b, 0x3b, 0x7f},
	{0x33, 0x43, 0x8f}, {0x33, 0x4f, 0x9f}, {0x2f, 0x63, 0xaf}, {0x2f, 0x77, 0xbf},
	{0x2b, 0x8f, 0xcf}, {0x27, 0xab, 0xdf}, {0x1f, 0xcb, 0xef}, {0x1b, 0xf3, 0xff},
	{0x00, 0x07, 0x0b}, {0x00, 0x13, 0x1b}, {0x0f, 0x23, 0x2b}, {0x13, 0x2b, 0x37},
	{0x1b, 0x33, 0x47}, {0x23, 0x37, 0x53}, {0x2b, 0x3f, 0x63}, {0x33, 0x47, 0x6f},
	{0x3f, 0x53, 0x7f}, {0x47, 0x5f, 0x8b}, {0x53, 0x6b, 0x9b}, {0x5f, 0x7b, 0xa7},
	{0x6b, 0x87, 0xb7}, {0x7b, 0x93, 0xc3}, {0x8b, 0xa3, 0xd3}, {0x97, 0xb3, 0xe3},
	{0xa3, 0x8b, 0xab}, {0x97, 0x7f, 0x9f}, {0x87, 0x73, 0x93}, {0x7b, 0x67, 0x8b},
	{0x6f, 0x5b, 0x7f}, {0x63, 0x53, 0x77}, {0x57, 0x4b, 0x6b}, {0x4b, 0x3f, 0x5f},
	{0x43, 0x37, 0x57}, {0x37, 0x2f, 0x4b}, {0x2f, 0x27, 0x43}, {0x23, 0x1f, 0x37},
	{0x1b, 0x17, 0x2b}, {0x13, 0x13, 0x23}, {0x0b, 0x0b, 0x17}, {0x07, 0x07, 0x0f},
	{0x9f, 0x73, 0xbb}, {0x8f, 0x6b, 0xaf}, {0x83, 0x5f, 0xa3}, {0x77, 0x57, 0x97},
	{0x6b, 0x4f, 0x8b}, {0x5f, 0x4b, 0x7f}, {0x53, 0x43, 0x73}, {0x4b, 0x3b, 0x6b},
	{0x3f, 0x33, 0x5f}, {0x37, 0x2b, 0x53}, {0x2b, 0x23, 0x47}, {0x23, 0x1f, 0x3b},
	{0x1b, 0x17, 0x2f}, {0x13, 0x13, 0x23}, {0x0b, 0x0b, 0x17}, {0x07, 0x07, 0x0f},
	{0xbb, 0xc3, 0xdb}, {0xa7, 0xb3, 0xcb}, {0x9b, 0xa3, 0xbf}, {0x8b, 0x97, 0xaf},
	{0x7b, 0x87, 0xa3}, {0x6f, 0x7b, 0x97}, {0x5f, 0x6f, 0x87}, {0x53, 0x63, 0x7b},
	{0x47, 0x57, 0x6b}, {0x3b, 0x4b, 0x5f}, {0x33, 0x3f, 0x53}, {0x27, 0x33, 0x43},
	{0x1f, 0x2b, 0x37}, {0x17, 0x1f, 0x27}, {0x0f, 0x13, 0x1b}, {0x07, 0x0b, 0x0f},
	{0x7b, 0x83, 0x6f}, {0x6f, 0x7b, 0x67}, {0x67, 0x73, 0x5f}, {0x5f, 0x6b, 0x57},
	{0x57, 0x63, 0x4f}, {0x4f, 0x5b, 0x47}, {0x47, 0x53, 0x3f}, {0x3f, 0x4b, 0x37},
	{0x37, 0x43, 0x2f}, {0x2f, 0x3b, 0x2b}, {0x27, 0x33, 0x23}, {0x1f, 0x2b, 0x1f},
	{0x17, 0x23, 0x17}, {0x13, 0x1b, 0x0f}, {0x0b, 0x13, 0x0b}, {0x07, 0x0b, 0x07},
	{0x1b, 0xf3, 0xff}, {0x17, 0xdf, 0xef}, {0x13, 0xcb, 0xdb}, {0x0f, 0xb7, 0xcb},
	{0x0f, 0xa7, 0xbb}, {0x0b, 0x97, 0xab}, {0x07, 0x83, 0x9b}, {0x07, 0x73, 0x8b},
	{0x07, 0x63, 0x7b}, {0x00, 0x53, 0x6b}, {0x00, 0x47, 0x5b}, {0x00, 0x37, 0x4b},
	{0x00, 0x2b, 0x3b}, {0x00, 0x1f, 0x2b}, {0x00, 0x0f, 0x1b}, {0x00, 0x07, 0x0b},
	{0xff, 0x00, 0x00}, {0xef, 0x0b, 0x0b}, {0xdf, 0x13, 0x13}, {0xcf, 0x1b, 0x1b},
	{0xbf, 0x23, 0x23}, {0xaf, 0x2b, 0x2b}, {0x9f, 0x2f, 0x2f}, {0x8f, 0x2f, 0x2f},
	{0x7f, 0x2f, 0x2f}, {0x6f, 0x2f, 0x2f}, {0x5f, 0x2f, 0x2f}, {0x4f, 0x2b, 0x2b},
	{0x3f, 0x23, 0x23}, {0x2f, 0x1b, 0x1b}, {0x1f, 0x13, 0x13}, {0x0f, 0x0b, 0x0b},
	{0x00, 0x00, 0x2b}, {0x00, 0x00, 0x3b}, {0x00, 0x07, 0x4b}, {0x00, 0x07, 0x5f},
	{0x00, 0x0f, 0x6f}, {0x07, 0x17, 0x7f}, {0x07, 0x1f, 0x93}, {0x0b, 0x27, 0xa3},
	{0x0f, 0x33, 0xb7}, {0x1b, 0x4b, 0xc3}, {0x2b, 0x63, 0xcf}, {0x3b, 0x7f, 0xdb},
	{0x4f, 0x97, 0xe3}, {0x5f, 0xab, 0xe7}, {0x77, 0xbf, 0xef}, {0x8b, 0xd3, 0xf7},
	{0x3b, 0x7b, 0xa7}, {0x37, 0x9b, 0xb7}, {0x37, 0xc3, 0xc7}, {0x57, 0xe3, 0xe7},
	{0xff, 0xbf, 0x7f}, {0xff, 0xe7, 0xab}, {0xff, 0xff, 0xd7}, {0x00, 0x00, 0x67},
	{0x00, 0x00, 0x8b}, {0x00, 0x00, 0xb3}, {0x00, 0x00, 0xd7}, {0x00, 0x00, 0xff},
	{0x93, 0xf3, 0xff}, {0xc7, 0xf7, 0xff}, {0xff, 0xff, 0xff}, {0x53, 0x5b, 0x9f},
};

void IMG_Convert8BitImageToRGBA32(byte **out, u32 *poutWidth, u32 *poutHeight, const byte *in, u32 inWidth, u32 inHeight, const byte *pal) {
	int		i, j;
	int		row1[2048], row2[2048], col1[2048], col2[2048];
	const byte	*pix1, *pix2, *pix3, *pix4;
	byte	*tex;

	if(pal == 0) {
		pal = (const byte*)&g_quake1DefaultPalette[0];
	}

	int outWidth;
	// convert texture to power of 2
	for (outWidth = 1; outWidth < inWidth; outWidth <<= 1)
		;

	if (outWidth > 256)
		outWidth = 256;

	int outHeight;
	for (outHeight = 1; outHeight < inHeight; outHeight <<= 1)
		;

	if (outHeight > 256)
		outHeight = 256;

	tex = (byte *)malloc( outWidth * outHeight * 4);
	if(tex == 0) {
		*out = 0;
		*poutWidth = 0;
		*poutHeight = 0;
		g_core->RedWarning("IMG_Convert8BitImageToRGBA32: malloc failed for %i bytes\n",outWidth * outHeight * 4);
		return;
	}

	for (i = 0; i < outWidth; i++)
	{
		col1[i] = (i + 0.25) * (inWidth / (float)outWidth);
		col2[i] = (i + 0.75) * (inWidth / (float)outWidth);
	}

	for (i = 0; i < outHeight; i++)
	{
		row1[i] = (int)((i + 0.25) * (inHeight / (float)outHeight)) * inWidth;
		row2[i] = (int)((i + 0.75) * (inHeight / (float)outHeight)) * inWidth;
	}

	// scale down and convert to 32bit RGB
	byte *ptr = tex;
	for (i=0; i < outHeight; i++)
	{
		for (j=0; j < outWidth; j++, ptr += 4)
		{
			pix1 = &pal[in[row1[i] + col1[j]] * 3];
			pix2 = &pal[in[row1[i] + col2[j]] * 3];
			pix3 = &pal[in[row2[i] + col1[j]] * 3];
			pix4 = &pal[in[row2[i] + col2[j]] * 3];

			ptr[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0])>>2;
			ptr[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1])>>2;
			ptr[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2])>>2;
			ptr[3] = 0xFF;
		}
	}

	*out = tex;

	*poutWidth = outWidth;
	*poutHeight = outHeight;
}
