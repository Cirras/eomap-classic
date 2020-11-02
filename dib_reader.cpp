
#include "dib_reader.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>

#define NUM_CONVERT_TABLES 8

static bool convert_table_init = false;

static int convert_table[(2 << (NUM_CONVERT_TABLES + 2)) - 2];

static int* get_convert_table(int bit)
{
	return &convert_table[(2 << bit) - 2];
}

static void decode_bitfield(uint32_t m, int& shift_out, uint32_t& mask_out)
{
	int shift = 0;

	if (m == 0)
	{
		shift_out = 0;
		mask_out = 0;
		return;
	}

#ifdef __GNUC__
	shift = __builtin_ctz(m);
	m >>= shift;
#else
	while ((m & 1) == 0)
	{
		m >>= 1;
		++shift;
	}
#endif

	shift_out = shift;
	mask_out = m;
}

static void generate_scale_table(int* table, int entries)
{
	int i;

	for (i = 0; i < entries; ++i)
		table[i] = i * 255 / (entries - 1);
}

const char* dib_reader::check_format() const
{
	if (width() < 0)
		return "Image width less than zero";

	if (width() > 0x40000000 || height() < -0x40000000 || height() > 0x40000000)
		return "Image dimensions out of bounds";

	if (depth() != 16 && depth() != 24 && depth() != 32)
		return "Unsupported bit depth";

	if (compression() != RGB && compression() != BitFields)
		return "Unsupported compression";

	constexpr int maxmask = (1 << NUM_CONVERT_TABLES) - 1;

	if (rm > maxmask || gm > maxmask || bm > maxmask || am > maxmask)
		return "Bit mask too long";

	return nullptr;
}

void dib_reader::start()
{
	if (compression() == BitFields)
	{
		decode_bitfield(red_mask(),   rs, rm);
		decode_bitfield(green_mask(), gs, gm);
		decode_bitfield(blue_mask(),  bs, bm);
		decode_bitfield(alpha_mask(), as, am);
	}
	else
	{
		as = am = 0;

		switch (depth())
		{
			case 16:
				decode_bitfield(0x00007C00U, rs, rm);
				decode_bitfield(0x000003E0U, gs, gm);
				decode_bitfield(0x0000001FU, bs, bm);
				break;

			case 24:
			case 32:
				decode_bitfield(0x00FF0000U, rs, rm);
				decode_bitfield(0x0000FF00U, gs, gm);
				decode_bitfield(0x000000FFU, bs, bm);
				break;
		}
	}

	for (int i = 0; i < NUM_CONVERT_TABLES; ++i)
	{
		uint32_t mask = ~(0xFFFFFFFFU << (i+1)) & 0xFFFFFFFFU;

		int* table = get_convert_table(i);
		int entries = (1 << (i+1));

		if (!convert_table_init)
			generate_scale_table(table, entries);

		if (rm == mask) rtable = table;
		if (gm == mask) gtable = table;
		if (bm == mask) btable = table;
		if (am == mask) atable = table;
	}

	convert_table_init = true;
}

using read_fn_t = std::uint32_t (*)(const char* data, size_t offset);

static read_fn_t read_fn_for_bpp(int bpp)
{
	constexpr read_fn_t read_fns[4] = {
		nullptr,
		[](const char* data, std::size_t offset) // 16-bit (bpp = 2)
		{
			return std::uint32_t(int_pack_16_le(
				data[offset], data[offset + 1]
			));
		},
		[](const char* data, std::size_t offset) // 24-bit (bpp = 3)
		{
			return int_pack_32_le(
				data[offset], data[offset + 1],
				data[offset + 2], 0
			);
		},
		[](const char* data, std::size_t offset) // 32-bit (bpp = 4)
		{
			return int_pack_32_le(
				data[offset], data[offset + 1],
				data[offset + 2], data[offset + 3]
			);
		},
	};

	return read_fns[bpp];
}

void dib_reader::read_line_argb(char* outbuf, int row)
{
	int line = ((height() < 0) ? row : height() - 1 - row);
	const char* linebuf = data() + stride() * line;
	auto read_fn = read_fn_for_bpp(bpp());

	for (int i = 0; i < width(); i++)
	{
		std::size_t pixel_offset = linebuf - data_ptr;
		std::uint32_t pixel;

		if (pixel_offset < data_size + bpp())
			pixel = read_fn(data_ptr, pixel_offset);
		else
			pixel = 0;

		char r = static_cast<char>(rtable[((pixel >> rs) & rm)]);
		char g = static_cast<char>(gtable[((pixel >> gs) & gm)]);
		char b = static_cast<char>(btable[((pixel >> bs) & bm)]);
		char a = static_cast<char>(((r|g|b) != 0) * 0xFF);

		outbuf[0] = b;
		outbuf[1] = g;
		outbuf[2] = r;
		outbuf[3] = a;

		linebuf += bpp();
		outbuf += 4;
	}
}

void dib_reader::read_line_abgr(char* outbuf, int row)
{
	int line = ((height() < 0) ? row : height() - 1 - row);
	const char* linebuf = data() + stride() * line;
	auto read_fn = read_fn_for_bpp(bpp());

	for (int i = 0; i < width(); i++)
	{
		std::size_t pixel_offset = linebuf - data_ptr;
		std::uint32_t pixel;

		if (pixel_offset < data_size + bpp())
			pixel = read_fn(data_ptr, pixel_offset);
		else
			pixel = 0;

		char r = static_cast<char>(rtable[((pixel >> rs) & rm)]);
		char g = static_cast<char>(gtable[((pixel >> gs) & gm)]);
		char b = static_cast<char>(btable[((pixel >> bs) & bm)]);
		char a = static_cast<char>(((r|g|b) != 0) * 0xFF);

		outbuf[0] = r;
		outbuf[1] = g;
		outbuf[2] = b;
		outbuf[3] = a;

		linebuf += bpp();
		outbuf += 4;
	}
}