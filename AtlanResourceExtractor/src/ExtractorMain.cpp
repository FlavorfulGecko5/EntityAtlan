#include "entityslayer/EntityParser.h"
#include "archives/ResourceStructs.h"
#include "archives/PackageMapSpec.h"
#include "archives/SoundArchive.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanOodle.h"
#include "DeserialMain.h"
#include <iostream>
#include <fstream>
#include <set>
#include <thread>
#include <mutex>
#include <cassert>

typedef std::set<std::string> restypeset_t;
typedef std::set<std::string> audiotypeset_t;

#define THREADMAX 8

struct configdata_t {
	fspath inputdir = "";
	fspath outputdir = "";
	bool run_extractor = true;
	bool run_deserializer = true;
	bool run_audio_extractor = false;

	restypeset_t restypes;

	deserialconfig_t dsconfig;

	audiotypeset_t audiotypes;
	int max_audio_threads = THREADMAX;
	//bool remove_compressed_audio = true;
};

enum SoundArchiveType {
	et_sfx,
	et_music,
	et_voice,
	et_cine
};

struct audiothreadargs {
	const aksnd* snd;
	const AudioSampleMap* samplemap;
	fspath archivepath;
	fspath archiveoutdir;
	uint32_t firstindex;
	uint32_t maxindex;
	SoundArchiveType archiveType;
	int threadid;
	std::atomic<int>* totalSamples;
	std::unordered_map<uint32_t, bool>* extractedSamples;
	sndContainerMask::entry bitmask;
};

std::mutex AUDIO_MAP_MUTEX;

void AudioThread(audiothreadargs args) {

	//aksnd snd;
	//snd.ReadFrom(args.archivepath.string().c_str());

	const fspath audiotempfile = args.archiveoutdir.parent_path() / (std::string("audiotempfile_") + std::to_string(args.threadid) + ".wav");

	std::ifstream archivereader(args.archivepath, std::ios_base::binary);
	assert(archivereader.good());

	std::ofstream compressedWriter;

	size_t bufferSize = 4000000;
	char* samplebuffer = new char[bufferSize];

	int localSampleCount = 0;
	int progressInterval = (args.maxindex - args.firstindex) / 10 + 1;
	if (progressInterval > 25) {
		progressInterval = 25;
	}

	for (uint32_t iter = args.firstindex; iter < args.maxindex; iter++) {

		const aksnd::entry& e = args.snd->entries[iter];

		{
			bool isloaded = args.bitmask.IsLoaded(iter);

			std::lock_guard<std::mutex> map_lock(AUDIO_MAP_MUTEX);
			const auto& tryiter = args.extractedSamples->try_emplace(e.id, isloaded);

			// We've extracted this sample before
			if (!tryiter.second) {
				
				// Same logic as the resource extractor: re-extract if the original version
				// of this sample is disabled by the container mask
				if(isloaded && !tryiter.first->second) {
					tryiter.first->second = true;

					#ifdef _DEBUG
					printf("Re-extracting %d\n", e.id);
					#endif
				}
				else {
					// TODO: Find some way to improve counter display
					continue;
				}
			}
		}

		std::string samplename = args.snd->GetSampleName(e, args.archiveType == et_music);
		std::string sampleevent = args.samplemap->ResolveEventName(e.id);

		// Some adjustments to simplify the final output path where possible
		switch (args.archiveType)
		{
			case et_music:
			sampleevent = "";
			break;

			case et_voice: case et_cine:
			samplename = sampleevent + "_" + samplename;
			sampleevent = "";
			break;
		}


		args.snd->GetSampleData(e, archivereader, samplebuffer, bufferSize);

		const fspath sampleoutpath_decomp = args.archiveoutdir / sampleevent / samplename;

		// MONITOR: Is this thread-safe?
		create_directory(sampleoutpath_decomp.parent_path());

		compressedWriter.open(audiotempfile, std::ios_base::binary);
		assert(compressedWriter.good());
		compressedWriter.write(samplebuffer, e.encodedSize);
		compressedWriter.close();

		#if 1
		std::string syscommand = "vgmstream\\vgmstream-cli.exe -o \"";
		syscommand.append(sampleoutpath_decomp.string());
		syscommand.append("\" \"");
		syscommand.append(audiotempfile.string());
		syscommand.append("\" 1> NUL");
		

		//std::cout << syscommand << "\n";
		int returnresult = std::system(syscommand.c_str());
		assert(returnresult == 0);
		#endif

		if(localSampleCount % progressInterval == 0) {
			args.totalSamples->fetch_add(localSampleCount);
			localSampleCount = 0;

			printf("\rProgress %d / %d", args.totalSamples->load(), args.snd->numentries);
		}
		localSampleCount++;
	}

	args.totalSamples->fetch_add(localSampleCount);
	delete[] samplebuffer;
	if(exists(audiotempfile))
		remove(audiotempfile);

	#ifdef _DEBUG
	printf("\nThread %d Finished\n", args.threadid);
	#endif
}

