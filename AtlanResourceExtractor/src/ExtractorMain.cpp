#include "entityslayer/EntityParser.h"
#include "entityslayer/Oodle.h"
#include "archives/ResourceStructs.h"
#include "archives/PackageMapSpec.h"
#include "archives/SoundArchive.h"
#include "archives/Blang.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanOodle.h"
#include "DeserialMain.h"
#include "io/BinaryReader.h"
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
	bool run_soundbank_extractor = false;
	bool run_extra_scripts = true;

	restypeset_t restypes;

	deserialconfig_t dsconfig;

	audiotypeset_t audiotypes;
	int max_audio_threads = THREADMAX;
	bool decode_samples = true;
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
	uint32_t* ptr_nextindex;
	uint32_t endindex;
	SoundArchiveType archiveType;
	int threadid;
	std::atomic<int>* totalSamples;
	std::unordered_map<uint32_t, bool>* extractedSamples;
	sndContainerMask::entry bitmask;
	bool decode_samples;
};

std::mutex AUDIO_MAP_MUTEX;

void AudioThread(audiothreadargs args) {
	using namespace std::filesystem;
	const gamebit_t GAME = args.snd->game;

	fspath audiotempfile = args.archiveoutdir.parent_path() / (std::string("audiotempfile_") + std::to_string(args.threadid) + ".wav");

	std::ifstream archivereader(args.archivepath, std::ios_base::binary);
	assert(archivereader.good());

	std::ofstream compressedWriter;

	size_t bufferSize = 4000000;
	char* samplebuffer = new char[bufferSize];

	u32 localExtractionCount = 0;
	u32 print_lastiter = 0;
	u32 print_numcheck = args.snd->numentries / 5 + 1;
	if(print_numcheck > 25)
		print_numcheck = 25;

	while(1) {
		aksnd::entry e;
		u32 iter;
		{
			std::lock_guard<std::mutex> map_lock(AUDIO_MAP_MUTEX);

			iter = *args.ptr_nextindex;
			if(iter >= args.endindex)
				break;
			*args.ptr_nextindex += 1;
			e = args.snd->entries[iter];

			// Some samples are empty?
			if (e.encodedSize == 0) {
				#ifdef _DEBUG
				printf("\nSkipping empty sample\n");
				#endif
				continue;
			}

			bool isloaded = args.bitmask.IsLoaded(iter);

			const auto& tryiter = args.extractedSamples->try_emplace(e.id, isloaded);

			// We've extracted this sample before
			if (!tryiter.second) {
				
				// Same logic as the resource extractor: re-extract if the original version
				// of this sample is disabled by the container mask
				if(isloaded && !tryiter.first->second) {
					tryiter.first->second = true;

					#ifdef _DEBUG
					printf("\nRe-extracting %u\n", e.id);
					#endif
				}
				else {
					// TODO: Find some way to improve counter display
					continue;
				}
			}
		}

		fspath sampleoutpath_decomp;
		if(GAME == game_darkages) {

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

			sampleoutpath_decomp = args.archiveoutdir / sampleevent / samplename;
			// MONITOR: Is this thread-safe?
			create_directory(sampleoutpath_decomp.parent_path());
		}
		else {
			std::string samplepath = args.samplemap->ResolveEventName(e.id);

			// Insert the sample ID before the extension
			samplepath.erase(samplepath.end() - 4, samplepath.end());
			samplepath.push_back('_');
			samplepath.append(std::to_string(e.id));
			samplepath.append(".wav");

			sampleoutpath_decomp = args.archiveoutdir / samplepath;

			// Eternal's official sample names can have nested directories
			std::filesystem::create_directories(sampleoutpath_decomp.parent_path());

			// Vgmstream will fail to open the file if the extension doesn't match
			// up with it's encoding...
			if (e.metaunion.de.encoding == 2) {
				audiotempfile.replace_extension(".opus");
			}
			else audiotempfile.replace_extension(".wav");
		}

		if(sampleoutpath_decomp.wstring().length() > 250) {
			atlog("\nWARNING: Long audio sample path %ls", sampleoutpath_decomp.c_str());
		}

		args.snd->GetSampleData(e, archivereader, samplebuffer, bufferSize);
		compressedWriter.open(args.decode_samples ? audiotempfile : sampleoutpath_decomp, std::ios_base::binary);

		// Rare issue caused by file handles still being active after vgmstream finishes executing
		while (!compressedWriter.good()) {
			atlog("\nRARE ERROR: Temporary file open failed. Retrying", sampleoutpath_decomp.c_str());
			compressedWriter.open(args.decode_samples ? audiotempfile : sampleoutpath_decomp, std::ios_base::binary);
		}
		compressedWriter.write(samplebuffer, e.encodedSize);
		compressedWriter.close();

		if (args.decode_samples) {
			std::string syscommand = "vgmstream\\vgmstream-cli.exe -o \""; 
			syscommand.append(sampleoutpath_decomp.string());
			syscommand.append("\" \"");
			syscommand.append(audiotempfile.string());
			syscommand.append("\" 1> NUL");


			//std::cout << syscommand << "\n";
			int returnresult = std::system(syscommand.c_str());
			assert(returnresult == 0);
		}

		if(args.threadid == 0) {
			if(iter - print_lastiter >= print_numcheck) {
				print_lastiter = iter;

				printf("\rProgress %d / %d", iter + 1, args.snd->numentries);
			}
		}
		localExtractionCount++;
	}

	args.totalSamples->fetch_add(localExtractionCount);
	delete[] samplebuffer;

	// Cleanup temporary files
	// We have to do things this way because, in rare
	// circumtances, something is causing active file handles to remain
	// after vgmstream finishes executing.
	std::error_code err;
	audiotempfile.replace_extension(".opus");
	while(exists(audiotempfile))
		remove(audiotempfile, err);
	audiotempfile.replace_extension(".wav");
	while(exists(audiotempfile))
		remove(audiotempfile, err);
}

