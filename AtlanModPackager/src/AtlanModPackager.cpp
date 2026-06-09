
#include <filesystem>
#include <thread>
#include <vector>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "archives/ResourceEnums.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanProfiling.h"
#include "ReserialMain.h"
#include "io/BinaryWriter.h"
#include "miniz/miniz.h"
#include "atlan/AtlanOodle.h"
#include "atlan/AtlanModConfig.h"
#include "archives/idImage.h"

typedef std::filesystem::path fspath;

bool FileDialog(fspath& output_filepath, const fspath& in_zipname, bool SaveAs);

struct idImageJob {
	fspath filepath;
	std::string assetpath;
	std::string encodinginfo;
	std::string zippath;
};

struct idImageJobList {
	std::vector<idImageJob> jobs;
	idImageEncodingContext* context = nullptr;
	mz_zip_archive* zptr = nullptr;
};

std::mutex g_getjob_mutex;
std::mutex g_addzip_mutex;

void ImageEncodingThread(idImageJobList* joblist) {
	
	idImageJob CurrentJob;
	idImageEncodingResults ImageOutput;
	std::string OutputLog;

	if(!idImageEncodingContext::COMThreadInit())
		return;

	while (1) {

		// Fetch next image job
		{
			std::lock_guard<std::mutex> lock(g_getjob_mutex);
			if(joblist->jobs.size() == 0)
				break;

			CurrentJob = joblist->jobs.back();
			joblist->jobs.pop_back();
		}

		OutputLog = CurrentJob.zippath;
		OutputLog.append(" (");
		OutputLog.append(CurrentJob.assetpath);
		OutputLog.append(" )\n");
		bool success = joblist->context->EncodeImage(CurrentJob.assetpath, CurrentJob.encodinginfo, CurrentJob.filepath.c_str(), ImageOutput, OutputLog);
		if (!success) {
			atlog << OutputLog;
			continue;
		}

		// Add to image
		// todo finish and test
		{
			std::lock_guard<std::mutex> lock(g_addzip_mutex);

			success = mz_zip_writer_add_mem(joblist->zptr, CurrentJob.zippath.c_str(), ImageOutput.buffer, ImageOutput.file_length, MZ_DEFAULT_COMPRESSION);
			if (!success) {
				OutputLog.append("   ERROR: Failed to add image to zip file\n");
			}
		}

		atlog << OutputLog;
	}

	idImageEncodingContext::COMThreadRelease();
}