void AudioExtractor(const configdata_t& config) 
{
	using namespace std::filesystem;
	if(!exists(config.inputdir / "DOOMTheDarkAges.exe")) {
		atlog << "FATAL ERROR: Atlan Audio Extractor only supports DOOM: The Dark Ages\n";
		return;
	}

	if (!exists("vgmstream/vgmstream-cli.exe")) {
		atlog << "FATAL ERROR: Missing vgmstream\n";
		return;
	}

	const fspath snddir = config.inputdir / "base" / "sound" / "soundbanks" / "pc";
	const fspath audiodir = config.outputdir / "audio";
	create_directories(audiodir);

	AudioSampleMap sampleMap;
	sampleMap.Build((config.inputdir / snddir).string());

	// Prioritized Archive List
	std::vector<sndContainerMask::entry> archivesToExtract; 
	for (const sndContainerMask::entry& e : sampleMap.GetMask().masks) {
		const std::string& name = e.archiveName;

		bool foundmatch = false;
		for (const std::string& type : config.audiotypes) {
			if (name.find(type) != std::string::npos) {
				foundmatch = true;
				break;
			}
		}

		if(foundmatch)
			archivesToExtract.push_back(e);

	}
	//for(const sndContainerMask::entry& e : archivesToExtract) {
	//	std::cout << e.archiveName << " " << e.size << "\n";
	//}

	std::unordered_map<uint32_t, bool> ExtractedSamples;
	ExtractedSamples.reserve(40000);

	/*
	* Since we're using the container mask, it's best to extract in reverse order
	* because the highest priority archives are base versions. This should reduce
	* the amount of duplicate extractions significantly versus going from highest
	* priority to lowest, like we do with the resource archives
	*/
	for(int archiveIndex = (int)archivesToExtract.size() - 1; archiveIndex >= 0; archiveIndex--) {
		const sndContainerMask::entry archiveMask = archivesToExtract[archiveIndex];
		SoundArchiveType archiveType;

		if(archiveMask.archiveName.find("SFX") != std::string::npos)
			archiveType = et_sfx;
		else if(archiveMask.archiveName.find("MUSIC") != std::string::npos)
			archiveType = et_music;
		else if(archiveMask.archiveName.find("CINEMAT") != std::string::npos)
			archiveType = et_cine;
		else archiveType = et_voice;

		const fspath archivepath = snddir / archiveMask.archiveName;

		const fspath archiveoutdir = audiodir / archivepath.stem().string().substr(0,  archivepath.stem().string().find_first_of('_') );
		//const fspath archiveoutdir = audiodir / archivepath.stem(); // If we want to extract to separate folders

		create_directory(archiveoutdir);
		atlog << "Extracting from " << archivepath.filename() << "\n";


		aksnd snd;
		snd.ReadFrom(archivepath.string().c_str());

		// In case the container masks wind up being out-of-order
		if(snd.numentries > archiveMask.size) {
			atlog << "FATAL: Entry count larger than container mask\n";
			return;
		}

		std::atomic<int> totalSamples = 0;
		std::thread threadpool[THREADMAX];
		
		int threadsToUse;
		if (snd.numentries < 500) {
			threadsToUse = 4;
		} else threadsToUse = 8;

		if(threadsToUse > config.max_audio_threads)
			threadsToUse = config.max_audio_threads;

		assert(threadsToUse > 0 && threadsToUse <= THREADMAX);

		atlog << "Launching " << threadsToUse << " thread(s).\n";

		uint32_t nextIndex = 0;
		uint32_t lastIndex = 0;
		for(int t = 0; t < threadsToUse; t++) {
			
			audiothreadargs args;
			args.snd = &snd;
			args.samplemap = &sampleMap;
			args.archivepath = archivepath;
			args.archiveoutdir = archiveoutdir;
			args.archiveType = archiveType;
			args.threadid = t;
			args.totalSamples = &totalSamples;
			args.extractedSamples = &ExtractedSamples;
			args.bitmask = archiveMask;

			args.firstindex = nextIndex;
			args.maxindex = nextIndex + snd.numentries / threadsToUse + snd.numentries % threadsToUse;
			nextIndex = args.maxindex;


			// If we exceed the total early, restrict the number of threads
			if (args.maxindex >= snd.numentries) {
				args.maxindex = snd.numentries;
				threadsToUse = t + 1;
			}

			#ifdef _DEBUG
			printf("Thread %d [%d, %d)\n", t, args.firstindex, args.maxindex);
			#endif

			threadpool[t] = std::thread(AudioThread, args);
		}

		for(int t = 0; t < threadsToUse; t++) {
			threadpool[t].join();
		}
		
		atlog << "\rExtracted " << totalSamples.load() << " files from archive\n";
		if(snd.numentries - totalSamples.load() > 0)
			atlog << (snd.numentries - totalSamples.load()) << " duplicates skipped\n";
	}
	
	atlog << "Audio Extractor complete\n";

	std::ofstream dupelogwriter(audiodir / "duplicate_log.txt", std::ios_base::binary);
	dupelogwriter << sampleMap.GetDuplicateLog();
	dupelogwriter.close();
}

