#include "miniz/miniz.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanLib.h"
#include "entityslayer/EntityParser.h"
#include "entityslayer/Oodle.h"
#include "archives/ResourceStructs.h"
#include "archives/idImage.h"
#include "atlan/AtlanDB.h"
#include "io/BinaryWriter.h"
#include "io/BinaryReader.h"
#include <string>
#include <set>
#include <unordered_map>
#include <fstream>

enum LegacyType {
	lt_unknown,
	lt_decl,
	lt_entities,
	lt_swf,
	lt_image,

	lt_audio_wem,
	lt_audio_opus,

	lt_model_resource,
	lt_model_streamdb,

	lt_json_EternalMod,
	lt_json_assetsinfo,
	lt_json_strings,
};

struct imgextension_t {
	std::string metadata;
	bool isAmbiguous = false;
};

typedef std::unordered_map<std::string, imgextension_t> ExtensionMap;

struct LegacyInfo {
	mz_zip_archive* inzip;
	mz_zip_archive* outzip;
	ExtensionMap* extmap;
	LegacyType ltype;
	std::string oldname;
	std::string newname;
	charbuffer_t filedata;
};

LegacyType Legacy_FindType(const std::string& oldname) {

	std::string extension;
	const size_t dotindex = oldname.find('.');
	if(dotindex == -1)
		return lt_unknown;
	extension = oldname.substr(dotindex + 1);

	if(!extension.length())
		return lt_unknown;
	if(extension == "decl" || extension == "md6.decl")
		return lt_decl;
	if(extension == "entities")
		return lt_entities;
	if(extension == "swf")
		return lt_swf;
	if(extension == "wem")
		return lt_audio_wem;
	if(extension == "opus")
		return lt_audio_opus;

	if (extension._Starts_with("tga") || extension._Starts_with("bimage") || extension._Starts_with("png")) {
		if (extension.find(".dds") != -1) // Edge case for some improperly packaged texture mods
			return lt_unknown;

		return lt_image;
	}

	if (extension == "lwo") {
		return oldname._Starts_with("streamdb/") ? lt_model_streamdb : lt_model_resource;
	}

	if (extension == "json") {
		
		if(oldname == "EternalMod.json")
			return lt_json_EternalMod;

		if(oldname.find("/assetsinfo/") != -1)
			return lt_json_assetsinfo;

		if(oldname.find("/strings/") != -1)
			return lt_json_strings;
	}

	return lt_unknown;
}

std::string Legacy_GetNewName(LegacyType lt, const std::string& oldname, const ExtensionMap* extmap) {

	// Remove archive name from the path
	const size_t firstslash = oldname.find('/');
	const std::string chopped = firstslash == -1 ? oldname : oldname.substr(firstslash + 1);

	switch (lt) {
		case lt_unknown:
		case lt_audio_opus:
		case lt_json_assetsinfo:
		return "CONVERSION_FAILED/" + chopped;

		case lt_entities: return "compfile/" + chopped;
		case lt_swf:      return "cswf/" + chopped;
		case lt_audio_wem: return "audio/" + chopped;
		case lt_json_EternalMod: return "atlanmod.txt";

		// These are special-cased into a different conversion pipeline
		case lt_model_resource: return "model/" + chopped;
		case lt_model_streamdb: 
		{
			// Legacy model converter names streamdb files with the format
			// "streamdb/<original_file_path>_id#<defaultHash>.lwo"
			// We want to chop it down to match the new model file name
			const size_t hashtag = chopped.find('#');
			return "model/" + chopped.substr(0, hashtag - 3) + ".lwo";
		}
		
		case lt_decl:
		if (chopped._Starts_with("generated/decls/")) {
			return chopped.substr(10); // Remove "generated/"
		}
		else {
			atlog("   ERROR: Improper decl path");
			return "CONVERSION_FAILED/" + chopped;
		}

		case lt_json_strings:
		{
			const size_t lastslash = oldname.find_last_of('/');
			const size_t dot = oldname.find_last_of('.');

			return "binaryFile/strings/" + oldname.substr(lastslash + 1, dot - lastslash - 1) + ".txt";
		}

		case lt_image:
		{
			const auto& iter = extmap->find(chopped);
			if (iter == extmap->end()) {
				atlog("   WARNING: Assuming name has accurate extension metadata");
				return "image/" + chopped;
			}
			else if(iter->second.isAmbiguous){
				
				atlog("   ERROR: File extension information is ambiguous");
				return "CONVERSION_FAILED/" + chopped;

			}
			else {
				return "image/" + chopped + iter->second.metadata;
			}
		}

		default:
		atlog("   ERROR: Legacy_GetNewName Unimplemented Type");
		return "CONVERSION_FAILED/" + chopped;
	}
}

