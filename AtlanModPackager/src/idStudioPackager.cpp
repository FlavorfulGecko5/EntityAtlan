#include "archives/ResourceStructs.h"
#include "archives/Blang.h"
#include "archives/idImage.h"
#include "miniz/miniz.h"
#include "atlan/AtlanLib.h"
#include "atlan/AtlanLogger.h"
#include "io/BinaryWriter.h"

void pk5_zipDecl(mz_zip_archive* zptr, std::string name, ResourceEntryData_t data) {
	
	// PK5 decl paths might use capitalized resource type strings?
	// Need to verify if this is actually true or not
	for (char& c : name) {
		if(c >= 'A' && c <= 'Z')
			c += 32;
	}
	
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

void pk5_zipImage(mz_zip_archive* zptr, const ResourceArchive& r, ResourceEntryBuffers_t& buffers) {

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
	mz_zip_writer_add_mem(zptr, zipname.c_str(), results.buffer, results.file_length, MZ_DEFAULT_COMPRESSION);
}

// TODO: THIS FUNCTION IS A WIP
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

	//std::string zipname = "rs_emb_sfile/";
	//zipname += embname;

	//mz_zip_writer_add_mem(zptr, zipname.c_str(), data_sfile.buffer, data_sfile.length, MZ_DEFAULT_COMPRESSION);

	std::string zipname = "model/";
	zipname += modelname;

	const u32 OFFSET = 32 + 64 + data_model.length;

	BinaryWriter writer;
	writer.WriteBytes("ATDB", 4);

	// old
	//writer << (u32)1 << (u32)67 << (u32)0 << data_model.length << data_model.length;

	writer << (u32)1 << (u32)67 << (u32)1 << data_model.length << data_model.length;
	writer << (u64)0 << (u64)0 << (u64)0 
		<< (u64)0 << (u64)0 << (u64)0 << (u64)0
		<< OFFSET << (u32)data_sfile.length;


	writer.WriteBytes(data_model.buffer, data_model.length);

	writer.WriteBytes(data_sfile.buffer, data_sfile.length);

	mz_zip_writer_add_mem(zptr, zipname.c_str(), writer.GetBuffer(), writer.GetFilledSize(), MZ_DEFAULT_COMPRESSION);
}

void idStudio_ZipPk5(mz_zip_archive* zptr, const fspath& pk5path) {

	idPackage package;
	ResourceEntryBuffers_t packagebuffers;
	
	ResourceArchive resarchive;
	ResourceEntryBuffers_t archivebuffers;

	if (!package.Read(pk5path.c_str())) {
		atlog("ERROR: Failed to read pk5");
		return;
	}

	#ifdef _DEBUG
	atlog("%s", package.ListEntries().c_str());
	atlog("-------\n");
	#endif

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

		if (e.name._Starts_with("generated/image/")) {

			if (!idcl::ReadResource_PK5(resarchive, entrydata.buffer, entrydata.length)) {
				atlog("   ERROR: Failed to read image archive");
				continue;
			}

			pk5_zipImage(zptr, resarchive, archivebuffers);
			continue;
		}

		if (e.name._Starts_with("generated/model/")) {
			if (!idcl::ReadResource_PK5(resarchive, entrydata.buffer, entrydata.length)) {
				atlog("   ERROR: Failed to read image archive");
				continue;
			}

			pk5_zipModel(zptr, resarchive);
			continue;
		}

		atlog("   ERROR: Unsupported resource type");
	}
}


void idStudioPackagerMain(const char* modfolder) {
	
	atlog("Running idStudio Mod Packager on %s", modfolder);

	const fspath modpath = modfolder;
	std::string outpath = (modpath / "AtlanPackage.zip").string();

	mz_zip_archive ziparchive;
	mz_zip_archive* zptr = &ziparchive;
	
	mz_zip_zero_struct(zptr);
	if (!mz_zip_writer_init_file(zptr, outpath.c_str(), 0)) {
		atlog("ERROR: Failed to open output zip file");
		return;
	}

	
	const fspath pk5path = modpath / "base.pk5";
	if (std::filesystem::exists(pk5path)) {
		atlog("\nPackaging base.pk5\n-----");
		idStudio_ZipPk5(zptr, pk5path);
	}


	mz_zip_writer_finalize_archive(zptr);
	mz_zip_writer_end(zptr);
}