#include "SlugFont.h"
#include "atlan/AtlanOodle.h"

bool idcl::make_slugfont(const char* slugdata, const size_t sluglength, charbuffer_t& outbuffer)
{
	if (sluglength < 4 || *(int*)slugdata != 'slug')
		return false;

	struct slugheader_t {
		u32 flags = 0;
		u32 unk0 = 0x20;
		u32 unk1 = 0x2000;
		u32 unk2 = 0x2020;
		u64 slug_size;
		u32 meta_offset = 0;
		u32 meta_length = 0;
	} header;
	header.slug_size = sluglength;

	const size_t GLYPHMASK_SIZE = 0x10000 / 8;
	const size_t FINAL_SIZE = sizeof(header) + GLYPHMASK_SIZE + sluglength;

	charbuffer_t uncompressed;
	uncompressed.EnsureCapacity(FINAL_SIZE);

	memcpy(uncompressed.data, &header, sizeof(header));
	memset(uncompressed.data + sizeof(header), 0, GLYPHMASK_SIZE);
	memcpy(uncompressed.data + sizeof(header) + GLYPHMASK_SIZE, slugdata, sluglength);
	return Oodle::AtlanCompress(uncompressed.data, FINAL_SIZE, outbuffer.data, outbuffer.length, outbuffer.capacity);
}

bool idcl::make_slugfont(const wchar_t* filepath, charbuffer_t& output)
{
	charbuffer_t rawslug;
	if (!FileReader::ReadFile(filepath, rawslug))
		return false;
	return make_slugfont(rawslug.data, rawslug.length, output);
}
