#include "archives/ResourceStructs.h"
#include "archives/Blang.h"
#include "archives/idImage.h"
#include "miniz/miniz.h"
#include "atlan/AtlanLib.h"
#include "atlan/AtlanLogger.h"
#include "io/BinaryWriter.h"
#include "atlan/AtlanDB.h"
#include "atlan/AtlanModConfig.h"
#include <set>

#define DEBUG_ZIP_NO_MEMORY 1
#if DEBUG_ZIP_NO_MEMORY && defined ( _DEBUG )
#define mz_zip_writer_add_mem(...)
#endif

// Need this so the level packager can warn about new global assets
// not being contained in the pk5
// It consists of "real" assetpaths extracted from an entry's IDCL container
std::set<std::string> G_PK5_MANIFEST;

void pk5_zipDecl(mz_zip_archive* zptr, std::string name, ResourceEntryData_t data) {
	
	// PK5 decl paths might use capitalized resource type strings?
	// Need to verify if this is actually true or not
	for (char& c : name) {
		if(c >= 'A' && c <= 'Z')
			c += 32;
	}
	
	G_PK5_MANIFEST.insert(std::string("rs_streamfile/") + name);

	// + 10 to skip "generated/"
	mz_zip_writer_add_mem(zptr, name.c_str() + 10 , data.buffer, data.length, MZ_DEFAULT_COMPRESSION);
}

void pk5_zipStrings(mz_zip_archive* zptr, const std::string& name, ResourceEntryData_t data) {
	
	if (name.rfind(".lang") != -1) {
		atlog("   WARNING: .lang files are ignored. Only .blang files are converted");
		return;
	}
	if (name.rfind(".blang") == -1) {
		atlog("   ERROR: Expected a .blang for strings");
		return;
	}

	std::string outcsv;
	if (!idcl::blang_totxt(data.buffer, data.length, outcsv)) {
		atlog("   ERROR: Failed to convert blang to CSV");
		return;
	}

	std::string zipname = "binaryFile/strings/" + fspath(name).stem().string() + ".txt";

	mz_zip_writer_add_mem(zptr, zipname.c_str(), outcsv.data(), outcsv.size(), MZ_DEFAULT_COMPRESSION);
}

void pk5_zipSound(mz_zip_archive* zptr, const std::string& name, ResourceEntryData_t data) {
	if (name.rfind(".wem") == -1) {
		atlog("   ERROR: This audio file is currently unsupported");
		return;
	}

	std::string zipname = "audio/" + name;
	mz_zip_writer_add_mem(zptr, zipname.c_str(), data.buffer, data.length, MZ_DEFAULT_COMPRESSION);
}

void pk5_zipSingleArchive(mz_zip_archive* zptr, const ResourceArchive& r, const char* ExpectedType) {
	if (r.header.numResources != 1) {
		atlog("   ERROR: Expected 1 resource entry, found %u", r.header.numResources);
		return;
	}
	if (r.entries[0].dataSize == 0) {
		atlog("   WARNING: Archive is empty. Skipping");
		return;
	}

	const char* typestring, *namestring;

	Get_EntryStrings(r, r.entries[0], typestring, namestring);
	if (strcmp(typestring, ExpectedType)) {
		atlog("   ERROR: Unexpected resource type string %s", typestring);
		return;
	}

	charbuffer_t decompbuffer;
	ResourceEntryData_t entrydata = Get_EntryDataPK5(r, r.entries[0], decompbuffer.data, decompbuffer.capacity);

	check_debug(entrydata.returncode == EntryDataCode::OK);

	std::string zipname = typestring;
	zipname.push_back('/');
	zipname += namestring;

	atlog("   %s", zipname.c_str());

	G_PK5_MANIFEST.insert(zipname);
	mz_zip_writer_add_mem(zptr, zipname.c_str(), entrydata.buffer, entrydata.length, MZ_DEFAULT_COMPRESSION);
}