void PackagerMain(const char* OVERRIDE_IMAGE_ENCODER_PATH)
{
	using namespace std::filesystem;

	fspath DIR_INPUT = ".";
	fspath ZIP_OUTPUT;
	if(FileDialog(DIR_INPUT, "NOT_USED", false) == false) {
		atlog << "Folder dialog cancelled, or error encountered\n";
		return;
	}
	if (FileDialog(ZIP_OUTPUT, DIR_INPUT.stem(), true) == false) {
		atlog << "Save As dialog cancelled or encountered error\n";
		return;
	}

	atlanstamp START_TIME("Packaging Time");

	// TODO: Really need to synchronize this with the modloader so we're
	// not updating two different maps per tool
	const std::unordered_map<std::string, ResourceType> ValidResourceTypes = {
		{"rs_streamfile", rt_rs_streamfile},
		{"decls",         rt_rs_streamfile},
		{"image",         rt_image},
		{"audio",         rt_audio},
		{"entityDef",     rt_entityDef},
		{"logicClass",    rt_logicClass},
		{"logicEntity",   rt_logicEntity},
		{"logicFX",       rt_logicFX},
		{"logicLibrary",  rt_logicLibrary},
		{"logicUIWidget", rt_logicUIWidget},
		{"mapentities",   rt_mapentities}
	};

	AtlanModConfig ModConfig;

	std::vector<fspath> filepaths;
	const fspath modsfolder = absolute(DIR_INPUT);
	const size_t modsfolder_length = modsfolder.string().size();

	if(!is_directory(modsfolder)) {
		atlog << "FATAL ERROR: Could not find mods folder\n";
		return;
	}
	atlog << "Packaging " << modsfolder << "\n";

	// Put this after the mod folder is located, so
	// we don't download Oodle in an incorrect place.
	if (!Oodle::AtlanOodleInit("."))
		return;

	// Keep init_heap values at zero or the finalized zip file will have some sort of offset error
	mz_zip_archive zipfile;
	mz_zip_archive* zptr = &zipfile;
	mz_zip_zero_struct(zptr);
	mz_zip_writer_init_heap(zptr, 0, 0);

	/* Gather all mod filepaths */
	for (const directory_entry& entry : recursive_directory_iterator(modsfolder))
	{
		if(entry.is_directory())
			continue;

		const fspath entrypath = entry.path();
		const fspath extension = entrypath.extension();
		if(extension == L".zip" || extension == L".ZIP")
			continue;

		if(entrypath.filename() == CFG_NAME) {

			// Edge Case: Mod config is in a subfolder - ignore it
			if(entrypath.parent_path() != modsfolder) {
				atlog << "Ignoring " CFG_NAME << " inside a subfolder\n";
				continue;
			}

			atlog << "Found " CFG_NAME "\n";
			if(!ModConfig.TryRead(entrypath.string()))
				return;

			// Need to add this to the zip now or else the config will be filtered out later
			bool result = mz_zip_writer_add_file(zptr, CFG_NAME, entrypath.string().c_str(), 
				"", 0, MZ_DEFAULT_COMPRESSION);
			if (!result) {
				atlog << "ERROR: Failed to add " CFG_NAME " to zip file\n";
				return;
			}
			continue;
		}

		filepaths.push_back(entrypath);
		//atlog << entry.path() << "\n"; 
	}

	idImageJobList ImageJobs;
	ImageJobs.jobs.reserve(filepaths.size());

	int IgnoredFiles = 0;
	std::string IgnoreLog;

	/* iterate through all the files */
	for(const fspath& modfile : filepaths) 
	{
		std::string zippedName = modfile.string().substr(modsfolder_length + 1);
		std::string typestring, AssetName;
		ModConfig.GetNormalizedName(zippedName, typestring, AssetName);

		// Exclude any files from the zip that aren't valid mod files
		const auto& TYPEITER = ValidResourceTypes.find(typestring);
		if(TYPEITER == ValidResourceTypes.end()) {
			IgnoredFiles++;
			IgnoreLog.append("- IGNORING: ");
			IgnoreLog.append(zippedName);
			IgnoreLog.push_back('\n');
			continue;
		}
		const ResourceType RESTYPE = TYPEITER->second;

		if(RESTYPE == rt_image) {

			// Extract encoding info from end of filename if it exists
			std::string EncodingInfo;
			{
				const char* c = AssetName.data();
				while (*c) {
					if ('~' == *c) {
						size_t subindex = c - AssetName.data();
						EncodingInfo = AssetName.substr(subindex + 1);
						AssetName.erase(AssetName.begin() + subindex, AssetName.end());
						break;
					}
					c++;
				}
			}

			ImageJobs.jobs.emplace_back();
			idImageJob& j = ImageJobs.jobs.back();
			j.assetpath = AssetName;
			j.encodinginfo = EncodingInfo;
			j.filepath = modfile;
			j.zippath = zippedName;
			continue;
		}

		atlog << "Packaging " << zippedName << "\n";

		// TODO: Investigate the serialization codepath. CRT detects massive
		// memory leaks. Most likely the global variables used in the serialization pipeline?
		// Update: Probably the entity class map
		if (RESTYPE & rtc_serialized) {

			atlog << "Serializing " << zippedName << "\n";
			BinaryWriter serialized(static_cast<size_t>(file_size(modfile) * 0.5));
			Reserializer::Serialize(modfile.string().c_str(), serialized, RESTYPE);

			// TODO FIXME: This screws up the aliasing system if the file has an alias
			std::string bin_name = fspath(zippedName).replace_extension(".bin").string();
			bool result;

			// Oodle Compress Mapentities
			if (RESTYPE == rt_mapentities) {
				char* compbuffer = nullptr;
				size_t outputlength = 0, outputBufferLength = 0;

				atlog << "Compressing " << zippedName << "\n";
				if(!Oodle::AtlanCompress(serialized.GetBuffer(), serialized.GetFilledSize(), compbuffer, outputlength, outputBufferLength))
					atlog << "ERROR: Failed to create Atlan Compression File\n";

				assert(Oodle::IsAtlanCompFile(compbuffer, outputlength));
				result = mz_zip_writer_add_mem(zptr, bin_name.c_str(), compbuffer, outputlength, MZ_DEFAULT_COMPRESSION);

				delete[] compbuffer;
			}
			else {
				result = mz_zip_writer_add_mem(zptr, bin_name.c_str(), serialized.GetBuffer(), serialized.GetFilledSize(), MZ_DEFAULT_COMPRESSION);
			}

			if (!result) {
				atlog << "ERROR: Failed to add " << bin_name << " to zip file\n";
			}

			std::string temp = zippedName;
			zippedName = "noload/";
			zippedName += temp;
		}

		bool result = mz_zip_writer_add_file(zptr, zippedName.c_str(), modfile.string().c_str(), "", 0, MZ_DEFAULT_COMPRESSION);
		if(!result)
			atlog << "ERROR: Failed to add " << zippedName << " to zip file\n";		
	}

	if(ImageJobs.jobs.size()) {
		const int NUMTHREADS = 8;
		std::thread threadpool[NUMTHREADS];

		atlog << "Running " << ImageJobs.jobs.size() 
			<< " Image Encoding Jobs on " << NUMTHREADS << " threads\n";

		idImageEncodingContext ImageEncoder;
		{
			std::string gamedir;
			if (OVERRIDE_IMAGE_ENCODER_PATH) {
				gamedir = OVERRIDE_IMAGE_ENCODER_PATH;
			}
			else {
				gamedir = absolute(".").string();
			}

			atlog << "Initializing Image Encoder with directory " << gamedir << "\n";
			if (!ImageEncoder.InitializeContext(gamedir, 6))
				return;
		}

		ImageJobs.context = &ImageEncoder;
		ImageJobs.zptr = zptr;

		for(int i = 0; i < NUMTHREADS; i++)
			threadpool[i] = std::thread(ImageEncodingThread, &ImageJobs);

		for(int i = 0; i < NUMTHREADS; i++)
			threadpool[i].join();
	}

	if(IgnoredFiles) {
		atlog << "----------\n" << IgnoredFiles << " Files were not valid mod files and ignored\n";
		atlog << IgnoreLog << "----------\n";
	}

	void* buffer = nullptr;
	size_t buffersize = 0;
	bool finalize = mz_zip_writer_finalize_heap_archive(zptr, &buffer, &buffersize);
	if (!finalize) {
		atlog << "ERROR: Failed to finalize zip archive\n";
		return;
	}

	mz_zip_writer_end(zptr);

	std::ofstream zipoutput(ZIP_OUTPUT, std::ios_base::binary);
	zipoutput.write((char*)buffer, buffersize);
	zipoutput.close();
	delete[] buffer;

	START_TIME.log();
}