void Legacy_AddDefaultConfig(LegacyInfo& info) {

	const char* DEFAULT_CFG = 
R"(modinfo {
	game = "DOOM Eternal"
	requiredVersion = 10
	loadPriority = 1
})";

	atlog("Adding default config file");

	mz_zip_writer_add_mem(info.outzip, "atlanmod.txt", DEFAULT_CFG, strlen(DEFAULT_CFG), MZ_DEFAULT_COMPRESSION);
}

bool Legacy_ConvertData_EternalMod(LegacyInfo& info) {
	std::string outconfig = "modinfo = {\n";
	try {
		std::string_view rawdata(info.filedata.data, info.filedata.length);
		EntityParser parser(ParsingMode::JSON, rawdata, false);

		for (const EntNode& e : (*parser.getRoot())[0]) {
			outconfig.append(e.getNameUQ());
			outconfig.append(" = ");

			if (e.getNameUQ() == "requiredVersion") {
				outconfig.append("10");
			}
			else {
				outconfig.append(e.getValue());
			}

			outconfig.push_back('\n');
		}

		outconfig.append("game = \"DOOM Eternal\"\n");
	}
	catch (...) {
		atlog("   ERROR: Failed to parse config json");
		return false;
	}

	outconfig.append("}\n");

	mz_zip_writer_add_mem(info.outzip, info.newname.c_str(), outconfig.data(), outconfig.length(), MZ_DEFAULT_COMPRESSION);
	return true;
}

bool Legacy_ConvertData_Strings(LegacyInfo& info) {
	std::string outcsv;

	try {
		std::string_view rawdata(info.filedata.data, info.filedata.length);
		EntityParser parser(ParsingMode::JSON, rawdata, false);

		for (const EntNode& e : (*parser.getRoot())[0]["\"strings\""]) {
			outcsv.append(e["\"name\""].getValueUQ());
			outcsv.append(", ");
			outcsv.append(e["\"text\""].getValue());
			outcsv.push_back('\n');
		}
	}
	catch (...) {
		atlog("   ERROR: Failed to parse string json");
		return false;
	}
	mz_zip_writer_add_mem(info.outzip, info.newname.c_str(), outcsv.data(), outcsv.length(), MZ_DEFAULT_COMPRESSION);
	return true;
}

bool Legacy_ConvertData_Image(LegacyInfo& info) {

	idImageEncodingResults results;
	bool result = idImageEncodingContext::ConvertLegacy(info.filedata.data, info.filedata.length, results);
	if (!result) {
		return false;
	}

	mz_zip_writer_add_mem(info.outzip, info.newname.c_str(), results.buffer, results.file_length, MZ_DEFAULT_COMPRESSION);
	return true;
}

// WARNING: Some data in the LegacyInfo structure will be stale in this function call
bool Legacy_ConvertData_Model(LegacyInfo& info, const charbuffer_t& resdata, const charbuffer_t& dbdata) {

	struct dbentry_t {
		u32 offset;
		u32 size;
	};

	std::vector<dbentry_t> dbentries;

	/* Parse the streamdb file */
	{
		u32 num = 0;
		BinaryReader reader(dbdata.data, dbdata.length);
		reader.GoRight(8); // STREAMDB magic
		reader >> num;
		
		check(num > 0 && num < 32);
		dbentries.resize(num);
		for (u32 i = 0; i < num; i++) {
			reader >> dbentries[i].offset;
			check(reader.ReadLE(dbentries[i].size));
		}

		/* Every entry should refer to the same data block*/
		const u32 offset = dbentries[0].offset;
		for (u32 i = 0; i < num; i++) {
			check(dbentries[i].offset == offset);
			check(dbentries[i].size == reader.GetRemaining());
		}

		atlog("   Validated StreamDB");
	}

	/* Write the Atlan DB File */

	BinaryWriter writer;
	writer.EnsureMaxCapacity(resdata.length + dbdata.length + 4096);

	writer.WriteBytes("ATDB", 4);
	writer << (u32)1 << (u32)67 << (u32)dbentries.size()
		<< resdata.length << resdata.length; // Won't bother to Oodle-compress the data

	const size_t DB_ENTRY_OFFSET = writer.GetPosition() 
		+ dbentries.size() * sizeof(AtlanDBFile::entry)
		+ resdata.length;

	for (size_t i = 0; i < dbentries.size(); i++) {
		writer << (u64)0 << (u64)0 << (u64)(i)
			<< (u64)0 << (u64)0 << (u64)0 << (u64)(0)
			<< (u32)DB_ENTRY_OFFSET << (u32)dbentries[0].size;
	}

	writer.WriteBytes(resdata.data, resdata.length);
	writer.WriteBytes(dbdata.data + dbentries[0].offset, dbentries[0].size);

	AtlanDBFile validation;
	check(validation.ReadFrom(writer.GetBuffer(), writer.GetFilledSize()));

	mz_zip_writer_add_mem(info.outzip, info.newname.c_str(), writer.GetBuffer(), writer.GetFilledSize(), MZ_DEFAULT_COMPRESSION);
	return true;
}