void pk5_zipImage(mz_zip_archive* zptr, const ResourceArchive& r) {

	if (r.header.numResources != 2) {
		if (r.header.numResources == 1 && r.entries[0].dataSize == 0) {
			atlog("   WARNING: File is empty. Skipping");
		}
		else {
			atlog("   ERROR: Expected 2 resource entries, found %u", r.header.numResources);
		}
		return;
	}

	const char* typestring, *embname, *imgname;
	charbuffer_t decomp_img, decomp_sfile;

	Get_EntryStrings(r, r.entries[0], typestring, imgname);
	if (strcmp(typestring, "image")) {
		atlog("   ERROR: Expected 'image' type as first entry");
		return;
	}
	Get_EntryStrings(r, r.entries[1], typestring, embname);
	if (strcmp(typestring, "rs_emb_sfile")) {
		atlog("   ERROR: Expected 'rs_emb_sfile' type as second entry");
		return;
	}

	ResourceEntryData_t data_img = Get_EntryDataPK5(r, r.entries[0], decomp_img.data, decomp_img.length);
	ResourceEntryData_t data_sfile = Get_EntryDataPK5(r, r.entries[1], decomp_sfile.data, decomp_sfile.length);

	check_debug(data_img.returncode == EntryDataCode::OK && data_sfile.returncode == EntryDataCode::OK);

	idImageEncodingResults results;
	bool ok = idImageEncodingContext::ConvertIdStudio(data_img.buffer, data_img.length, data_sfile.buffer, data_sfile.length, results);

	if (!ok) {
		atlog("   ERROR: Failed to convert image file");
		return;
	}

	std::string zipname = std::string("image/") + imgname;
	G_PK5_MANIFEST.insert(zipname);
	mz_zip_writer_add_mem(zptr, zipname.c_str(), results.buffer, results.file_length, MZ_DEFAULT_COMPRESSION);
}

void pk5_zipModel(mz_zip_archive* zptr, const ResourceArchive& r) {
	if (r.header.numResources != 2) {
		if (r.header.numResources == 1 && r.entries[0].dataSize == 0) {
			atlog("   WARNING: File is empty. Skipping");
		}
		else {
			atlog("   ERROR: Expected 2 resource entries, found %u", r.header.numResources);
		}
		return;
	}

	const char* typestring, *embname, *modelname;
	charbuffer_t decomp_model, decomp_sfile;

	Get_EntryStrings(r, r.entries[0], typestring, modelname);
	if (strcmp(typestring, "model")) {
		atlog("   ERROR: Expected 'model' type as first entry");
		return;
	}
	Get_EntryStrings(r, r.entries[1], typestring, embname);
	if (strcmp(typestring, "rs_emb_sfile")) {
		atlog("   ERROR: Expected 'rs_emb_sfile' type as second entry");
		return;
	}

	ResourceEntryData_t data_model = Get_EntryDataPK5(r, r.entries[0], decomp_model.data, decomp_model.length);
	ResourceEntryData_t data_sfile = Get_EntryDataPK5(r, r.entries[1], decomp_sfile.data, decomp_sfile.length);
	check_debug(data_model.returncode == EntryDataCode::OK && data_sfile.returncode == EntryDataCode::OK);

	charbuffer_t output;
	if (!AtlanDBFile::FromStaticModel(data_model.buffer, data_model.length, data_sfile.buffer, data_sfile.length, output)) {
		atlog("   ERROR: Failed to convert model to AtlanDB file");
		return;
	}

	std::string zipname = "model/";
	zipname += modelname;
	G_PK5_MANIFEST.insert(zipname);
	mz_zip_writer_add_mem(zptr, zipname.c_str(), output.data, output.length, MZ_DEFAULT_COMPRESSION);
}