void AudioExtractor(const configdata_t& config) 
{
	using namespace std::filesystem;

	const gamebit_t GAME = exists(config.inputdir / "DOOMTheDarkAges.exe") ? game_darkages : game_eternal;

	if (!exists("vgmstream/vgmstream-cli.exe")) {
		atlog("FATAL ERROR: Missing vgmstream");
		return;
	}

	const fspath snddir = config.inputdir / "base/sound/soundbanks/pc";
	const fspath audiodir = config.outputdir / "audio";
	create_directories(audiodir);

	AudioSampleMap sampleMap;
	if (!sampleMap.Build_V2((config.inputdir / snddir).string())) {
		atlog("FATAL ERROR: Error while building Audio Sample map");
		return;
	}
	const sndContainerMask& ContainerMask = sampleMap.GetMask();

	// Prioritized Archive List
	std::vector<sndContainerMask::entry> archivesToExtract; 
	for (const sndContainerMask::entry& e : ContainerMask.masks) {

		for (const std::string& type : config.audiotypes) {
			if (e.fnvstring.find(type) != std::string::npos) {

				archivesToExtract.push_back(e);
				break;
			}
		}
	}
	//for(const sndContainerMask::entry& e : archivesToExtract) {
	//	std::cout << e.fnvstring << " " << e.size << "\n";
	//}
	//return;

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

		if(archiveMask.fnvstring.find("SFX") != -1 || archiveMask.fnvstring.find("sfx") != -1)
			archiveType = et_sfx;
		else if(archiveMask.fnvstring.find("MUSIC") != -1 || archiveMask.fnvstring.find("music") != -1)
			archiveType = et_music;
		else if(archiveMask.fnvstring.find("CINEMAT") != -1)
			archiveType = et_cine;
		else archiveType = et_voice;

		const fspath archivepath = snddir / (archiveMask.fnvstring + ".snd");
		const std::string archivestem = archivepath.stem().string();

		const fspath archiveoutdir = audiodir / ( 
			archivestem.substr(0,  archivestem.find_first_of('_')) 
			+ (config.decode_samples ? "" : "_encoded") 
		);
		//const fspath archiveoutdir = audiodir / archivepath.stem(); // If we want to extract to separate folders

		create_directory(archiveoutdir);
		atlog("Extracting from %ls", archivepath.filename().c_str());


		aksnd snd;
		snd.ReadFrom(archivepath.string().c_str(), GAME);

		// In case the container masks wind up being out-of-order
		if(snd.numentries > archiveMask.size) {
			atlog("FATAL: Entry count larger than container mask");
			return;
		}

		u32 nextSample = 0;
		std::atomic<int> totalSamples = 0;
		std::thread threadpool[THREADMAX];
		
		int threadsToUse;
		if (snd.numentries < 500) {
			threadsToUse = 4;
		} else threadsToUse = 8;

		if(threadsToUse > config.max_audio_threads)
			threadsToUse = config.max_audio_threads;

		assert(threadsToUse > 0 && threadsToUse <= THREADMAX);

		atlog("Launching %d thread(s).", threadsToUse);

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
			args.decode_samples = config.decode_samples;

			args.ptr_nextindex = &nextSample;
			args.endindex = snd.numentries;

			threadpool[t] = std::thread(AudioThread, args);
		}

		for(int t = 0; t < threadsToUse; t++) {
			threadpool[t].join();
		}
		
		atlog("\rExtracted %d files from archive", totalSamples.load());
		if(snd.numentries - totalSamples.load() > 0)
			atlog("%u duplicates skipped", snd.numentries - totalSamples.load());
	}
	
	atlog("Audio Extractor complete");
	if (ExtractedSamples.size() == 0) {
		atlog("WARNING: No audio files were extracted.\n"
		"   In your config, remember to remove the '//' in front of the audio types you want to extract");
	}

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

	atlog("NOTICE: Detected legacy decl output dir at <output>/rs_streamfile/generated/decls\n"
	      "Attempting to rename folder to <output>/decls");

	if (exists(newdir)) {
		atlog("ERROR: Failed to rename legacy decl dir. A directory already exists at the new path");
		return;
	}

	std::filesystem::rename(legacydir, newdir);

	atlog("Successfully renamed legacy decl dir");
}