void Legacy_ConvertData(LegacyInfo& info) {

	bool result;

	switch (info.ltype) {
		case lt_unknown:
		case lt_decl:
		case lt_entities:
		case lt_swf:
		case lt_audio_wem:
		case lt_audio_opus:
		case lt_json_assetsinfo:
		mz_zip_writer_add_mem(info.outzip, info.newname.c_str(), info.filedata.data, info.filedata.length, MZ_DEFAULT_COMPRESSION);

		if(info.ltype == lt_audio_opus || info.ltype == lt_json_assetsinfo)
			atlog("   ERROR: Manual conversion required for this file. See modding wiki for more information");
		else if(info.ltype == lt_unknown)
			atlog("   ERROR: Could not determine file type");
		return;


		case lt_json_EternalMod:
		result = Legacy_ConvertData_EternalMod(info);
		break;

		case lt_json_strings:
		result = Legacy_ConvertData_Strings(info);
		break;

		case lt_image:
		result = Legacy_ConvertData_Image(info);
		break;

		default:
		atlog("   ERROR: Legacy_ConvertData Unimplemented Type");
		break;
	}

	if (!result) {
		mz_zip_writer_add_mem(info.outzip, ("CONVERSION_FAILED/" + info.oldname).c_str(), info.filedata.data, info.filedata.length, MZ_DEFAULT_COMPRESSION);
	}

}

bool Legacy_LoadFile(mz_zip_archive* zptr, u32 index, charbuffer_t& namebuffer, charbuffer_t& databuffer) {
	mz_zip_archive_file_stat fstats;

	u32 namelength = mz_zip_reader_get_filename(zptr, index, namebuffer.data, (u32)namebuffer.capacity);
	while (namelength >= namebuffer.capacity) {
		namebuffer.EnsureCapacity(namelength);
		namelength = mz_zip_reader_get_filename(zptr, index, namebuffer.data, (u32)namebuffer.capacity);
	}
	namelength--; // Length includes null char
	namebuffer.length = namelength;

	mz_zip_reader_file_stat(zptr, index, &fstats);
	databuffer.EnsureCapacity(fstats.m_uncomp_size + 2048);
	databuffer.length = fstats.m_uncomp_size;

	if (!mz_zip_reader_extract_to_mem_no_alloc(zptr, index, databuffer.data, databuffer.capacity, 0, NULL, 0)) {
		atlog("ERROR: Failed to extract file data for '%s'", namebuffer.data);
		return false;
	}
	return true;
}

void Legacy_MainLoop(LegacyInfo info) {
	std::set<std::string> DuplicateSet;

	struct modelinfo_t {
		std::string zipname;
		std::string newname;
		size_t filesize;
		u32 zipindex;
	};

	bool foundConfig = false;
	std::vector<modelinfo_t> listmodels;
	std::vector<modelinfo_t> listdbs;

	const u32 filecount = mz_zip_reader_get_num_files(info.inzip);

	charbuffer_t namebuffer;
	namebuffer.EnsureCapacity(1024);

	for (u32 i = 0; i < filecount; i++) {
		if (mz_zip_reader_is_file_a_directory(info.inzip, i))
			continue;

		if (!Legacy_LoadFile(info.inzip, i, namebuffer, info.filedata))
			continue;

		info.oldname = namebuffer.data;
		info.ltype = Legacy_FindType(info.oldname);

		atlog("%s", info.oldname.c_str());
		info.newname = Legacy_GetNewName(info.ltype, info.oldname, info.extmap);
		atlog("   %s", info.newname.c_str());


		// Special Case: We must merge the streamdb and the model files
		if (info.ltype == lt_model_streamdb) {
			atlog("   Caching Model StreamDB for merging");
			listdbs.push_back({ info.oldname, info.newname, info.filedata.length, i});
			continue;
		}

		if (info.ltype == lt_json_EternalMod) {
			foundConfig = true;
		}

		// We only want to insert the model resource into the set,
		// NOT the model streamdb.
		if (!DuplicateSet.insert(info.newname).second) {
			atlog("   WARNING: Duplicate Entry Skipped");
			continue;
		}

		if (info.ltype == lt_model_resource) {
			atlog("   Caching Model Resource for merging");
			listmodels.push_back({ info.oldname, info.newname, info.filedata.length, i});
			continue;
		}

		Legacy_ConvertData(info);
	}

	if (!foundConfig) {
		Legacy_AddDefaultConfig(info);
	}


	charbuffer_t resdata;
	charbuffer_t streamdata;

	// Second Pass over model resource / streamdb files
	for (const modelinfo_t& res : listmodels) {
		
		atlog("Static Model Merge: %s", res.zipname.c_str());
		
		resdata.EnsureCapacity(res.filesize);
		resdata.length = res.filesize;
		bool result = mz_zip_reader_extract_to_mem_no_alloc(info.inzip, res.zipindex, resdata.data, resdata.capacity, 0, NULL, 0);

		if (!result) {
			atlog("   ERROR: Failed to extract resource data");
			continue;
		}

		bool foundDB = false;
		for (const modelinfo_t& db : listdbs) {
			if(db.newname != res.newname)
				continue;

			streamdata.EnsureCapacity(db.filesize);
			streamdata.length = db.filesize;
			result = mz_zip_reader_extract_to_mem_no_alloc(info.inzip, db.zipindex, streamdata.data, streamdata.capacity, 0, NULL, 0);
			if (!result) {
				atlog("   ERROR: Failed to extract streamdb data");
				continue;
			}

			foundDB = true;
			break;
		}

		if (!foundDB) {
			atlog("   ERROR: Failed to find matching streamdb file");
			continue;
		}

		info.oldname = res.zipname;
		info.newname = res.newname;
		result = Legacy_ConvertData_Model(info, resdata, streamdata);

		if(!result)
			atlog("   ERROR: Failed to convert static model file");
	}
}

