# Making Mods
A breakdown of what Atlan Mod Loader supports

## What is possible?
The following file types are supported:
* Decl files
* entitydefs - Requires reserialization via [AtlanModPackager](https://github.com/FlavorfulGecko5/EntityAtlan/releases/tag/Extractor)
* Logic Decls - Requires reserialization via [AtlanModPackager](https://github.com/FlavorfulGecko5/EntityAtlan/releases/tag/Extractor)
	* :warning: Support for modded Logic Decls is tentative and may not work. However, it's still enabled so modders can experiment and see what's possible. If your mod contains logic decls, please thoroughly test for bugs and stability issues before publishing it.
* Map Entities - Requires reserialization via [AtlanModPackager](https://github.com/FlavorfulGecko5/EntityAtlan/releases/tag/Extractor)
    * Please read to the very end of this file for additional notes on map entities.

You can replace existing files, or add your own original files. Thanks to the revolutionary advancements of Atlan Mod Loader, all stability issues with adding new assets are eliminated. AssetsInfo JSONs are not needed, and you don't need to care about resource patch priority anymore!

The paths of all decl files must begin with their resource type.
* Decl Files: `rs_streamfile` (i.e. `rs_streamfile/generated/decls/my_decl.decl`)
* EntityDefs: `entityDef`
* Logic Decls: `logicClass`, `logicEntity`, etc.

To get started with making mods, use [Atlan Resource Extractor](https://github.com/FlavorfulGecko5/EntityAtlan/releases/tag/Extractor) to extract the game's decl files!

### Mod Config File
Atlan Mod Loader uses `darkagesmod.txt` as a mod configuration file. Including one in your mod zip is optional, but highly recommended! 

Here is an example:
```
modinfo = {
    name = "Your Awesome Mod"
    author = "Your Name Here"
    description = "Your Mod Description"
    version = "3.0"
    loadPriority = 1
    requiredVersion = 1
}
aliasing = {
    "my_first_decl.decl" = "rs_streamfile/generated/decls/ability_dash/my_ability_dash_decl.decl"
    "my_second_decl.decl" = "rs_streamfile/generated/decls/ability_dash/default.decl"
}
```

If you've created mods for DOOM Eternal, most of this will be familiar. But there are a few key differences:
* We've moved away from JSON in favor of a simpler, decl-like syntax.
* The Mod Manager properties are now encased in a `modinfo` block. For those new to modding:
	* `name` - Your mod's name - displayed in the Mod Manager
	* `author` - A list of authors - displayed in the Mod Manager
	* `description` - Your mod's description - displayed in the Mod Manager
	* `version` - Your mod's version - displayed in the Mod Manager
	* `loadPriority`- If multiple mods edit the same file, their load priority is used to help resolve conflicts. Mods with a lower load priority will override mods with a higher load priority
	* `requiredVersion` - The version of *Atlan Mod Loader* you must be running to load this mod.   
		* Decl mods require a minimum version of 1
		* EntityDef and Logic Decl mods require a minimum version of 2
		* Mapentities mods require a minimum version of 3
* The `aliasing` block is an *optional* new feature. Is the name of your mod file extremely long and convoluted? Name it something simpler, then use your config. file to declare what's it name should be when mods are loaded!

### Zip Format
A major goal of Atlan Mod Loader is improving the developer experience when making mods. The config file's `aliasing` system is one way of achieving this. Another massive improvement is making directories optional!

Is your mod file nested inside of 5 or 6 different folders? Use `@` symbols in the filename, instead of creating *literal* directories.
For example: `rs_streamfile/generated/decls/strings/ui/mainmenu/a/b/c/somestring.decl` is encased in 9 folders! Eliminate them all by naming your file `rs_streamfile@generated@decls@strings@ui@mainmenu@a@b@c@somestring.decl`

You can also mix and match! Have a folder named `rs_streamfile@generated@decls@strings` and name your file `ui@mainmenu@a@b@c@somestring.decl`. Find a setup and style most convenient for you!

### Example Zip
To illustrate these new convenience features, I've uploaded a [sample mod zip.](https://github.com/FlavorfulGecko5/EntityAtlan/blob/master/documentation/Example_Mod.zip) Download it and check out the power at your fingertips!

## EntityDefs and Logic Decls versus Normal Decls
The differences between normal decls, and the serialized entitydefs/decls are minimal. But there are some key differences you must understand when editing them.

### File Paths

With normal decls, a reference to an entityDef file might look like this
`entity = "projectile_ent/my_projectile_entity"`

Inside serialized files, this same filepath will look like:
`entity = "entityDef/projectile_ent/my_projectile_entity"`

The path before the first `/` is the file type. *To correctly reserialize the file, you MUST ensure the file type is there, and capitalized accurately.*

### Color Values

Serialized files use *Linear RGB* for color values. Example:
```
127.5 / 255 = 0.5
my_idColor = {
	r = 0.5; 
	g = 0.5;
	b = 0.5;
	a = 0.5;
}

```

## MapEntities

Map entities are the equivalent of DOOM Eternal's .entities files. You'll notice several key differences between the two games' files.

### Prepare for Unforeseen Consequences

Map Entities modding is new, with more questions than answers. We have no idea how stable it is or what bugs may exist. Expect the unexpected, and please collaborate with each other to identify issues and develop a good work flow. Even if it's perfectly stable, making a level mod from scratch will naturally be a time consuming task.

### Submap Chunks

First and foremost, you'll notice every entity has a number assigned to it. This value is the submap index.

```
entity 4 {
 <entity data>
}
```
In DOOM Eternal, entity files were a single, long, uninterrupted list of entities. In Dark Ages, entities are split into groups that I call "submap chunks". Each chunk has it's own, independent list of entities - including it's own world entity. The number of submap chunks is different for every map. 

The exact purpose and properties of submap chunks remain unknown. When creating new entities, I would strongly advise assigning it to a submap chunk used by entities with a nearby spawnPosition. However, I would also encourage experimentation to learn what submap chunks allow, and won't allow. **Only use a submap index that already exists in the vanilla file.**


### Layers
DOOM Eternal made constant use of the layer's system. It still exists in Dark Ages, but only for specific purposes.

If an entity has a layer, it will look something like this:
```
entity 14 {
	layerIndex = 1;
	layers = {
		"spawn_target_layer"
	}
    entityDef some_entity { ... }
}
```

Dark Ages entities can only be added to ONE layer. The `layerIndex` property is important. You may think of it as the "ID" for a layer. It must be included when specifying a layer. **The same layer may have a different layerIndex value across different submaps.**

### End-of-File Data

If you scroll down to the bottom of a mapentities file, you'll see a massive blob of encoded data. This is the header chunk of the original file. It is there intentionally, and **you must not touch it**. Simply pretend it doesn't exist. EntitySlayer will correctly handle the blob when opening and saving mapentities files.