void SoundBankExtractor(const fspath& inputdir, fspath outputdir) {

	using namespace std::filesystem;

	outputdir /= "bnk";
	create_directories(outputdir);
	
	const fspath path_metadata = inputdir / "base/sound/soundbanks/pc/soundmetadata.bin";
	const fspath dir_soundbanks = inputdir / "base/sound/soundbanks/pc";
	
	akmetadata::fnvmap_t pckfnvmap;
	if(!exists(path_metadata)) {
		atlog("FATAL ERROR: Could not locate soundmetadata.bin");
		return;
	}
	else {
		BinaryOpener soundmetadata(path_metadata.string());
		akmetadata::Build(pckfnvmap, soundmetadata.data(), soundmetadata.len());
	}

	for (const auto& dir_iter : directory_iterator(dir_soundbanks)) {

		const fspath path_pck = dir_iter.path();
		if(path_pck.extension() != ".pck")
			continue;

		akpck::LangMap_t     pcklangmap;
		akpck::EntryList_t   pckentrylist;

		BinaryOpener pckopener(path_pck.string());
		akpck::Build(pcklangmap, pckopener.data(), pckopener.len());
		akpck::Build(pckentrylist, pckopener.data(), pckopener.len());

		// Pre-create language directories
		for(const auto& pair : pcklangmap) {
			fspath outfolder = outputdir / pair.second;
			create_directories(outfolder);
		}

		atlog("Extracting %llu Sound Banks From %ls", pckentrylist.size(), path_pck.stem().c_str());

		int total_extracted = 0;
		BinaryReader reader = pckopener.ToReader();
		for(const akpck::entry& bnk : pckentrylist) {
			assert(bnk.chunksize == 1);

			printf("\rProgress: %d / %d", total_extracted, (int)pckentrylist.size());

			if(pcklangmap.find(bnk.langid) == pcklangmap.end()) {
				atlog("ERROR: Could not resolve language id to string");
				continue;
			}

			if (pckfnvmap.find(bnk.id) == pckfnvmap.end()) {
				atlog("ERROR: Could not resolve bnk hash to string");
				continue;
			}

			if(!reader.Goto(bnk.offset * bnk.chunksize)) {
				atlog("ERROR: Sound bank out of bounds?");
				continue;
			}
			if(reader.GetRemaining() < bnk.size * bnk.chunksize) {
				atlog("ERROR: Sound bank too big?");
				continue;
			}


			std::string outputpath = outputdir.string();
			outputpath.push_back('/');
			outputpath.append(pcklangmap[bnk.langid]);
			outputpath.push_back('/');
			outputpath.append(pckfnvmap[bnk.id]);
			outputpath.append(".bnk");

			if(outputpath.length() > 250) {
				atlog("WARNING: Output path exceeding safe thresholds");
			}

			std::ofstream outwriter(outputpath, std::ios_base::binary);
			outwriter.write(reader.GetNext(), bnk.chunksize * bnk.size);
			outwriter.close();

			total_extracted++;
		}
		printf("\rSound Banks extracted successfully\n");
	}
}