void RunLegacyConverter(const char* zip_path, const char* out_zip_path, ExtensionMap* extmap) {
	atlog("\nRunning Atlan Legacy Converter on '%s'\nSaving to %s\n-----", zip_path, out_zip_path);

	mz_zip_archive inarchive;
	mz_zip_archive* inzip = &inarchive;
	mz_zip_zero_struct(inzip);
	if (!mz_zip_reader_init_file(inzip, zip_path, 0)) {
		atlog("ERROR: Failed to open input zip file");
		return;
	}

	mz_zip_archive outarchive;
	mz_zip_archive* outzip = &outarchive;
	mz_zip_zero_struct(outzip);
	if (!mz_zip_writer_init_heap(outzip, 0, 0)) {
		atlog("ERROR: Failed to open output zip file");
		mz_zip_reader_end(inzip);
		return;
	}

	Legacy_MainLoop({inzip, outzip, extmap});

	void* heap_buffer = nullptr;
	size_t heap_size = 0;

	mz_zip_reader_end(inzip);
	mz_zip_writer_finalize_heap_archive(outzip, &heap_buffer, &heap_size);
	mz_zip_writer_end(outzip);

	std::ofstream outwriter(out_zip_path, std::ios_base::binary);
	outwriter.write((char*)heap_buffer, heap_size);
	outwriter.close();

	delete[] heap_buffer;
}

/*
* Problem:
* Eternal Mod Loader doesn't require images to have their complete extension metadata
* If it's missing, the mod loader will identify the correct resource to replace by
* chopping the metadata from the resource names before comparing them
* 
* Therefore, we need to replicate this behavior using an extension map
* Some ambiguities exist: multiple images can use the same base path, but have different metadata
* However, most of these ambiguities will be with files that modders won't edit
*/
bool Legacy_BuildImageMap(ExtensionMap& map, const char* gamedir) {

	fspath mapspec = fspath(gamedir) / "base/packagemapspec.json";
	if(!std::filesystem::exists(mapspec))
		return false;

	map.reserve(50'000);

	idcl::ArchiveIterator iter(mapspec.string().c_str(), true);
	iter.typefilter = "image";

	std::string name, meta;
	int nummismatches = 0;
	for (const ResourceEntry& e : iter) {

		const char* cptr = iter.namestring;
		while (*cptr) {
			if (*cptr++ == '$') {
				cptr--;
				break;
			}
		}

		name = std::string(iter.namestring, cptr - iter.namestring);

		// Identify ambiguous cases based on whether the metadata is different
		auto emplace = map.try_emplace(name, imgextension_t{cptr, false});
		if (!emplace.second && emplace.first->second.metadata != cptr) {
			emplace.first->second.isAmbiguous = true;
			nummismatches++;
		}
	}

	return map.size();
}

void LegacyConverterMain(const char** input_zips, size_t numzips, const char* gamedir) {

	// Todo: It's a bit wasteful to always initialize this
	// even when we aren't parsing image files
	ExtensionMap imgmap;

	atlog("Building Image Name Map");
	if (!Legacy_BuildImageMap(imgmap, gamedir)) {
		atlog("FATAL ERROR: Failed to build image name map");
		return;
	}

	for (size_t i = 0; i < numzips; i++) {
		fspath outzip = input_zips[i];
		outzip.replace_extension("");
		outzip += "_AtlanPackage.zip";
		
		RunLegacyConverter(input_zips[i], outzip.string().c_str(), &imgmap);
	}
}