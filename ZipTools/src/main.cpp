#include <cstdio>
#include <filesystem>
#include <fstream>
#include "entityslayer/EntityParser.h"
#include "miniz/miniz.h"

typedef EntNode enode;
typedef std::filesystem::path fspath;

void buildzip(const enode& zip)
{
	using namespace std::filesystem;

	const std::string_view zipstem = zip.getName();

	printf("\n\nZipping %.*s\n-----\n", (int)zipstem.length(), zipstem.data());

	mz_zip_archive zipfile;
	mz_zip_archive* zptr = &zipfile;
	mz_zip_zero_struct(zptr);
	mz_zip_writer_init_heap(zptr, 0, 0);

	for (int i = 0; i < zip.getChildCount(); i++) {
		const std::string_view entryview = zip[i].getNameUQ();

		printf("Adding %.*s\n", (int)entryview.length(), entryview.data());

		const fspath       entrypath = entryview;
		const std::string  entryname = entrypath.filename().string();

		if (!exists(entrypath)) {
			printf("- ERROR: File not found\n");
			continue;
		}

		if (is_directory(entrypath)) {

			// Add an empty directory incase there's nothing inside it
			const std::string slashedName = entryname + '/';
			mz_zip_writer_add_mem(zptr, slashedName.c_str(), nullptr, 0, MZ_DEFAULT_COMPRESSION);

			// Edge Case: Don't recursively add stuff from this directory
			// if we explicitly want it to be empty
			if(zip[i].getValueUQ() == "empty")
				continue;
			
			for (const auto& dir_iter : recursive_directory_iterator(entrypath)) 
			{
				if(is_directory(dir_iter))
					continue;

				const std::string subpath = dir_iter.path().string();
				const std::string subZipName = subpath.substr(entrypath.string().size() - entryname.size());

				printf("   %s\n", subZipName.c_str());
				mz_zip_writer_add_file(zptr, subZipName.c_str(), subpath.c_str(), "", 0, MZ_DEFAULT_COMPRESSION);
			}
		}

		else {
			mz_zip_writer_add_file(zptr, entryname.c_str(), entrypath.string().c_str(), "", 0, MZ_DEFAULT_COMPRESSION);
		}
		
	}

	void* buffer = nullptr;
	size_t buffersize = 0;
	bool finalize = mz_zip_writer_finalize_heap_archive(zptr, &buffer, &buffersize);
	if (!finalize) {
		printf("ERROR: Failed to finalize zip archive\n");
		return;
	}
	mz_zip_writer_end(zptr);

	std::ofstream outwriter(std::string(zipstem) + ".zip", std::ios_base::binary);
	outwriter.write((char*)buffer, buffersize);
	outwriter.close();
	delete[] buffer;
}

int main()
{
	try {
		EntityParser parser("zipscript.txt", ParsingMode::PERMISSIVE);

		const enode& root = *parser.getRoot();

		for (int i = 0; i < root.getChildCount(); i++) {
			buildzip(root[i]);
		}
	}
	catch (...) {
		printf("Exception caught. Bad file format?\n");
	}

}