
# Atlan Modding Tools
A collection of modding tools for id Software's DOOM: The Dark Ages. Official releases and explanations for every available tool can be found on the [Releases Page](https://github.com/FlavorfulGecko5/EntityAtlan/releases)

## Atlan Mod Loader
An entirely new mod loader with vast improvements over the DOOM Eternal mod loader. Available on the [Releases Page](https://github.com/FlavorfulGecko5/EntityAtlan/releases)

## Atlan Resource Extractor

A limited resource extractor intended for files that control game logic. Available on the [Releases Page](https://github.com/FlavorfulGecko5/EntityAtlan/releases/tag/Extractor)

## Atlan Mod Packager

A tool for packaging your mods into a ZIP file ready for publishing. This process includes reserializing your entity files. Available on the [Releases Page](https://github.com/FlavorfulGecko5/EntityAtlan/releases/tag/Extractor)

## Technical Details

Unlike in DOOM Eternal, map entities are pre-serialized into a binary format. For level modding to be achievable, it's critical that a tool exists to decode these files, and re-encode our modded ones.

Atlan Modding Tools accomplish this goal in 2 steps:
* Step 1: Parse the Runtime Type Information (RTTI) dumped from DOOM: The Dark Ages' executable file to generate reflection code.
* Step 2: Use this reflection code to deserialize and reserialize entity files.

Atlan Resource Extractor contains the deserializer, and Atlan Mod Packager contains the reserializer. Together they represent Atlan Modding Tools' crowning achievement: a reflection code generator for idTech RTTI.

## Compiling

Want to clone the repository and compile these tools yourself? Here's a guide to get you started. Visual Studio is required.

The generated reflection code files are excluded from the repository. To build the Deserializer and Reserializer, you must first run the `reflector` project.
1. Download the latest idLib JSON + header from the [Releases Page](https://github.com/FlavorfulGecko5/EntityAtlan/releases/tag/Extractor)
2. Extract the zip file's contents into the `reflector` folder
3. Run the `reflector` project.
4. The following files will be created once the reflector finishes running:
   * `deserializer/src/deserialgenerated.[h/cpp]`
   * `reserializer/src/reserialgenerated.[h/cpp]`
5. Ensure these files are included in the solution
6. The Deserializer, Reserializer, and every project dependent on them will now successfully compile.

Note: The `reflector` project is intended to only run with Debug build configurations, never as a Release build.

## Credits
* FlavorfulGecko5 - Author of the Atlan Modding Tools
* Proteh - Author of [DarkAgesPatcher](https://github.com/dcealopez/DarkAgesPatcher) and DarkAgesModManager, which both ship with Atlan Mod Loader
* jandk / tjoener - Extracting RTTI data from the game executable. Provided additional help with file formats, hashing algorithms, and other challenges. Author of idTech resource extractor [Valen](https://github.com/jandk/valen)