/*
* CONSOLIDATED RESOURCE EXTRACTOR FUNCTION
*/
void ExtractorMain() {
	/*
	* REMEMBER TO UPDATE VERSION NUMBER
	*/
	atlog("Atlan Resource Extractor v4.0 by FlavorfulGecko5");

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
			atlog("FATAL ERROR: %ls is not a valid directory\n"
				  "Did you remember to set your input/output folders in " configpath "?", config.inputdir.c_str());
			return;
		}
		if (!std::filesystem::is_directory(config.outputdir)) {
			atlog("FATAL ERROR: %ls is not a valid directory\n"
				"Did you remember to set your input/output folders in " configpath "?", config.outputdir.c_str());
			return;
		}

		config.inputdir = std::filesystem::absolute(config.inputdir);
		config.outputdir = std::filesystem::absolute(config.outputdir);

		if (config.outputdir.string().size() >= 40) {
			atlog("FATAL ERROR: Output directory must be less than 40 characters.\n"
				  "This is to prevent export errors due to long filepaths.\n"
				  "Your output directory %ls is %llu characters", config.outputdir.c_str(), config.outputdir.string().size());
			return;
		}

		if (!core["run_extractor"].ValueBool(config.run_extractor)) {
			atlog("WARNING: Failed to read config bool core/run_extractor: assuming default");
		}
		if (!core["run_deserializer"].ValueBool(config.run_deserializer)) {
			atlog("WARNING: Failed to read config bool core/run_deserializer: assuming default");
		}
		if (!core["run_audio_extractor"].ValueBool(config.run_audio_extractor)) {
			atlog("WARNING: Failed to read config bool core/run_audio_extractor: assuming default");
		}
		if (!core["run_soundbank_extractor"].ValueBool(config.run_soundbank_extractor)) {
			atlog("WARNING: Failed to read config bool core/run_soundbank_extractor: assuming default");
		}
		if (!core["run_extra_scripts"].ValueBool(config.run_extra_scripts)) {
			atlog("WARNING: Failed to read config bool core/run_extra_scripts: assuming default");
		}


		EntNode& restypes = root["extractor"]["resource_types"];
		for (int i = 0; i < restypes.getChildCount(); i++) {
			EntNode& rt = *restypes.ChildAt(i);

			if(rt.IsComment())
				continue;

			config.restypes.insert(std::string(rt.getNameUQ()));
		}
		atlog("Found %llu resource types", config.restypes.size());

		EntNode& audiotypes = root["audio_extractor"]["audio_types"];
		for (int i = 0; i < audiotypes.getChildCount(); i++) {
			EntNode& at = *audiotypes.ChildAt(i);
			if(at.IsComment())
				continue;

			config.audiotypes.insert(std::string(at.getNameUQ()));
		}
		atlog("Found %llu audio types", config.audiotypes.size());

		if (!root["audio_extractor"]["max_threads"].ValueInt(config.max_audio_threads, 1, THREADMAX)) {
			atlog("WARNING: Failed to read config bool audio_extractor/max_threads: assuming default");
		}
		if (!root["audio_extractor"]["decode_samples"].ValueBool(config.decode_samples)) {
			atlog("WARNING: Failed to read config bool audio_extractor/decode_samples: assuming default");
		}

		/* Deserialization Settings */
		EntNode& deserial = root["deserializer"];
		if(!deserial["deserialize_entity_defs"].ValueBool(config.dsconfig.deserial_entitydefs)) {
			atlog("WARNING: Failed to read config bool deserializer/deserialize_entity_defs: assuming default");
		}
		if(!deserial["deserialize_logic_decls"].ValueBool(config.dsconfig.deserial_logicdecls)) {
			atlog("WARNING: Failed to read config bool deserializer/deserialize_logic_decls: assuming default");
		}
		if(!deserial["deserialize_level_files"].ValueBool(config.dsconfig.deserial_mapentities)) {
			atlog("WARNING: Failed to read config bool deserializer/deserialize_level_files: assuming default");
		}
		if (!deserial["remove_binary_files"].ValueBool(config.dsconfig.remove_binaries)) {
			atlog("WARNING: Failed to read config bool deserializer/remove_binary_files: assuming default");
		}
		if (!deserial["add_indentation"].ValueBool(config.dsconfig.indent)) {
			atlog("WARNING: Failed to read config bool deserializer/add_indentation: assuming default");
		}
		if (!deserial["include_originals"].ValueBool(config.dsconfig.include_original)) {
			atlog("WARNING: Failed to read config bool deserializer/include_originals: assuming default");
		}


	}
	catch(...) {
		atlog("FATAL ERROR: failed to parse " configpath);
		return;
	}

	/*
	* Read and verify PackageMapSpec data
	*/
	config.outputdir /= "atlan";
	std::vector<std::string> packages = PackageMapSpec::GetPrioritizedArchiveList(config.inputdir, false);

	if (packages.empty()) {
		atlog("FATAL ERROR: Could not find PackageMapSpec.json\n"
			  "Did you enter the correct path to your Dark Ages folder?");
		return;
	}
	else {
		atlog("Found DOOM The Dark Ages Folder\n"
			  "Dumping data to %ls", config.outputdir.c_str());
	}
	std::filesystem::create_directories(config.outputdir);

	/*
	* Download and load Oodle
	* (Do it here so it doesn't get downloaded to the wrong folder if install folder is input wrong)
	*/
	if(!Oodle::AtlanOodleInit(config.inputdir))
		return;

	if (config.run_extractor) {
		atlog("Performing resource extraction");

		FixLegacyDeclPath(config.outputdir);

		idclMaskFile containerMask;
		containerMask.Read(config.inputdir);

		if (containerMask.maskcount == 0) {
			atlog("FATAL ERROR: Could not read container.mask");
			return;
		}

		const fspath basepath = config.inputdir / "base";
		std::unordered_map<std::string, bool> extractedFileMap;

		charbuffer_t extraBuffer;
		ResourceEntryBuffers_t entryBuffers;
		entryBuffers.init(24000);

		// Aliasing system for logic object descriptors
		// Many of their filenames are too long to export verbatim.
		// Plus, all of them use invalid path characters like ':'
		struct {
			std::string aliases;
			int total = 0;
		} descriptorData;

		descriptorData.aliases.reserve(500000);

		std::ofstream outputstream;

		for(size_t i = 0; i < packages.size(); i++) {
			fspath respath = basepath / packages[i];
			int filecount = 0;

			atlog("Extracting from %ls", respath.filename().c_str());

			ResourceArchive archive;
			idcl::ReadResource(archive, respath.c_str(), RF_SkipData, true);

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
							atlog("Container Mask: Re-Extracting %s", setstring.c_str());
							#endif
						}
						else {
							continue;
						}

					}
					filecount++;
				}

				// Get the entry data
				ResourceEntryData_t entrydata = Get_EntryData(e, archive.filehandle, entryBuffers);
				if (entrydata.returncode != EntryDataCode::OK) {
					if (entrydata.returncode == EntryDataCode::UNKNOWN_COMPRESSION) {
						atlog("ERROR: Unknown compression format %hhu on file %s/%s", e.compMode, typestring, namestring);
					}
					else {
						atlog("ERROR: Failure code %d on file %s/%s", (int)entrydata.returncode, typestring, namestring);
						continue;
					}
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
						if (c == '/')
							c = '@';
					}
				}
				else if (strcmp(typestring, "compfile") == 0) {
					adjustedNameString = namestring;
					if (adjustedNameString.find(".entities") != -1) {
						for (char& c : adjustedNameString) {
							if(c == '/')
								c = '@';
						}
					}
					// Decompress and swap the data we're outputting
					if (idcl::compfile_decompress((char*)entrydata.buffer, entrydata.length, extraBuffer)) {
						entrydata.buffer = extraBuffer.data;
						entrydata.length = extraBuffer.length;
					}
				}
				else if (strcmp(typestring, "binaryFile") == 0) {
					if (std::string(namestring).find(".blang") != -1) {
						idcl::blang_decrypt((char*)entrydata.buffer, entrydata.length, namestring, extraBuffer);
						entrydata.buffer = extraBuffer.data;
						entrydata.length = extraBuffer.length;

						std::string txt;
						idcl::blang_totxt(entrydata.buffer, entrydata.length, txt);

						fspath txt_outpath = config.outputdir / typestring / namestring;
						txt_outpath.replace_extension(".txt");
						outputstream.open(txt_outpath, std::ios_base::binary);
						outputstream << txt;
						outputstream.close();
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
						atlog("WARNING: Filepath %ls exceeding safe limit. Unexpected behavior may occur", output_path.c_str());
				}

				// Write the file
				outputstream.open(output_path, std::ios_base::binary);
				if(!outputstream.good()) {
					atlog("ERROR: Failed to open file %ls for writing", output_path.c_str());
				}
				outputstream.write(entrydata.buffer, entrydata.length);
				outputstream.close();
			}

			atlog("Extracted %d files from archive", filecount);
		}

		// Write the LogicObjectDescriptor alias file, if it's populated
		if(descriptorData.total > 0) {
			outputstream.open(config.outputdir / "logicObjectDescriptor/aliases.txt", std::ios_base::binary);
			outputstream << descriptorData.aliases;
			outputstream.close();
		}
		atlog("Extraction Complete: %llu files extracted in total", extractedFileMap.size());
	}
	else {
		atlog("Skipping resource extraction");
	}

	if(config.run_deserializer) {
		Deserializer::DeserialMain(config.inputdir, config.outputdir, config.dsconfig);
	}
	else {
		atlog("Skipping deserialization");
	}

	if (config.run_audio_extractor) {
		AudioExtractor(config);
	}
	else {
		atlog("Skipping Audio Extractor");
	}

	if (config.run_soundbank_extractor) {
		SoundBankExtractor(config.inputdir, config.outputdir);
	}
	else {
		atlog("Skipping Soundbank Extractor");
	}

	if (config.run_extra_scripts) {
		extern void RunExtraScripts(fspath gamedir, fspath outputdir);
		RunExtraScripts(config.inputdir, config.outputdir);
	}
}

int main(int argc, char* argv[]) {
	#define logpath "extractor_log.txt"

	#ifdef _DEBUG
	AtlanLogger_Init(logpath);
	ExtractorMain();
	AtlanLogger_Shutdown();
	#else

	try {
		AtlanLogger_Init(logpath);
		ExtractorMain();
	}
	catch (std::exception e) {
		atlog("\n\nFATAL ERROR: An unexpected crash has occurred\n"
			  "This may have left your extracted files incomplete or corrupted.\n"
			  "Error Message: %s", e.what());
	}

	atlog("\n\nThis window will close in 10 seconds");
	atlog("Output written to " logpath);
	AtlanLogger_Shutdown();
	
	std::this_thread::sleep_for(std::chrono::seconds(10));
	#endif
}