// Unused
#if 0
void AlternateMode(int argc, char* argv[])
{
	atlog << "\n";

	if (argc != 4 || strcmp(argv[1], "--serializemap") != 0) {
		atlog << "Specialized Mode: AtlanModPackager.exe --serializemap <input_path> <output_path>\n";
		return;
	}

	using namespace std::filesystem;

	fspath inputpath = absolute(argv[2]);
	fspath outputpath = absolute(argv[3]);

	if(!exists(inputpath) || is_directory(inputpath)) {
		atlog << "FATAL ERROR: Input file does not exist\n";
		return;
	}

	if (is_directory(outputpath) || !is_directory(outputpath.parent_path())) {
		atlog << "FATAL ERROR: Invalid output location\n";
		return;
	}

	BinaryWriter writer(static_cast<size_t>(file_size(inputpath) * 0.5f));
	atlog << "Serializing Mapentities " << inputpath << "\n";

	int warningcount = Reserializer::Serialize(inputpath.string().c_str(), writer, rt_mapentities);

	atlog << "Total Warnings: " << warningcount << "\n";
	atlog << "Writing output to " << outputpath << "\n";

	std::ofstream outwriter(outputpath, std::ios_base::binary);
	outwriter.write(writer.GetBuffer(), writer.GetFilledSize());
	outwriter.close();
}
#endif