void ZipPk5(mz_zip_archive* zptr, const fspath& pk5path) {

	idPackage package;
	ResourceEntryBuffers_t packagebuffers;
	
	ResourceArchive resarchive;

	if (!package.Read(pk5path.c_str())) {
		atlog("ERROR: Failed to read pk5");
		return;
	}

	#ifdef _DEBUG
	atlog("---PK5: DEBUG LIST ENTRIES---\n%s", package.ListEntries().c_str());
	atlog("---PK5: DEBUG LIST ENTRIES END---\n");
	#endif

	atlog("Scanning %u PK5 entries", package.numentries);

	for (u32 i = 0; i < package.numentries; i++) {
		const idPackageEntry& e = package.entries[i];

		// Some older pk5s seem to have a zero'd out entry
		if (!e.name.size()) {
			atlog("Ignoring empty entry");
			continue;
		}

		atlog("%s", e.name.c_str());

		ResourceEntryData_t entrydata = package.GetEntryData(e, packagebuffers);
		if (entrydata.returncode != EntryDataCode::OK) {
			atlog("   ERROR: Failed to extract file from pk5");
			continue;
		}

		if (e.name._Starts_with("generated/decls/")) {
			pk5_zipDecl(zptr, e.name, entrydata);
			continue;
		}

		if (e.name._Starts_with("strings/")) {
			pk5_zipStrings(zptr, e.name, entrydata);
			continue;
		}

		if (e.name._Starts_with("sound/")) {
			pk5_zipSound(zptr, e.name, entrydata);
			continue;
		}

		// All entries past this point are assumed to be IDCL containers
		if (entrydata.length < 4 || memcmp(entrydata.buffer, "IDCL", 4)) {
			atlog("   ERROR: Unsupported non-IDCL resource type");
			continue;
		}
		if (!idcl::ReadResource_PK5(resarchive, entrydata.buffer, entrydata.length)) {
			atlog("   ERROR: Failed to read entry into an IDCL archive");
			continue;
		}

		if (e.name._Starts_with("generated/compfile/")) {
			pk5_zipSingleArchive(zptr, resarchive, "compfile");
			continue;
		}

		if (e.name._Starts_with("generated/cswf/")) {
			pk5_zipSingleArchive(zptr, resarchive, "cswf");
			continue;
		}

		if (e.name._Starts_with("generated/image/")) {
			pk5_zipImage(zptr, resarchive);
			continue;
		}

		if (e.name._Starts_with("generated/model/")) {
			pk5_zipModel(zptr, resarchive);
			continue;
		}

		atlog("   ERROR: Unsupported resource type");
	}
}

void ZipPK5Files(const fspath& modfolder) {
	std::string pathstring = (modfolder / "AtlanPackage.zip").string();

	mz_zip_archive ziparchive;
	mz_zip_archive* zptr = &ziparchive;

	mz_zip_zero_struct(zptr);
	if (!mz_zip_writer_init_file(zptr, pathstring.c_str(), 0)) {
		atlog("ERROR: Failed to open output zip file");
		return;
	}

	// Zip the config file if it exists
	pathstring = (modfolder / CFG_NAME_V2).string();
	if(std::filesystem::exists(pathstring))
		mz_zip_writer_add_file(zptr, CFG_NAME_V2, pathstring.c_str(), NULL, 0, MZ_DEFAULT_COMPRESSION);

	const fspath pk5path = modfolder / "base.pk5";
	if (std::filesystem::exists(pk5path)) {
		atlog("\nPackaging base.pk5\n-----");
		ZipPk5(zptr, pk5path);
		atlog("PK5 Packaging Complete\n-----");
	}

	mz_zip_writer_finalize_archive(zptr);
	mz_zip_writer_end(zptr);
}

extern void ZipLevel(const fspath& modfolder, const fspath& leveldir);

void idStudioPackagerMain(const char* modfolder) {
	
	atlog("Running idStudio Mod Packager on %s", modfolder);

	const fspath modpath = modfolder;

	ZipPK5Files(modpath);

	const fspath leveldir = modpath / "dvdpc/base";
	if (std::filesystem::is_directory(leveldir)) {
		atlog("\nRunning Level Packager\n-----");
		ZipLevel(modpath, leveldir);
	}
}