void FixLegacyDeclPath(const fspath& outputdir) {
	const fspath legacydir = outputdir / "rs_streamfile" / "generated" / "decls";
	const fspath newdir = outputdir / "decls";

	using namespace std::filesystem;

	if(!exists(legacydir))
		return;

	atlog << "NOTICE: Detected legacy decl output dir at <output>/rs_streamfile/generated/decls\n"
	"Attempting to rename folder to <output>/decls\n";

	if (exists(newdir)) {
		atlog << "ERROR: Failed to rename legacy decl dir. A directory already exists at the new path\n";
		return;
	}

	std::filesystem::rename(legacydir, newdir);

	atlog << "Successfully renamed legacy decl dir\n";
}

/*
* CONSOLIDATED RESOURCE EXTRACTOR FUNCTION
*/
void ExtractorMain() {
	/*
	* REMEMBER TO UPDATE VERSION NUMBER
	*/
	atlog << "Atlan Consolidated Resource Extractor v3.0.1 by FlavorfulGecko5\n";

	/*
	* Parse and validate config file
	*/
	configdata_t config;
	try
	{
		#ifdef _DEBUG
		#define configpath "extractor_config_debug.txt"
		#else
		#define configpath "extractor_config.txt"
		#endif

		EntityParser parser(configpath, ParsingMode::PERMISSIVE);

		EntNode& root = *parser.getRoot();
		EntNode& core = root["core"];

		config.inputdir = core["input_folder"].getValueUQ();
		config.outputdir = core["output_folder"].getValueUQ();

		if (!std::filesystem::is_directory(config.inputdir)) {
			atlog << "FATAL ERROR: " << config.inputdir << " is not a valid directory\n"
				<< "Did you remember to set your input/output folders in " << configpath << "?";
			return;
		}
		if (!std::filesystem::is_directory(config.outputdir)) {
			atlog << "FATAL ERROR: " << config.outputdir << " is not a valid directory"
				<< "Did you remember to set your input/output folders in " << configpath << "?";
			return;
		}

		config.inputdir = std::filesystem::absolute(config.inputdir);
		config.outputdir = std::filesystem::absolute(config.outputdir);

		if (config.outputdir.string().size() >= 16) {
			atlog << "FATAL ERROR: Output directory must be less than 16 characters.\n"
				<< "This is to prevent export errors due to long filepaths.\n"
				<< "Your output directory " << config.outputdir << " is " << config.outputdir.string().size() << " characters";
			return;
		}

		if (!core["run_extractor"].ValueBool(config.run_extractor)) {
			atlog << "WARNING: Failed to read config bool core/run_extractor: assuming default\n";
		}
		if (!core["run_deserializer"].ValueBool(config.run_deserializer)) {
			atlog << "WARNING: Failed to read config bool core/run_deserializer: assuming default\n";
		}
		if (!core["run_audio_extractor"].ValueBool(config.run_audio_extractor)) {
			atlog << "WARNING: Failed to read config bool core/run_audio_extractor: assuming default\n";
		}


		EntNode& restypes = root["extractor"]["resource_types"];
		for (int i = 0; i < restypes.getChildCount(); i++) {
			EntNode& rt = *restypes.ChildAt(i);

			if(rt.IsComment())
				continue;

			config.restypes.insert(std::string(rt.getNameUQ()));
		}
		atlog << "Found " << config.restypes.size() << " resource types\n";

		EntNode& audiotypes = root["audio_extractor"]["audio_types"];
		for (int i = 0; i < audiotypes.getChildCount(); i++) {
			EntNode& at = *audiotypes.ChildAt(i);
			if(at.IsComment())
				continue;

			config.audiotypes.insert(std::string(at.getNameUQ()));
		}
		atlog << "Found " << config.audiotypes.size() << " audio types\n";

		if (!root["audio_extractor"]["max_threads"].ValueInt(config.max_audio_threads, 1, THREADMAX)) {
			atlog << "WARNING: Failed to read config bool audio_extractor/max_threads: assuming default\n";
		}

		/* Deserialization Settings */
		EntNode& deserial = root["deserializer"];
		if(!deserial["deserialize_entity_defs"].ValueBool(config.dsconfig.deserial_entitydefs)) {
			atlog << "WARNING: Failed to read config bool deserializer/deserialize_entity_defs: assuming default\n";
		}
		if(!deserial["deserialize_logic_decls"].ValueBool(config.dsconfig.deserial_logicdecls)) {
			atlog << "WARNING: Failed to read config bool deserializer/deserialize_logic_decls: assuming default\n";
		}
		if(!deserial["deserialize_level_files"].ValueBool(config.dsconfig.deserial_mapentities)) {
			atlog << "WARNING: Failed to read config bool deserializer/deserialize_level_files: assuming default\n";
		}
		if (!deserial["remove_binary_files"].ValueBool(config.dsconfig.remove_binaries)) {
			atlog << "WARNING: Failed to read config bool deserializer/remove_binary_files: assuming default\n";
		}
		if (!deserial["add_indentation"].ValueBool(config.dsconfig.indent)) {
			atlog << "WARNING: Failed to read config bool deserializer/add_indentation: assuming default\n";
		}
		if (!deserial["include_originals"].ValueBool(config.dsconfig.include_original)) {
			atlog << "WARNING: Failed to read config bool deserializer/include_originals: assuming default\n";
		}


	}
	catch(...) {
		atlog << "FATAL ERROR: failed to parse " << configpath << "\n";
		return;
	}

	/*
	* Read and verify PackageMapSpec data
	*/
	config.outputdir /= "atlan";
	std::vector<std::string> packages = PackageMapSpec::GetPrioritizedArchiveList(config.inputdir, false);

	if (packages.empty()) {
		atlog << "FATAL ERROR: Could not find PackageMapSpec.json\n"
			<< "Did you enter the correct path to your Dark Ages folder?";
		return;
	}
	else {
		atlog << "Found DOOM The Dark Ages Folder\n"
			<< "Dumping data to " << config.outputdir << "\n";
	}
	std::filesystem::create_directories(config.outputdir);

	/*
	* Download and load Oodle
	* (Do it here so it doesn't get downloaded to the wrong folder if install folder is input wrong)
	*/
	if(!Oodle::AtlanOodleInit(config.inputdir))
		return;

	if (config.run_extractor) {
		atlog << "Performing resource extraction\n";

		FixLegacyDeclPath(config.outputdir);

		idclMaskFile containerMask;
		containerMask.Read(config.inputdir);

		if (containerMask.maskcount == 0) {
			atlog << "FATAL ERROR: Could not read container.mask\n";
			return;
		}

		const fspath basepath = config.inputdir / "base";
		std::unordered_map<std::string, bool> extractedFileMap;

		size_t compsize = 24000;
		size_t decompsize = 24000;
		char* compbuffer = new char[compsize];
		char* decompbuffer = new char[decompsize];

		// Aliasing system for logic object descriptors
		// Many of their filenames are too long to export verbatim.
		// Plus, all of them use invalid path characters like ':'
		struct {
			std::string aliases;
			int total = 0;
		} descriptorData;

		descriptorData.aliases.reserve(500000);

		for(size_t i = 0; i < packages.size(); i++) {
			fspath respath = basepath / packages[i];
			int filecount = 0;

			atlog << "Extracting from " << respath.filename() << "\n";
			
			ResourceArchive archive;
			Read_ResourceArchive(archive, respath.string(), RF_SkipData);
			std::ifstream archivestream(respath, std::ios_base::binary);

			// A select few resource archives don't have a container mask blob. This is normal
			const idclMaskFile::entry bitmask = containerMask.FindArchiveMask(respath);
			const bool hasBitmask = bitmask.size >= archive.header.numResources;

			for(uint32_t entryindex = 0; entryindex < archive.header.numResources; entryindex++) {
				const ResourceEntry& e = archive.entries[entryindex];

				const char* typestring, *namestring;
				Get_EntryStrings(archive, e, typestring, namestring);

				// Don't extract files with undesired types
				if (config.restypes.count(typestring) == 0)
					continue;

				// Only proceed if a higher-priority archive doesn't have this file
				{
					std::string setstring = typestring;
					setstring.push_back('/');
					setstring.append(namestring);

					const bool isloaded = hasBitmask ? bitmask.IsLoaded(entryindex) : true;

					const auto& tryresult = extractedFileMap.try_emplace(setstring, isloaded);
					if (!tryresult.second) { // Key already exists in map
						
						// Rare Edge Case: The first copy of the file we extracted is disabled by the container mask
						// But another version in a lower-priority archive is enabled instead.
						// We re-extract the enabled version of the file under the assumption that it's more accurate.
						// MONITOR: If all copies of a file are disabled, there's no real way to determine which is the most
						// "up-to-date" version. Best we can do is go in order of archive priority like we already are.
						// TODO: investigate using generatedTimeStamps
						if (isloaded && !tryresult.first->second) {
							tryresult.first->second = true;
							#ifdef _DEBUG
							atlog << "Container Mask: Re-Extracting " << setstring << "\n";
							#endif
						}
						else {
							continue;
						}

					}
					filecount++;
				}

				// Make adjustments to the output name string depending on the resource type
				std::string adjustedNameString;
				if (strcmp(typestring, "rs_streamfile") == 0) {

					adjustedNameString = namestring;

					if (adjustedNameString._Starts_with("generated/decls/")) {
						adjustedNameString = adjustedNameString.substr(16); // Remove "generated/decls/"
						typestring = "decls";
					}
				}
				else if (strcmp(typestring, "mapentities") == 0) {
					adjustedNameString = namestring;
					for (char& c : adjustedNameString) {
						if(c == '/')
							c = '@';
					}
				}
				else if(strcmp( typestring, "logicObjectDescriptor") == 0) {
					adjustedNameString = "logicObjectDescriptor_";
					adjustedNameString.append(std::to_string(descriptorData.total));
					adjustedNameString.append(".bin");

					descriptorData.total++;
					descriptorData.aliases.push_back('"');
					descriptorData.aliases.append(adjustedNameString);
					descriptorData.aliases.append("\" = \"logicObjectDescriptor/");
					descriptorData.aliases.append(namestring);
					descriptorData.aliases.append("\"\n");
				}

				// Setup the output path
				fspath output_path = (config.outputdir / typestring) / (adjustedNameString.empty() ? namestring : adjustedNameString.c_str());
				{
					if (!output_path.has_extension()) {
						output_path.replace_extension(".bin");
					}
					std::filesystem::create_directories(output_path.parent_path());

					if (output_path.string().length() > 250)
						atlog << "WARNING: Filepath " << output_path << " exceeding safe limit. Unexpected behavior may occur\n";
				}


				// Get the entry data
				ResourceEntryData_t entrydata = Get_EntryData(e, archivestream, compbuffer, compsize, decompbuffer, decompsize);
				if (entrydata.returncode != EntryDataCode::OK) {
					if(entrydata.returncode == EntryDataCode::UNKNOWN_COMPRESSION) {
						atlog << "ERROR: Unknown compression format " << e.compMode << " on file " << output_path << "\n";
					}
					else {
						atlog << "ERROR: Failure code " << static_cast<int>(entrydata.returncode) << " on file " << output_path << "\n";
						continue;
					}
				}

				// Write the file
				std::ofstream outputstream(output_path, std::ios_base::binary);
				outputstream.write(entrydata.buffer, entrydata.length);
				outputstream.close();
			}

			atlog << "Extracted " << filecount << " files from archive\n";
		}

		delete[] compbuffer;
		delete[] decompbuffer;

		// Write the LogicObjectDescriptor alias file, if it's populated
		if(descriptorData.total > 0) {
			std::ofstream descriptorwriter(config.outputdir / "logicObjectDescriptor/aliases.txt", std::ios_base::binary);
			descriptorwriter << descriptorData.aliases;
			descriptorwriter.close();
		}

		atlog << "Extraction Complete: " << extractedFileMap.size() << " files extracted in total\n";
	}
	else {
		atlog << "Skipping resource extraction\n";
	}

	if(config.run_deserializer) {
		Deserializer::DeserialMain(config.inputdir, config.outputdir, config.dsconfig);
	}
	else {
		atlog << "Skipping deserialization\n";
	}

	if (config.run_audio_extractor) {
		AudioExtractor(config);
	}
	else {
		atlog << "Skipping Audio Extractor\n";
	}
}

int main(int argc, char* argv[]) {
	#define logpath "extractor_log.txt"

	#ifdef _DEBUG
	AtlanLogger::init(logpath);
	ExtractorMain();
	AtlanLogger::exit();
	#else

	try {
		AtlanLogger::init(logpath);
		ExtractorMain();
	}
	catch (std::exception e) {
		atlog << "\n\nFATAL ERROR: An unexpected crash has occurred\n"
			<< "This may have left your extracted files incomplete or corrupted.\n"
			<< "Error Message: " << e.what();
	}

	atlog << "\n\nThis window will close in 10 seconds\n";
	atlog << "Output written to " << logpath << "\n";
	AtlanLogger::exit();
	
	std::this_thread::sleep_for(std::chrono::seconds(10));
	#endif
}