int main(int argc, char* argv[])
{
	#define logpath "packager_log.txt"

	#ifndef _DEBUG
	try {
	#endif

		// The packager also needs COM for the file dialogs
		if(!idImageEncodingContext::COMThreadInit())
			return 0;
		AtlanLogger::init(logpath);
		atlog << "Atlan Mod Packager 2.0 by FlavorfulGecko5\n";
		PackagerMain(argc > 1 ? argv[1] : nullptr);
		

	#ifndef _DEBUG
	}
	catch (std::exception e) {
		atlog << "\n\nFATAL ERROR: An unexpected crash has occurred\n"
			<< "This may have left your packaged zip incomplete or corrupted.\n"
			<< "Error Message: " << e.what();
	}
	#endif

	atlog << "\n\nThis window will close in 10 seconds\n";
	atlog << "Output written to " << logpath << "\n";
	AtlanLogger::exit();
	idImageEncodingContext::COMThreadRelease();

	#ifndef _DEBUG
	std::this_thread::sleep_for(std::chrono::seconds(10));
	#endif
}

#define WIN32_LEAN_AND_MEAN
//#include <Windows.h>
#include <shobjidl.h>

bool FileDialog(fspath& output_filepath, const fspath& in_zipname, bool SaveAs) {
	bool returnval = false;
	IFileDialog* folderdialog;
	HRESULT hr;

	// Create the FileOpenDialog object.
	if (SaveAs) {
		hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL, IID_IFileSaveDialog, (void**)&folderdialog);
	}
	else {
		hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&folderdialog);
	}

	if (SUCCEEDED(hr))
	{
		if (SaveAs) {
			COMDLG_FILTERSPEC spec;
			spec.pszName = L"Zip Files";
			spec.pszSpec = L"*.zip";
			folderdialog->SetFileTypes(1, &spec);

			std::wstring default_zipname = in_zipname;
			default_zipname.append(L"_AtlanPackage.zip");
			folderdialog->SetFileName(default_zipname.c_str());

			folderdialog->SetTitle(L"Save Zip File");
		}
		else {
			DWORD options;
			folderdialog->GetOptions(&options);
			options |= FOS_PICKFOLDERS | FOS_FILEMUSTEXIST;
			folderdialog->SetOptions(options);
			folderdialog->SetTitle(L"Select Mod Folder to Package");

			IShellItem *defaultFolder = NULL;
			hr = SHCreateItemFromParsingName(std::filesystem::absolute(".").c_str(), NULL, IID_PPV_ARGS(&defaultFolder));
			if (SUCCEEDED(hr)) {
				folderdialog->SetDefaultFolder(defaultFolder);
				defaultFolder->Release();
			}
		}

		hr = folderdialog->Show(NULL);

		// Get the file name from the dialog box.
		if (SUCCEEDED(hr))
		{
			IShellItem* selectionItem;
			hr = folderdialog->GetResult(&selectionItem);
			if (SUCCEEDED(hr))
			{
				PWSTR selectionString;
				hr = selectionItem->GetDisplayName(SIGDN_FILESYSPATH, &selectionString);

				// Display the file name to the user.
				if (SUCCEEDED(hr))
				{
					output_filepath = selectionString;
					returnval = true;
					CoTaskMemFree(selectionString);
				}
				selectionItem->Release();
			}
		}
		folderdialog->Release();
	}
	return returnval;
}